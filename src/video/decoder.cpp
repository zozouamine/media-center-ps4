#include "decoder.hpp"
#include "platform/platform.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <mutex>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/imgutils.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

namespace mc {
namespace {
std::string av_error(const char* operation, int code) {
    char message[AV_ERROR_MAX_STRING_SIZE]{};
    av_strerror(code, message, sizeof(message));
    return std::string(operation) + ": " + message;
}
// Each stream has its own bounded demuxer. This avoids accumulating video
// packets while the audio sink waits, including files with poor interleaving.
struct Track {
    Item item;
    ReadOnlyFile file;
    AVFormatContext* format = nullptr;
    AVIOContext* io = nullptr;
    AVCodecContext* codec = nullptr;
    AVPacket* packet = nullptr;
    AVFrame* frame = nullptr;
    int stream = -1;
    unsigned seek_preroll_errors = 0;
    bool draining = false, eof = false;
    double origin = 0;
    const std::atomic<bool>* cancel = nullptr;
    ~Track() { close(); }
    static int interrupt(void* opaque) {
        auto* self = static_cast<Track*>(opaque);
        return self->cancel && self->cancel->load();
    }
    static int read(void* opaque, uint8_t* output, int size) {
        auto* self = static_cast<Track*>(opaque);
        if (interrupt(opaque)) return AVERROR_EXIT;
        if (size <= 0) return AVERROR(EINVAL);
        size_t got = self->file.read(output, static_cast<size_t>(size));
        if (got) return static_cast<int>(got);
        return self->file.tell() >= self->file.size() ? AVERROR_EOF : AVERROR(EIO);
    }
    static int64_t seek_io(void* opaque, int64_t offset, int whence) {
        auto* self = static_cast<Track*>(opaque);
        if (interrupt(opaque)) return AVERROR_EXIT;
        if (whence & AVSEEK_SIZE) return static_cast<int64_t>(self->file.size());
        whence &= ~AVSEEK_FORCE;
        if (whence != SEEK_SET && whence != SEEK_CUR && whence != SEEK_END) return AVERROR(EINVAL);
        if (!self->file.seek(offset, whence)) return AVERROR(EIO);
        return static_cast<int64_t>(self->file.tell());
    }
    static int deny_external_io(AVFormatContext*, AVIOContext**, const char*, int, AVDictionary**) {
        // Play only the selected verified USB descriptor, never a URL or a
        // playlist's external path. FFmpeg has no protocol/network support.
        return AVERROR(EACCES);
    }
    void close() {
        av_frame_free(&frame);
        av_packet_free(&packet);
        avcodec_free_context(&codec);
        avformat_close_input(&format);
        if (io) av_freep(&io->buffer);
        avio_context_free(&io);
        file.close();
        stream = -1; seek_preroll_errors = 0; draining = eof = false; cancel = nullptr;
    }
    bool open(const Item& item, const std::atomic<bool>& cancelled, AVMediaType type, std::string& error) {
        close(); cancel = &cancelled; this->item = item;
        if (!file.open(item.source, item.entry.relative, error, cancel)) return false;
        if (file.size() > static_cast<uint64_t>(INT64_MAX)) { error = "Video file is too large"; return false; }
        auto* buffer = static_cast<uint8_t*>(av_malloc(64 * 1024));
        if (!buffer) { error = "Video I/O allocation failed"; return false; }
        io = avio_alloc_context(buffer, 64 * 1024, 0, this, read, nullptr, seek_io);
        if (!io) { av_free(buffer); error = "Video I/O initialization failed"; return false; }
        format = avformat_alloc_context();
        if (!format) { error = "Video demuxer allocation failed"; return false; }
        format->pb = io;
        format->flags |= AVFMT_FLAG_CUSTOM_IO;
        format->interrupt_callback = {interrupt, this};
        format->io_open = deny_external_io;
        format->probesize = 8 * 1024 * 1024;
        format->max_analyze_duration = 5 * AV_TIME_BASE;
        format->max_streams = 64;
        int result = avformat_open_input(&format, nullptr, nullptr, nullptr);
        if (result >= 0) result = avformat_find_stream_info(format, nullptr);
        if (result < 0) { error = av_error("Cannot read media container", result); return false; }
        stream = av_find_best_stream(format, type, -1, -1, nullptr, 0);
        if (stream < 0) { error = av_error(type == AVMEDIA_TYPE_VIDEO ? "No playable video stream" : "No playable audio stream", stream); return false; }
        auto* parameters = format->streams[stream]->codecpar;
        const AVCodec* decoder = parameters->codec_id == AV_CODEC_ID_AV1 ?
            avcodec_find_decoder_by_name("libdav1d") : avcodec_find_decoder(parameters->codec_id);
        if (!decoder) { error = std::string("Decoder unavailable for ") + avcodec_get_name(parameters->codec_id); return false; }
        codec = avcodec_alloc_context3(decoder);
        if (!codec) { error = "Video decoder allocation failed"; return false; }
        result = avcodec_parameters_to_context(codec, parameters);
        codec->thread_count = 4;
        codec->max_pixels = 16 * 1024 * 1024;
        codec->pkt_timebase = format->streams[stream]->time_base;
        if (result >= 0) result = avcodec_open2(codec, decoder, nullptr);
        if (result < 0) { error = av_error("Cannot initialize media decoder", result); return false; }
        packet = av_packet_alloc(); frame = av_frame_alloc();
        if (!packet || !frame) { error = "Media buffer allocation failed"; return false; }
        // Some demuxer versions report negative AAC/Opus encoder preroll as
        // the container start. Discard that preroll before the file's zero;
        // otherwise it delays both video and audible samples unnecessarily.
        origin = format->start_time == AV_NOPTS_VALUE ? 0 : std::max(0.0, double(format->start_time) / AV_TIME_BASE);
        return !cancelled;
    }
    bool next(std::string& error) {
        // Let demuxers skip unused payloads through AVIO seek where possible,
        // rather than reading/allocating the other track's HD video packets.
        for (unsigned index = 0; index < format->nb_streams; ++index)
            if (static_cast<int>(index) != stream) format->streams[index]->discard = AVDISCARD_ALL;
        while (!interrupt(this)) {
            int result = avcodec_receive_frame(codec, frame);
            if (result >= 0) { seek_preroll_errors = 0; return true; }
            if (result == AVERROR_EOF) { eof = true; return false; }
            if (result != AVERROR(EAGAIN)) {
                if (result == AVERROR_INVALIDDATA && seek_preroll_errors) { --seek_preroll_errors; continue; }
                error = av_error("Media decode failed", result); return false;
            }
            if (draining) { eof = true; return false; }
            do {
                av_packet_unref(packet);
                result = av_read_frame(format, packet);
            } while (result >= 0 && packet->stream_index != stream && !interrupt(this));
            if (interrupt(this)) { error = "Cancelled"; return false; }
            if (result == AVERROR_EOF) {
                draining = true;
                result = avcodec_send_packet(codec, nullptr);
            } else if (result >= 0) {
                result = avcodec_send_packet(codec, packet);
            }
            av_packet_unref(packet);
            if (result < 0 && result != AVERROR_EOF) {
                // MPEG PES and similar indexes can land inside the first audio
                // frame. Recover only within bounded post-seek preroll, ending
                // at the first valid decoded frame; normal errors stay strict.
                if (result == AVERROR_INVALIDDATA && seek_preroll_errors) { --seek_preroll_errors; continue; }
                error = av_error("Media read/decode failed", result); return false;
            }
        }
        error = "Cancelled"; return false;
    }
    double timestamp(double fallback) const {
        return frame->best_effort_timestamp == AV_NOPTS_VALUE ? fallback :
            frame->best_effort_timestamp * av_q2d(format->streams[stream]->time_base) - origin;
    }
    bool restart(std::string& error) {
        auto selected_item = item; const auto* cancelled = cancel;
        auto type = codec->codec_type; double timeline_origin = origin;
        if (!open(selected_item, *cancelled, type, error)) return false;
        origin = timeline_origin; error.clear(); return true;
    }
    bool seek(double seconds, std::string& error) {
        // At the file start a timestamp seek can skip its first GOP, especially
        // in TS/PS whose initial DTS precedes the reported presentation origin.
        if (seconds <= 0) return restart(error);
        // A coarse stream time base (notably AVI frame numbers) must seek
        // at or before the target, including a keyframe immediately after it.
        auto time = static_cast<int64_t>(std::floor((seconds + origin) / av_q2d(format->streams[stream]->time_base)));
        int result = av_seek_frame(format, stream, time, AVSEEK_FLAG_BACKWARD);
        if (result < 0) {
            if (interrupt(this)) { error = "Cancelled"; return false; }
            // A valid local container can lack an index/read_seek method.
            // Reopen at the beginning and decode/discard up to the target.
            // Common indexed MP4/MKV/AVI files retain their fast seek path.
            return restart(error);
        }
        avcodec_flush_buffers(codec); av_packet_unref(packet); av_frame_unref(frame);
        seek_preroll_errors = codec->codec_type == AVMEDIA_TYPE_AUDIO ? 16 : 0;
        draining = eof = false;
        return true;
    }
};
}

struct VideoDecoder::Impl {
    bool audio_error = false, pending_video = false, resampler_drained = false, audio_timeline = false;
    VideoInfo info;
    Track video, audio;
    SwsContext* scaler = nullptr;
    SwrContext* resampler = nullptr;
    AVFrame* selected = av_frame_alloc();
    AVChannelLayout input_layout{};
    AVSampleFormat input_format = AV_SAMPLE_FMT_NONE;
    int input_rate = 0;
    std::shared_ptr<Bitmap> bitmap;
    double current_pts = -1, pending_pts = 0, video_fallback = 0, last_request = -1;
    uint64_t audio_cursor = 0;
    int64_t pcm_start = 0;
    std::vector<int16_t> pcm;
    size_t pcm_offset = 0;
    ~Impl() {
        av_frame_free(&selected); av_channel_layout_uninit(&input_layout);
        sws_freeContext(scaler); swr_free(&resampler);
    }
    std::shared_ptr<Bitmap> blank(std::string& error) {
        auto* pixels = static_cast<uint8_t*>(decode_allocate(4));
        if (!pixels) { error = "Video display memory budget reached"; return {}; }
        pixels[0] = pixels[1] = pixels[2] = 0; pixels[3] = 255;
        auto result = std::make_shared<Bitmap>(); result->width = result->height = 1;
        result->pixels = {pixels, decode_free}; return result;
    }
    std::shared_ptr<Bitmap> convert(std::string& error) {
        auto* frame = selected;
        if (frame->width <= 0 || frame->height <= 0 || int64_t(frame->width) * frame->height > 16 * 1024 * 1024) {
            error = "Video dimensions exceed the decoder memory limit"; return {};
        }
        AVRational sar = av_guess_sample_aspect_ratio(video.format, video.format->streams[video.stream], frame);
        double pixel_aspect = sar.num > 0 && sar.den > 0 ? av_q2d(sar) : 1;
        if (!std::isfinite(pixel_aspect) || pixel_aspect < 0.01 || pixel_aspect > 100) pixel_aspect = 1;
        double display_width = frame->width * pixel_aspect;
        double scale = std::min(1.0, std::min(double(screen_width) / display_width, double(screen_height) / frame->height));
        int width = std::max(1, int(std::llround(display_width * scale)));
        int height = std::max(1, int(std::llround(frame->height * scale)));
        scaler = sws_getCachedContext(scaler, frame->width, frame->height, static_cast<AVPixelFormat>(frame->format),
                                     width, height, AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (!scaler) { error = "Video pixel format conversion unavailable"; return {}; }
        const int* coefficients = sws_getCoefficients(frame->colorspace == AVCOL_SPC_BT709 ? SWS_CS_ITU709 :
            frame->colorspace == AVCOL_SPC_BT2020_NCL ? SWS_CS_BT2020 :
            frame->colorspace == AVCOL_SPC_UNSPECIFIED && frame->height >= 720 ? SWS_CS_ITU709 : SWS_CS_ITU601);
        sws_setColorspaceDetails(scaler, coefficients, frame->color_range == AVCOL_RANGE_JPEG,
                                coefficients, 1, 0, 1 << 16, 1 << 16);
        auto* pixels = static_cast<uint8_t*>(decode_allocate(static_cast<size_t>(width) * height * 4));
        if (!pixels) { error = "Video display memory budget reached"; return {}; }
        auto result = std::make_shared<Bitmap>(); result->width = width; result->height = height;
        result->pixels = {pixels, decode_free};
        uint8_t* planes[] = {pixels, nullptr, nullptr, nullptr}; int strides[] = {width * 4, 0, 0, 0};
        if (sws_scale(scaler, frame->data, frame->linesize, 0, frame->height, planes, strides) != height) {
            error = "Video frame conversion failed"; return {};
        }
        return result;
    }
    bool configure_audio(const AVFrame* frame) {
        int rate = frame->sample_rate > 0 ? frame->sample_rate : audio.codec->sample_rate;
        AVChannelLayout layout{};
        const auto& source = frame->ch_layout.nb_channels > 0 ? frame->ch_layout : audio.codec->ch_layout;
        if (rate < 8000 || rate > 384000 || source.nb_channels < 1 || source.nb_channels > 32 || frame->format < 0) return false;
        if (source.order == AV_CHANNEL_ORDER_UNSPEC) av_channel_layout_default(&layout, source.nb_channels);
        else if (av_channel_layout_copy(&layout, &source) < 0) return false;
        bool same = resampler && input_rate == rate && input_format == frame->format &&
            av_channel_layout_compare(&layout, &input_layout) == 0;
        if (same) { av_channel_layout_uninit(&layout); return true; }
        swr_free(&resampler);
        AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
        int result = swr_alloc_set_opts2(&resampler, &stereo, AV_SAMPLE_FMT_S16, output_sample_rate,
                                        &layout, static_cast<AVSampleFormat>(frame->format), rate, 0, nullptr);
        if (result >= 0) result = swr_init(resampler);
        av_channel_layout_uninit(&input_layout);
        if (result >= 0) result = av_channel_layout_copy(&input_layout, &layout);
        av_channel_layout_uninit(&layout);
        input_rate = rate; input_format = static_cast<AVSampleFormat>(frame->format);
        return result >= 0;
    }
    bool audio_block() {
        std::string error;
        pcm.clear(); pcm_offset = 0;
        if (!audio.next(error)) {
            if (!error.empty()) { audio_error = true; return false; }
            if (resampler_drained || !resampler) return false;
            resampler_drained = true;
            int count = swr_get_out_samples(resampler, 0);
            if (count < 0 || count > int(output_sample_rate * 10)) { audio_error = true; return false; }
            if (count == 0) return false;
            pcm.resize(static_cast<size_t>(count) * 2);
            uint8_t* out[] = {reinterpret_cast<uint8_t*>(pcm.data())};
            int got = swr_convert(resampler, out, count, nullptr, 0);
            if (got < 0) { audio_error = true; return false; }
            pcm.resize(static_cast<size_t>(got) * 2);
            return got > 0;
        }
        auto* frame = audio.frame;
        if (!configure_audio(frame)) { audio_error = true; return false; }
        int64_t delay = swr_get_delay(resampler, input_rate);
        double timestamp = audio.timestamp(double(pcm_start) / output_sample_rate + double(delay) / input_rate);
        if (!std::isfinite(timestamp) || std::fabs(timestamp) > 8 * 86400) { audio_error = true; return false; }
        int64_t measured = static_cast<int64_t>(std::llround((timestamp - double(delay) / input_rate) * output_sample_rate));
        // Matroska often stores audio timestamps in whole milliseconds. Keep
        // adjacent sample blocks contiguous within that rounding precision;
        // preserve real stream offsets/gaps rather than adding clicks per packet.
        int64_t tolerance = std::max<int64_t>(2, static_cast<int64_t>(std::ceil(
            2 * av_q2d(audio.format->streams[audio.stream]->time_base) * output_sample_rate)));
        int64_t difference = measured >= pcm_start ? measured - pcm_start : pcm_start - measured;
        if (!audio_timeline || difference > tolerance) pcm_start = measured;
        audio_timeline = true;
        int count = swr_get_out_samples(resampler, frame->nb_samples);
        if (count < 0 || count > int(output_sample_rate * 10)) { audio_error = true; return false; }
        pcm.resize(static_cast<size_t>(count) * 2);
        uint8_t* out[] = {reinterpret_cast<uint8_t*>(pcm.data())};
        int got = swr_convert(resampler, out, count, const_cast<const uint8_t**>(frame->extended_data), frame->nb_samples);
        if (got < 0) { audio_error = true; return false; }
        pcm.resize(static_cast<size_t>(got) * 2);
        return true;
    }
};

VideoDecoder::VideoDecoder() : impl_(new Impl) {}
VideoDecoder::~VideoDecoder() = default;
void VideoDecoder::close() { impl_.reset(new Impl); }
const VideoInfo& VideoDecoder::info() const { return impl_->info; }
bool VideoDecoder::audio_failed() const { return impl_->audio_error; }
bool VideoDecoder::open(const Item& item, const std::atomic<bool>& cancel, std::string& error) {
    close();
    static std::once_flag initialized;
    std::call_once(initialized, [] { av_log_set_level(AV_LOG_ERROR); av_max_alloc(64 * 1024 * 1024); });
    error.clear();
    auto& self = *impl_;
    if (!self.selected) { error = "Video frame allocation failed"; close(); return false; }
    if (!self.video.open(item, cancel, AVMEDIA_TYPE_VIDEO, error)) { close(); return false; }
    auto& video = self.video; auto* stream = video.format->streams[video.stream];
    self.info.width = video.codec->width; self.info.height = video.codec->height;
    self.info.duration = video.format->duration == AV_NOPTS_VALUE ? 0 : double(video.format->duration) / AV_TIME_BASE;
    if (self.info.duration <= 0 && stream->duration != AV_NOPTS_VALUE) self.info.duration = stream->duration * av_q2d(stream->time_base);
    AVRational rate = av_guess_frame_rate(video.format, stream, nullptr);
    self.info.frame_seconds = rate.num > 0 && rate.den > 0 ? 1 / av_q2d(rate) : 1.0 / 30;
    if (!std::isfinite(self.info.duration) || self.info.duration <= 0 || self.info.duration > 7 * 86400) {
        error = "Cannot determine a finite video duration; remux the file into MP4 or MKV"; close(); return false;
    }
    self.info.frames = static_cast<uint64_t>(std::ceil(self.info.duration / self.info.frame_seconds));
    self.info.codec = std::string(video.format->iformat->name) + " / " + video.codec->codec->name;
    int audio_stream = av_find_best_stream(video.format, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (audio_stream >= 0) {
        if (!self.audio.open(item, cancel, AVMEDIA_TYPE_AUDIO, error)) { close(); return false; }
        self.audio.origin = self.video.origin;
        auto* codec = self.audio.codec;
        if (codec->sample_rate <= 0 || codec->sample_rate > 384000 || codec->ch_layout.nb_channels < 1 || codec->ch_layout.nb_channels > 32) {
            error = "Invalid audio sample rate or channel layout"; close(); return false;
        }
        self.info.has_audio = true; self.info.audio.sample_rate = codec->sample_rate;
        self.info.audio.channels = codec->ch_layout.nb_channels; self.info.audio.codec = avcodec_get_name(codec->codec_id);
        self.info.codec += " + " + self.info.audio.codec;
    }
    return true;
}
std::shared_ptr<Bitmap> VideoDecoder::frame_at(double seconds, std::string& error) {
    error.clear();
    auto& self = *impl_;
    if (!std::isfinite(seconds) || seconds < 0 || seconds >= self.info.duration) { error = "Video time is outside the file"; return {}; }
    if (self.last_request >= 0 && (seconds + 0.000001 < self.last_request || seconds > self.last_request + 2)) {
        // DTS-based indexes may return a keyframe whose presentation time is
        // later than the seek target. Decode a short preroll before selecting.
        if (!self.video.seek(std::max(0.0, seconds - 1), error)) return {};
        self.bitmap.reset(); av_frame_unref(self.selected); self.pending_video = false; self.video_fallback = seconds;
    }
    bool changed = false;
    while (!self.video.eof) {
        if (!self.pending_video) {
            if (!self.video.next(error)) {
                if (!error.empty()) return {};
                break;
            }
            self.pending_pts = self.video.timestamp(self.video_fallback);
            if (!std::isfinite(self.pending_pts) || std::fabs(self.pending_pts) > 8 * 86400) {
                error = "Invalid video presentation timestamp"; return {};
            }
            self.video_fallback = self.pending_pts + self.info.frame_seconds;
            self.pending_video = true;
        }
        if (self.pending_pts > seconds + 0.000001) {
            if (!self.bitmap && !changed) { self.bitmap = self.blank(error); if (!self.bitmap) return {}; }
            break;
        }
        av_frame_unref(self.selected);
        av_frame_move_ref(self.selected, self.video.frame);
        changed = true;
        self.current_pts = self.pending_pts; self.pending_video = false;
    }
    // Decode reference/B frames as necessary, but scale only the frame that
    // will actually be displayed, including after a distant keyframe seek.
    if (changed) { self.bitmap = self.convert(error); if (!self.bitmap) return {}; }
    self.last_request = seconds;
    if (!self.bitmap && error.empty()) error = "Video stream contains no decoded frames";
    return self.bitmap;
}
bool VideoDecoder::seek(double seconds, double& actual, std::string& error) {
    error.clear();
    auto& self = *impl_;
    if (!std::isfinite(seconds) || seconds < 0 || seconds > self.info.duration) { error = "Invalid seek time"; return false; }
    actual = seconds;
    // Seeking exactly to EOF is handled by the engine without another read.
    if (seconds < self.info.duration && !self.video.seek(std::max(0.0, seconds - 1), error)) return false;
    if (self.info.has_audio) {
        // Decode preroll to restore MP3 reservoirs and AAC/Opus state, then
        // discard samples before the requested time in read_audio_stereo.
        if (seconds < self.info.duration && !self.audio.seek(std::max(0.0, seconds - 1), error)) return false;
        swr_free(&self.resampler);
    }
    self.bitmap.reset(); av_frame_unref(self.selected); self.pending_video = false;
    self.current_pts = self.last_request = -1; self.video_fallback = seconds;
    self.audio_cursor = static_cast<uint64_t>(std::llround(seconds * output_sample_rate));
    self.pcm_start = static_cast<int64_t>(self.audio_cursor); self.pcm.clear(); self.pcm_offset = 0;
    self.audio_error = self.resampler_drained = self.audio_timeline = false;
    return true;
}
size_t VideoDecoder::read_audio_stereo(int16_t* output, size_t frames) {
    auto& self = *impl_;
    if (!self.info.has_audio || self.audio_error) return 0;
    size_t done = 0;
    while (done < frames && !Track::interrupt(&self.audio)) {
        if (self.pcm_offset >= self.pcm.size() / 2) {
            self.pcm_start += static_cast<int64_t>(self.pcm.size() / 2);
            if (!self.audio_block()) break;
            if (self.pcm.empty()) continue;
        }
        int64_t start = self.pcm_start + static_cast<int64_t>(self.pcm_offset);
        int64_t cursor = static_cast<int64_t>(self.audio_cursor);
        if (start < cursor) {
            self.pcm_offset += static_cast<size_t>(std::min<int64_t>(cursor - start, self.pcm.size() / 2 - self.pcm_offset));
            continue;
        }
        size_t count;
        if (start > cursor) {
            count = static_cast<size_t>(std::min<int64_t>(frames - done, start - cursor));
            std::fill(output + done * 2, output + (done + count) * 2, 0);
        } else {
            count = std::min(frames - done, self.pcm.size() / 2 - self.pcm_offset);
            std::memcpy(output + done * 2, self.pcm.data() + self.pcm_offset * 2, count * 2 * sizeof(int16_t));
            self.pcm_offset += count;
        }
        done += count; self.audio_cursor += count;
    }
    return done;
}
}
