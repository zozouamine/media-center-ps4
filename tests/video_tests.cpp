#include "test.hpp"
#include "video/decoder.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
using namespace test;

namespace {
Source video_source() {
    Source source; std::string error;
    if (!source_from_path(root + "/tests/generated/video-formats", source, error)) throw std::runtime_error(error);
    return source;
}
void open_video(VideoDecoder& decoder, const Source& source, const std::string& name, const std::atomic<bool>& cancel) {
    std::string error;
    if (!decoder.open(item(source, name), cancel, error)) throw std::runtime_error(name + ": " + error);
}
void match(const std::shared_ptr<Bitmap>& frame, int r, int g, int b, const std::string& context = {}) {
    CHECK(frame); auto* center = frame->pixels.get() + (frame->height / 2 * frame->width + frame->width / 2) * 4;
    if (std::abs(int(center[0]) - r) > 6 || std::abs(int(center[1]) - g) > 6 || std::abs(int(center[2]) - b) > 6) {
        throw std::runtime_error(context + " expected RGB " + std::to_string(r) + "," + std::to_string(g) + "," + std::to_string(b) +
            "; got " + std::to_string(center[0]) + "," + std::to_string(center[1]) + "," + std::to_string(center[2]) +
            " (" + std::to_string(frame->width) + "x" + std::to_string(frame->height) + ")");
    }
    CHECK(std::abs(int(center[0]) - r) <= 6); CHECK(std::abs(int(center[1]) - g) <= 6);
    CHECK(std::abs(int(center[2]) - b) <= 6); CHECK(center[3] == 255);
}
long long energy(const int16_t* data, size_t samples) {
    long long result = 0; for (size_t i = 0; i < samples; ++i) result += std::abs(int(data[i])); return result;
}
}

TEST(real_video_containers_codecs_and_presentation_timestamps) {
    auto source = video_source(); std::atomic<bool> cancel{false};
    std::ifstream inputs(root + "/tests/generated/video-formats/fixtures.tsv"); CHECK(inputs.good());
    std::string name; double duration; int width, height, has_audio; unsigned tested = 0;
    std::vector<std::string> failures;
    while (inputs >> name >> duration >> width >> height >> has_audio) {
        try {
        VideoDecoder decoder; open_video(decoder, source, name, cancel);
        CHECK(std::abs(decoder.info().duration - duration) < .15);
        CHECK(decoder.info().width == width); CHECK(decoder.info().height == height);
        CHECK(decoder.info().has_audio == bool(has_audio));
        std::ifstream goldens(root + "/tests/generated/video-formats/frames.tsv");
        std::string fixture; double seconds, pts; int r, g, b, a; std::string error;
        while (goldens >> fixture >> seconds >> pts >> r >> g >> b >> a) {
            if (fixture != name || seconds >= decoder.info().duration) continue;
            auto frame = decoder.frame_at(seconds, error);
            if (!frame) throw std::runtime_error(name + " at " + std::to_string(seconds) + ": " + error);
            CHECK(frame->width <= screen_width); CHECK(frame->height <= screen_height);
            match(frame, r, g, b, name + " at " + std::to_string(seconds));
            double actual; CHECK(decoder.seek(seconds, actual, error)); CHECK(std::abs(actual - seconds) < .00001);
            match(decoder.frame_at(actual, error), r, g, b, name + " after seek to " + std::to_string(actual));
        }
        if (has_audio) {
            for (double target : {.6, 2.4}) {
                if (target + .1 >= decoder.info().duration) continue;
                double actual; CHECK(decoder.seek(target, actual, error));
                std::vector<int16_t> pcm(4096 * 2); size_t got = decoder.read_audio_stereo(pcm.data(), 4096);
                if (got != 4096) throw std::runtime_error(name + " audio seek at " + std::to_string(target) +
                    " returned " + std::to_string(got) + " frames; failed=" + std::to_string(decoder.audio_failed()));
                CHECK(got == 4096);
                CHECK(!decoder.audio_failed()); CHECK(energy(pcm.data(), pcm.size()) > 1000000);
            }
        }
        std::printf("Verified encoded %s: %s\n", name.c_str(), decoder.info().codec.c_str());
        decoder.close(); ++tested;
        } catch (const std::exception& exception) {
            failures.push_back(name + ": " + exception.what());
        }
    }
    if (!failures.empty()) {
        std::string errors; for (const auto& failure : failures) errors += failure + "\n";
        throw std::runtime_error(errors);
    }
    CHECK(tested >= 26); CHECK(decode_bytes_in_use() == 0);
}

TEST(variable_rate_frames_hold_until_their_actual_timestamp) {
    std::atomic<bool> cancel{false}; VideoDecoder decoder; open_video(decoder, video_source(), "variable-rate.mp4", cancel);
    std::string error; auto first = decoder.frame_at(.201, error); CHECK(first);
    CHECK(decoder.frame_at(.499, error) == first);
    auto next = decoder.frame_at(.501, error); CHECK(next); CHECK(next != first);
    CHECK(decoder.frame_at(.519, error) == next); CHECK(decoder.frame_at(.521, error) != next);
    double actual; CHECK(decoder.seek(.201, actual, error)); match(decoder.frame_at(actual, error), 47, 80, 140);
}

TEST(video_audio_resampling_has_no_packet_timestamp_clicks) {
    std::atomic<bool> cancel{false}; VideoDecoder decoder; open_video(decoder, video_source(), "audio44100.mkv", cancel);
    auto golden = bytes(root + "/tests/generated/video-formats/audio44100.s16le");
    std::vector<int16_t> decoded, block(4096 * 2);
    while (size_t got = decoder.read_audio_stereo(block.data(), 4096)) {
        decoded.insert(decoded.end(), block.begin(), block.begin() + static_cast<ptrdiff_t>(got * 2));
        CHECK(decoded.size() <= 48000 * 4 * 2);
    }
    CHECK(!decoder.audio_failed()); CHECK(decoded.size() * sizeof(int16_t) == golden.size());
    auto* reference = reinterpret_cast<const int16_t*>(golden.data());
    long long difference = 0; for (size_t i = 0; i < decoded.size(); ++i) difference += std::abs(int(decoded[i]) - reference[i]);
    CHECK(double(difference) / decoded.size() < 3);
    std::string error; double actual; CHECK(decoder.seek(.65, actual, error));
    CHECK(decoder.read_audio_stereo(block.data(), 4096) == 4096);
    difference = 0; size_t offset = static_cast<size_t>(std::llround(.65 * output_sample_rate)) * 2;
    for (size_t i = 0; i < block.size(); ++i) difference += std::abs(int(block[i]) - reference[offset + i]);
    CHECK(double(difference) / block.size() < 3);
}

TEST(encoded_audio_seek_matches_independent_sample_timeline) {
    std::atomic<bool> cancel{false}; auto source = video_source();
    for (const char* name : {"audio44100.mkv", "h264-aac.mp4"}) {
        VideoDecoder decoder; open_video(decoder, source, name, cancel);
        std::string stem = name; stem.resize(stem.find_last_of('.'));
        auto golden = bytes(root + "/tests/generated/video-formats/" + stem + ".s16le");
        auto* reference = reinterpret_cast<const int16_t*>(golden.data());
        for (double target : {.65, 2.4}) {
            double actual; std::string error; CHECK(decoder.seek(target, actual, error));
            std::vector<int16_t> pcm(4096 * 2); CHECK(decoder.read_audio_stereo(pcm.data(), 4096) == 4096);
            CHECK(!decoder.audio_failed());
            int64_t offset = std::llround(actual * output_sample_rate);
            CHECK((offset + 4096 + 49) * 2 * sizeof(int16_t) <= golden.size());
            // Allow one millisecond for container rounding/resampler phase;
            // an incorrect point in the changing-frequency signal will fail.
            double best = 1e10, best_offset = 0;
            for (int lag = -48; lag <= 48; ++lag) {
                double numerator = 0, denominator = 0;
                for (size_t i = 128 * 2; i < pcm.size(); ++i) {
                    size_t index = static_cast<size_t>((offset + lag) * 2) + i;
                    double slope = int(reference[index + 2]) - reference[index];
                    numerator += (int(pcm[i]) - reference[index]) * slope;
                    denominator += slope * slope;
                }
                // A restarted 44.1->48 kHz filter can have a fractional sample
                // phase. Fit that phase while retaining the strict time bound.
                double fraction = denominator ? std::clamp(numerator / denominator, -.5, .5) : 0;
                double difference = 0;
                for (size_t i = 128 * 2; i < pcm.size(); ++i) {
                    size_t index = static_cast<size_t>((offset + lag) * 2) + i;
                    double predicted = reference[index] + fraction * (int(reference[index + 2]) - reference[index]);
                    difference += std::abs(double(pcm[i]) - predicted);
                }
                double average = difference / (pcm.size() - 128 * 2);
                if (average < best) { best = average; best_offset = lag + fraction; }
            }
            std::printf("Audio seek %s %.3f: %.4f ms offset, %.2f sample error\n", name, target, best_offset / 48, best);
            if (best >= 80) throw std::runtime_error(std::string(name) + " audio seek at " + std::to_string(target) +
                " differs from the independent sample timeline: " + std::to_string(best));
            CHECK(best < 80);
        }
    }
}

TEST(video_audio_offsets_and_paused_seek_use_the_file_clock) {
    auto source = video_source(); std::atomic<bool> cancel{false}; VideoDecoder decoder;
    open_video(decoder, source, "delayed-audio.mkv", cancel);
    std::vector<int16_t> pcm(24000 * 2); CHECK(decoder.read_audio_stereo(pcm.data(), 24000) == 24000);
    CHECK(energy(pcm.data(), pcm.size()) == 0); CHECK(decoder.read_audio_stereo(pcm.data(), 4096) == 4096);
    CHECK(energy(pcm.data(), 4096 * 2) > 1000000);
    double actual; std::string error; CHECK(decoder.seek(.25, actual, error));
    CHECK(decoder.read_audio_stereo(pcm.data(), 24000) == 24000);
    CHECK(energy(pcm.data(), 12000 * 2) == 0); CHECK(energy(pcm.data() + 12000 * 2, 12000 * 2) > 1000000);

    auto clock = std::make_shared<AudioClock>(); MediaEngine engine([clock] { return std::unique_ptr<AudioSink>(new ControlledAudio(clock)); });
    engine.open(item(source, "h264-aac.mp4")); wait([&] { return engine.snapshot().state == Playback::Playing; });
    clock->grant(4096); wait([&] { return engine.snapshot().position > .06; });
    engine.toggle_pause(); clock->grant(1024); wait([&] { return engine.snapshot().paused; });
    auto paused = engine.snapshot(); std::this_thread::sleep_for(std::chrono::milliseconds(30));
    CHECK(engine.snapshot().position == paused.position); CHECK(engine.snapshot().frame_revision == paused.frame_revision);
    CHECK(engine.seek_to(2.4)); wait([&] { auto state = engine.snapshot(); return state.state == Playback::Paused && std::abs(state.position - 2.4) < .00001; });
    CHECK(engine.snapshot().session == paused.session); CHECK(engine.snapshot().frame_revision > paused.frame_revision);
    CHECK(clock->played == 0); CHECK(engine.seek_to(.65));
    wait([&] { auto state = engine.snapshot(); return state.state == Playback::Paused && std::abs(state.position - .65) < .00001; });
    engine.volume(0); engine.toggle_pause(); clock->grant(2048);
    wait([&] { return engine.snapshot().position > .65 && !engine.snapshot().paused; });
    { std::lock_guard<std::mutex> lock(clock->mutex); CHECK(energy(clock->last.data(), clock->last.size()) == 0); }
    engine.stop(); wait([&] { return clock->closed.load(); }); CHECK(engine.snapshot().state == Playback::Idle);
}

TEST(video_scaling_cancel_corruption_and_end_cleanup) {
    auto source = video_source(); std::atomic<bool> cancel{false}; VideoDecoder decoder;
    open_video(decoder, source, "4k-hevc10.mkv", cancel); std::string error;
    auto frame = decoder.frame_at(0, error); CHECK(frame); CHECK(frame->width == 1920); CHECK(frame->height == 1080);
    frame.reset(); decoder.close(); CHECK(decode_bytes_in_use() == 0);
    CHECK(!decoder.open(item(source, "corrupt.mp4"), cancel, error)); CHECK(!error.empty());
    CHECK(!decoder.open(item(source, "external.mp4"), cancel, error)); CHECK(!error.empty());
    cancel = true; CHECK(!decoder.open(item(source, "h264-aac.mp4"), cancel, error)); cancel = false;
    auto clock = std::make_shared<AudioClock>(); MediaEngine engine([clock] { return std::unique_ptr<AudioSink>(new ControlledAudio(clock)); });
    engine.open(item(source, "hevc-aac.mkv")); wait([&] { return engine.snapshot().state == Playback::Playing; });
    clock->grant(300000); wait([&] { return engine.snapshot().state == Playback::Ended; });
    CHECK(!engine.snapshot().seekable); CHECK(engine.snapshot().position == engine.snapshot().duration);
    engine.stop(); wait([&] { return clock->closed.load(); });
    for (const char* extension : {"file.MP4", "movie.MKV", "camera.MOV", "clip.WEBM", "stream.TS", "camera.M2TS"})
        CHECK(classify(extension) == Kind::Video);
    for (const char* extension : {"disc.VOB", "legacy.WMV", "phone.3GP", "clip.FLV", "old.RMVB", "camera.MXF"})
        CHECK(classify(extension) == Kind::Other);
}
