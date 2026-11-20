#include "decoder.hpp"
#include "metadata.hpp"
#include "image/image.hpp"
#include "platform/platform.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <mutex>
#include <unistd.h>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libswresample/swresample.h>
}

#define DR_WAV_IMPLEMENTATION
#define DR_WAV_NO_STDIO
#include "dr_wav.h"
#define DR_MP3_IMPLEMENTATION
#define DR_MP3_NO_STDIO
#include "dr_mp3.h"
#define DR_FLAC_IMPLEMENTATION
#define DR_FLAC_NO_STDIO
#include "dr_flac.h"
#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"

namespace mc {
static std::string ffmpeg_error(const char* operation,int code) {
    char message[AV_ERROR_MAX_STRING_SIZE]{};av_strerror(code,message,sizeof(message));
    return std::string(operation)+": "+message;
}
static size_t on_read(void* data, void* out, size_t bytes) { return static_cast<ReadOnlyFile*>(data)->read(out, bytes); }
static drwav_bool32 wav_seek(void* data, int offset, drwav_seek_origin origin) { return static_cast<ReadOnlyFile*>(data)->seek(offset, origin == DRWAV_SEEK_SET ? SEEK_SET : origin == DRWAV_SEEK_CUR ? SEEK_CUR : SEEK_END); }
static drmp3_bool32 mp3_seek(void* data, int offset, drmp3_seek_origin origin) { return static_cast<ReadOnlyFile*>(data)->seek(offset, origin == DRMP3_SEEK_SET ? SEEK_SET : origin == DRMP3_SEEK_CUR ? SEEK_CUR : SEEK_END); }
static drflac_bool32 flac_seek(void* data, int offset, drflac_seek_origin origin) { return static_cast<ReadOnlyFile*>(data)->seek(offset, origin == DRFLAC_SEEK_SET ? SEEK_SET : origin == DRFLAC_SEEK_CUR ? SEEK_CUR : SEEK_END); }
static drwav_bool32 wav_tell(void* data, drwav_int64* out) { *out = static_cast<drwav_int64>(static_cast<ReadOnlyFile*>(data)->tell()); return 1; }
static drmp3_bool32 mp3_tell(void* data, drmp3_int64* out) { *out = static_cast<drmp3_int64>(static_cast<ReadOnlyFile*>(data)->tell()); return 1; }
static drflac_bool32 flac_tell(void* data, drflac_int64* out) { *out = static_cast<drflac_int64>(static_cast<ReadOnlyFile*>(data)->tell()); return 1; }
static void* alloc_callback(size_t n, void*) { return decode_allocate(n); }
static void* realloc_callback(void* p, size_t n, void*) { return decode_reallocate(p,n); }
static void free_callback(void* p, void*) { decode_free(p); }
static FILE* vorbis_stream(ReadOnlyFile& file) {
    if (!file.seek(0, SEEK_SET)) return nullptr;
    // Vorbis uses stdio internally, but an Orbis dup can return EPERM. Keep
    // the bounded, cancellable native read/seek callbacks used by the other
    // audio decoders instead of duplicating the source descriptor.
#ifdef __APPLE__
    return funopen(&file,
        [](void* cookie, char* output, int bytes) -> int { return static_cast<int>(static_cast<ReadOnlyFile*>(cookie)->read(output, static_cast<size_t>(bytes))); },
        nullptr,
        [](void* cookie, fpos_t offset, int origin) -> fpos_t {
            auto& input = *static_cast<ReadOnlyFile*>(cookie);
            return input.seek(static_cast<int64_t>(offset), origin) ? static_cast<fpos_t>(input.tell()) : static_cast<fpos_t>(-1);
        }, nullptr);
#else
    cookie_io_functions_t callbacks{};
    callbacks.read = [](void* cookie, char* output, size_t bytes) -> ssize_t { return static_cast<ssize_t>(static_cast<ReadOnlyFile*>(cookie)->read(output, bytes)); };
    callbacks.seek = [](void* cookie, off_t* offset, int origin) -> int {
        auto& input = *static_cast<ReadOnlyFile*>(cookie);
        if (!input.seek(static_cast<int64_t>(*offset), origin)) return -1;
        *offset = static_cast<off_t>(input.tell()); return 0;
    };
    return fopencookie(&file, "rb", callbacks);
#endif
}
struct AudioDecoder::Impl {
    enum class Type { None, Wav, Mp3, Flac, Vorbis, Ffmpeg } type = Type::None;
    ReadOnlyFile file;
    drwav wav{}; drmp3 mp3{}; drflac* flac = nullptr; stb_vorbis* vorbis = nullptr; FILE* vorbis_file = nullptr;
    AudioInfo info;
    std::vector<int16_t> block;
    size_t block_offset = 0, block_frames = 0;
    int16_t a[2]{}, b[2]{};
    double phase = 0;
    uint64_t output_left = 0;
    bool initialized_samples = false;
    const std::atomic<bool>* cancel = nullptr;
    uint64_t source_read = 0;
    bool failed = false;
    void* vorbis_memory = nullptr;
    AVFormatContext* av_format = nullptr;
    AVIOContext* av_io = nullptr;
    AVCodecContext* av_codec = nullptr;
    AVPacket* av_packet = nullptr;
    AVFrame* av_frame = nullptr;
    SwrContext* av_resampler = nullptr;
    AVChannelLayout av_input_layout{};
    AVSampleFormat av_input_format = AV_SAMPLE_FMT_NONE;
    int av_input_rate = 0,av_stream = -1;
    bool av_draining = false,av_eof = false,av_resampler_drained = false;
    double av_origin = 0,av_discard_until = -1;
    std::vector<int16_t> av_pcm;
    size_t av_pcm_offset = 0;
    ~Impl() {
        if (type == Type::Wav) drwav_uninit(&wav);
        if (type == Type::Mp3) drmp3_uninit(&mp3);
        if (flac) drflac_close(flac);
        if (vorbis) stb_vorbis_close(vorbis);
        if (vorbis_file) std::fclose(vorbis_file);
        decode_free(vorbis_memory);
        av_channel_layout_uninit(&av_input_layout);swr_free(&av_resampler);
        av_frame_free(&av_frame);av_packet_free(&av_packet);avcodec_free_context(&av_codec);
        avformat_close_input(&av_format);
        if (av_io) av_freep(&av_io->buffer);
        avio_context_free(&av_io);
    }
    static int av_interrupt(void* opaque) {
        auto* self=static_cast<Impl*>(opaque);return self->cancel && self->cancel->load();
    }
    static int av_read(void* opaque,uint8_t* output,int bytes) {
        auto* self=static_cast<Impl*>(opaque);if (av_interrupt(opaque)) return AVERROR_EXIT;
        if (bytes<=0) return AVERROR(EINVAL);size_t got=self->file.read(output,static_cast<size_t>(bytes));
        if (got) return static_cast<int>(got);return self->file.tell()>=self->file.size()?AVERROR_EOF:AVERROR(EIO);
    }
    static int64_t av_seek_io(void* opaque,int64_t offset,int whence) {
        auto* self=static_cast<Impl*>(opaque);if (av_interrupt(opaque)) return AVERROR_EXIT;
        if (whence&AVSEEK_SIZE) return static_cast<int64_t>(self->file.size());whence&=~AVSEEK_FORCE;
        if (whence!=SEEK_SET && whence!=SEEK_CUR && whence!=SEEK_END) return AVERROR(EINVAL);
        return self->file.seek(offset,whence)?static_cast<int64_t>(self->file.tell()):AVERROR(EIO);
    }
    static int av_deny_external(AVFormatContext*,AVIOContext**,const char*,int,AVDictionary**) { return AVERROR(EACCES); }
    bool open_ffmpeg(std::string& error) {
        if (!file.seek(0,SEEK_SET)) { error="Cannot rewind audio file";return false; }
        auto* buffer=static_cast<uint8_t*>(av_malloc(64*1024));if (!buffer) { error="Audio I/O allocation failed";return false; }
        av_io=avio_alloc_context(buffer,64*1024,0,this,av_read,nullptr,av_seek_io);
        if (!av_io) { av_free(buffer);error="Audio I/O initialization failed";return false; }
        av_format=avformat_alloc_context();if (!av_format) { error="Audio demuxer allocation failed";return false; }
        av_format->pb=av_io;av_format->flags|=AVFMT_FLAG_CUSTOM_IO;av_format->interrupt_callback={av_interrupt,this};
        av_format->io_open=av_deny_external;av_format->probesize=8*1024*1024;av_format->max_analyze_duration=5*AV_TIME_BASE;av_format->max_streams=64;
        int result=avformat_open_input(&av_format,nullptr,nullptr,nullptr);if (result>=0) result=avformat_find_stream_info(av_format,nullptr);
        if (result<0) { error=ffmpeg_error("Cannot read audio container",result);return false; }
        av_stream=av_find_best_stream(av_format,AVMEDIA_TYPE_AUDIO,-1,-1,nullptr,0);
        if (av_stream<0) { error=ffmpeg_error("No playable audio stream",av_stream);return false; }
        auto* stream=av_format->streams[av_stream];const AVCodec* decoder=avcodec_find_decoder(stream->codecpar->codec_id);
        if (!decoder) { error=std::string("Audio decoder unavailable for ")+avcodec_get_name(stream->codecpar->codec_id);return false; }
        av_codec=avcodec_alloc_context3(decoder);if (!av_codec) { error="Audio decoder allocation failed";return false; }
        result=avcodec_parameters_to_context(av_codec,stream->codecpar);av_codec->thread_count=2;av_codec->pkt_timebase=stream->time_base;
        if (result>=0) result=avcodec_open2(av_codec,decoder,nullptr);if (result<0) { error=ffmpeg_error("Cannot initialize audio decoder",result);return false; }
        av_packet=av_packet_alloc();av_frame=av_frame_alloc();if (!av_packet || !av_frame) { error="Audio buffer allocation failed";return false; }
        double duration=av_format->duration==AV_NOPTS_VALUE?0:double(av_format->duration)/AV_TIME_BASE;
        if (duration<=0 && stream->duration!=AV_NOPTS_VALUE) duration=stream->duration*av_q2d(stream->time_base);
        if (!std::isfinite(duration) || duration<=0 || duration>43200) { error="Cannot determine a valid audio duration";return false; }
        av_origin=av_format->start_time==AV_NOPTS_VALUE?0:std::max(0.0,double(av_format->start_time)/AV_TIME_BASE);
        info.sample_rate=output_sample_rate;info.channels=2;info.duration=duration;
        info.total_frames=static_cast<uint64_t>(std::ceil(duration*output_sample_rate-1e-8));
        info.codec=std::string(av_format->iformat->name)+" / "+avcodec_get_name(av_codec->codec_id);
        auto metadata=[&](const char* key) -> std::string {
            AVDictionaryEntry* value=av_dict_get(stream->metadata,key,nullptr,0);
            if (!value) value=av_dict_get(av_format->metadata,key,nullptr,0);return value&&value->value?value->value:"";
        };
        if (info.title.empty()) info.title=metadata("title");if (info.artist.empty()) info.artist=metadata("artist");if (info.album.empty()) info.album=metadata("album");
        type=Type::Ffmpeg;return true;
    }
    bool configure_ffmpeg_audio() {
        int rate=av_frame->sample_rate>0?av_frame->sample_rate:av_codec->sample_rate;
        const auto& source=av_frame->ch_layout.nb_channels?av_frame->ch_layout:av_codec->ch_layout;AVChannelLayout layout{};
        if (rate<8000 || rate>384000 || source.nb_channels<1 || source.nb_channels>32 || av_frame->format<0) return false;
        if (source.order==AV_CHANNEL_ORDER_UNSPEC) av_channel_layout_default(&layout,source.nb_channels);
        else if (av_channel_layout_copy(&layout,&source)<0) return false;
        bool same=av_resampler && av_input_rate==rate && av_input_format==av_frame->format && av_channel_layout_compare(&layout,&av_input_layout)==0;
        if (same) { av_channel_layout_uninit(&layout);return true; }
        swr_free(&av_resampler);AVChannelLayout stereo=AV_CHANNEL_LAYOUT_STEREO;
        int result=swr_alloc_set_opts2(&av_resampler,&stereo,AV_SAMPLE_FMT_S16,output_sample_rate,&layout,static_cast<AVSampleFormat>(av_frame->format),rate,0,nullptr);
        if (result>=0) result=swr_init(av_resampler);av_channel_layout_uninit(&av_input_layout);
        if (result>=0) result=av_channel_layout_copy(&av_input_layout,&layout);av_channel_layout_uninit(&layout);
        av_input_rate=rate;av_input_format=static_cast<AVSampleFormat>(av_frame->format);return result>=0;
    }
    bool next_ffmpeg_frame(std::string& error) {
        while (!av_interrupt(this)) {
            int result=avcodec_receive_frame(av_codec,av_frame);if (result>=0) return true;
            if (result==AVERROR_EOF) { av_eof=true;return false; }
            if (result!=AVERROR(EAGAIN)) { error=ffmpeg_error("Audio decode failed",result);return false; }
            if (av_draining) { av_eof=true;return false; }
            do { av_packet_unref(av_packet);result=av_read_frame(av_format,av_packet); }
            while (result>=0 && av_packet->stream_index!=av_stream && !av_interrupt(this));
            if (av_interrupt(this)) { error="Cancelled";return false; }
            if (result==AVERROR_EOF) { av_draining=true;result=avcodec_send_packet(av_codec,nullptr); }
            else if (result>=0) result=avcodec_send_packet(av_codec,av_packet);
            av_packet_unref(av_packet);if (result<0 && result!=AVERROR_EOF && result!=AVERROR(EAGAIN)) { error=ffmpeg_error("Audio read/decode failed",result);return false; }
        }
        error="Cancelled";return false;
    }
    bool fill_ffmpeg_pcm() {
        av_pcm.clear();av_pcm_offset=0;std::string error;
        if (!next_ffmpeg_frame(error)) {
            if (!error.empty()) { failed=true;return false; }
            if (av_resampler_drained || !av_resampler) return false;av_resampler_drained=true;
            int count=swr_get_out_samples(av_resampler,0);if (count<=0 || count>int(output_sample_rate*10)) return false;
            av_pcm.resize(static_cast<size_t>(count)*2);uint8_t* output[]={reinterpret_cast<uint8_t*>(av_pcm.data())};
            int got=swr_convert(av_resampler,output,count,nullptr,0);if (got<0) { failed=true;return false; }
            av_pcm.resize(static_cast<size_t>(got)*2);return got>0;
        }
        if (!configure_ffmpeg_audio()) { failed=true;return false; }
        int count=swr_get_out_samples(av_resampler,av_frame->nb_samples);if (count<0 || count>int(output_sample_rate*10)) { failed=true;return false; }
        av_pcm.resize(static_cast<size_t>(count)*2);uint8_t* output[]={reinterpret_cast<uint8_t*>(av_pcm.data())};
        int got=swr_convert(av_resampler,output,count,const_cast<const uint8_t**>(av_frame->extended_data),av_frame->nb_samples);
        if (got<0) { failed=true;return false; }av_pcm.resize(static_cast<size_t>(got)*2);
        if (av_discard_until>=0 && av_frame->best_effort_timestamp!=AV_NOPTS_VALUE) {
            double start=av_frame->best_effort_timestamp*av_q2d(av_format->streams[av_stream]->time_base)-av_origin;
            size_t drop=static_cast<size_t>(std::max(0.0,std::min<double>(got,(av_discard_until-start)*output_sample_rate)));
            av_pcm_offset=drop;if (start+double(got)/output_sample_rate>=av_discard_until) av_discard_until=-1;
        }
        return av_pcm_offset<av_pcm.size()/2 || !av_eof;
    }
    size_t read_ffmpeg(int16_t* out,size_t frames) {
        size_t done=0;while (done<frames && !failed && !(cancel&&cancel->load())) {
            if (av_pcm_offset>=av_pcm.size()/2) { if (!fill_ffmpeg_pcm()) break;if (av_pcm_offset>=av_pcm.size()/2) continue; }
            size_t count=std::min(frames-done,av_pcm.size()/2-av_pcm_offset);
            std::memcpy(out+done*2,av_pcm.data()+av_pcm_offset*2,count*2*sizeof(int16_t));av_pcm_offset+=count;done+=count;
        }
        if (done<frames && av_eof && !failed) { size_t silence=std::min<uint64_t>(frames-done,info.total_frames>source_read+done?info.total_frames-source_read-done:0);std::fill(out+done*2,out+(done+silence)*2,0);done+=silence; }
        return done;
    }
    size_t read_source(int16_t* out, size_t frames) {
        if (cancel && cancel->load()) return 0;
        size_t n = 0;
        switch (type) {
            case Type::Wav: n=static_cast<size_t>(drwav_read_pcm_frames_s16(&wav,frames,out));break;
            case Type::Mp3: n=static_cast<size_t>(drmp3_read_pcm_frames_s16(&mp3,frames,out));break;
            case Type::Flac: n=static_cast<size_t>(drflac_read_pcm_frames_s16(flac,frames,out));break;
            case Type::Vorbis: n=static_cast<size_t>(stb_vorbis_get_samples_short_interleaved(vorbis, static_cast<int>(info.channels), out, static_cast<int>(frames * info.channels)));break;
            case Type::Ffmpeg: n=read_ffmpeg(out,frames);break;
            default: break;
        }
        source_read+=n;if (!n && source_read<info.total_frames) failed=true;return n;
    }
    bool pull(int16_t* sample) {
        if (block_offset >= block_frames) { block_frames=read_source(block.data(),1024); block_offset=0; if (!block_frames) return false; }
        auto* p=block.data()+block_offset++*info.channels; sample[0]=p[0]; sample[1]=info.channels==2?p[1]:p[0]; return true;
    }
    bool seek_frame(uint64_t frame) {
        bool ok = false;
        switch (type) {
            case Type::Wav: ok=drwav_seek_to_pcm_frame(&wav,frame)!=0; break;
            case Type::Mp3: ok=drmp3_seek_to_pcm_frame(&mp3,frame)!=0; break;
            case Type::Flac: ok=drflac_seek_to_pcm_frame(flac,frame)!=0; break;
            case Type::Vorbis: ok=frame <= UINT32_MAX && stb_vorbis_seek(vorbis,static_cast<unsigned>(frame))!=0; break;
            case Type::Ffmpeg: {
                double seconds=double(frame)/output_sample_rate;auto* stream=av_format->streams[av_stream];
                int64_t target=static_cast<int64_t>(std::floor((seconds+av_origin)/av_q2d(stream->time_base)));
                ok=av_seek_frame(av_format,av_stream,target,AVSEEK_FLAG_BACKWARD)>=0;
                if (ok) { avcodec_flush_buffers(av_codec);av_packet_unref(av_packet);av_frame_unref(av_frame);swr_free(&av_resampler);
                    av_channel_layout_uninit(&av_input_layout);av_input_format=AV_SAMPLE_FMT_NONE;av_input_rate=0;av_draining=av_eof=av_resampler_drained=false;
                    av_pcm.clear();av_pcm_offset=0;av_discard_until=seconds; }
                break;
            }
            default: break;
        }
        if (ok) { initialized_samples=false; block_frames=block_offset=0; phase=0; source_read=frame;failed=false;output_left=static_cast<uint64_t>(std::ceil(double(info.total_frames-frame)*output_sample_rate/info.sample_rate-1e-8)); }
        return ok;
    }
};
AudioDecoder::AudioDecoder() = default;
AudioDecoder::~AudioDecoder() = default;
void AudioDecoder::close() { impl_.reset(); }
const AudioInfo& AudioDecoder::info() const { static const AudioInfo empty; return impl_ ? impl_->info : empty; }
bool AudioDecoder::open(const Item& item, const std::atomic<bool>& cancel, std::string& error) {
    static std::once_flag initialized;std::call_once(initialized,[]{av_log_set_level(AV_LOG_ERROR);av_max_alloc(64*1024*1024);});
    close(); auto impl=std::unique_ptr<Impl>(new Impl); impl->cancel=&cancel;
    if (!impl->file.open(item.source,item.entry.relative,error,&cancel)) return false;
    if (impl->file.size()==0 || impl->file.size()>UINT32_MAX) { error="Audio is empty or exceeds the 4 GiB stream limit"; return false; }
    auto ext=extension(item.entry.name); bool ok=false;
    read_audio_metadata(impl->file,ext,impl->info,cancel);
    drwav_allocation_callbacks wa{nullptr,alloc_callback,realloc_callback,free_callback};
    drmp3_allocation_callbacks ma{nullptr,alloc_callback,realloc_callback,free_callback};
    drflac_allocation_callbacks fa{nullptr,alloc_callback,realloc_callback,free_callback};
    if (ext=="wav") {
        ok=drwav_init(&impl->wav,on_read,wav_seek,wav_tell,&impl->file,&wa)!=0;
        if (ok) { impl->type=Impl::Type::Wav; impl->info.sample_rate=impl->wav.sampleRate; impl->info.channels=impl->wav.channels; impl->info.total_frames=impl->wav.totalPCMFrameCount; impl->info.codec="WAV PCM";
            if ((impl->wav.translatedFormatTag!=DR_WAVE_FORMAT_PCM && impl->wav.translatedFormatTag!=DR_WAVE_FORMAT_IEEE_FLOAT)
                || (impl->wav.bitsPerSample!=8 && impl->wav.bitsPerSample!=16 && impl->wav.bitsPerSample!=24 && impl->wav.bitsPerSample!=32 && impl->wav.bitsPerSample!=64)) { error="WAV encoding is unavailable; use PCM or IEEE float"; return false; }
        }
    } else if (ext=="mp3") {
        impl->file.seek(0,SEEK_SET);
        ok=drmp3_init(&impl->mp3,on_read,mp3_seek,mp3_tell,nullptr,&impl->file,&ma)!=0;
        if (ok) { impl->type=Impl::Type::Mp3; impl->info.sample_rate=impl->mp3.sampleRate; impl->info.channels=impl->mp3.channels; impl->info.total_frames=drmp3_get_pcm_frame_count(&impl->mp3); impl->info.codec="MP3"; }
    } else if (ext=="flac") {
        impl->flac=drflac_open(on_read,flac_seek,flac_tell,&impl->file,&fa); ok=impl->flac!=nullptr;
        if (ok) { impl->type=Impl::Type::Flac; impl->info.sample_rate=impl->flac->sampleRate; impl->info.channels=impl->flac->channels; impl->info.total_frames=impl->flac->totalPCMFrameCount; impl->info.codec="FLAC"; }
    } else if (ext=="ogg") {
        impl->vorbis_file=vorbis_stream(impl->file);
        int code=0;impl->vorbis_memory=decode_allocate(8u*1024u*1024u);
        stb_vorbis_alloc va{static_cast<char*>(impl->vorbis_memory),8*1024*1024};
        if (impl->vorbis_file && impl->vorbis_memory) impl->vorbis=stb_vorbis_open_file(impl->vorbis_file,0,&code,&va); ok=impl->vorbis!=nullptr;
        if (ok) { impl->type=Impl::Type::Vorbis; auto vi=stb_vorbis_get_info(impl->vorbis); impl->info.sample_rate=vi.sample_rate; impl->info.channels=static_cast<unsigned>(vi.channels); impl->info.total_frames=stb_vorbis_stream_length_in_samples(impl->vorbis); impl->info.codec="Ogg Vorbis";
            auto comments=stb_vorbis_get_comment(impl->vorbis);
            for (int i=0;i<comments.comment_list_length && i<512 && !cancel;++i) {
                const char* comment=comments.comment_list[i];size_t length=strnlen(comment,8u*1024u*1024u);
                if (length<8u*1024u*1024u) read_vorbis_comment(comment,length,impl->info,cancel);
            }
        }
    } else if (ext=="aac" || ext=="m4a" || ext=="opus" || ext=="wma" || ext=="aif" || ext=="aiff") {
        ok=impl->open_ffmpeg(error);
    } else { error="Audio format unavailable in this build. Use WAV, MP3, FLAC, Ogg Vorbis, AAC, M4A, Opus, WMA or AIFF."; return false; }
    if (!ok || cancel) { error=cancel?"Cancelled":"Audio header or decoder initialization failed"; return false; }
    auto& info=impl->info;
    if (info.sample_rate<8000 || info.sample_rate>192000 || info.channels<1 || info.channels>2 || info.total_frames==0
        || info.total_frames>uint64_t(info.sample_rate)*43200) { error="Unsupported audio rate, channel count, duration, or missing length"; return false; }
    info.duration=double(info.total_frames)/info.sample_rate;
    impl->block.resize(1024*info.channels);
    if (!impl->seek_frame(0)) { error="Audio stream cannot seek to its start"; return false; }
    impl_=std::move(impl); return true;
}
size_t AudioDecoder::read_stereo(int16_t* out, size_t frames) {
    if (!impl_ || !out) return 0; auto& p=*impl_;
    if (!p.initialized_samples) { if (!p.pull(p.a)) return 0; if (!p.pull(p.b)) { p.b[0]=p.a[0];p.b[1]=p.a[1]; } p.initialized_samples=true; }
    size_t produced=0; double step=double(p.info.sample_rate)/output_sample_rate;
    while (produced<frames && p.output_left && !p.failed && !(p.cancel && p.cancel->load())) {
        for (int c=0;c<2;++c) out[produced*2+c]=static_cast<int16_t>(std::lround(p.a[c]*(1.0-p.phase)+p.b[c]*p.phase));
        ++produced; --p.output_left; p.phase+=step;
        while (p.phase>=1) { p.phase-=1; p.a[0]=p.b[0];p.a[1]=p.b[1]; if (!p.pull(p.b)) { p.b[0]=p.a[0];p.b[1]=p.a[1]; } }
    }
    return produced;
}
bool AudioDecoder::seek(double seconds, double& actual) {
    if (!impl_ || !std::isfinite(seconds)) return false;
    auto& info=impl_->info; uint64_t frame=static_cast<uint64_t>(std::max(0.0,std::min(info.duration,seconds))*info.sample_rate);
    frame=std::min(frame,info.total_frames); if (!impl_->seek_frame(frame)) return false; actual=double(frame)/info.sample_rate; return true;
}
bool AudioDecoder::failed() const { return impl_ && impl_->failed; }
}
