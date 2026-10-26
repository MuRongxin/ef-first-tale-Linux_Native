#include "video/movie_player.hpp"
#include "audio/output.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}
namespace ef {
namespace {
std::string ff_error(int code) {
    std::array<char, 256> text{};
    if (av_strerror(code, text.data(), text.size()) < 0) return "FFmpeg error " + std::to_string(code);
    return text.data();
}
void require_ff(int code, const char* operation) {
    if (code < 0) throw std::runtime_error(std::string(operation) + ": " + ff_error(code));
}
}
struct MoviePlayer::Impl {
    struct DecodedFrame {
        std::vector<std::uint8_t> pixels;
        double pts{};
    };
    // FFmpeg objects are touched only by the decoder thread.
    AVFormatContext* format{};
    AVCodecContext* video{};
    AVCodecContext* audio{};
    AVPacket* packet{};
    AVFrame* frame{};
    SwsContext* sws{};
    SwrContext* swr{};
    int video_stream{-1}, audio_stream{-1};
    AudioOutput* audio_out{};
    int output_width{}, output_height{};
    std::vector<std::uint8_t> scaled;
    std::vector<float> audio_pcm;
    bool demux_eof{}, video_eof{}, audio_eof{};
    bool audio_started{};
    // Shared with the control thread.
    std::mutex mutex_;
    std::condition_variable available_;
    std::deque<DecodedFrame> frames; // guarded by mutex_
    std::atomic<bool> stop_{false};
    std::atomic<bool> eof_{false};
    std::atomic<bool> failed_{false};
    std::atomic<bool> audio_clock_{false};
    std::string failure_;
    std::thread worker_;
    // Control-thread state.
    std::vector<std::uint8_t> current;
    double clock{};
    double displayed_pts_{-1};
    double display_gap_{1.0 / 24.0};
    bool finished{};
    static constexpr std::size_t max_queued_frames = 8;
    Impl(const std::filesystem::path& path, int width, int height, AudioOutput* output)
        : audio_out(output && output->active() ? output : nullptr), output_width(width), output_height(height) {
        if (width <= 0 || height <= 0) throw std::runtime_error("Movie destination must be positive");
        if (const int code = avformat_open_input(&format, path.c_str(), nullptr, nullptr); code < 0)
            throw std::runtime_error("Cannot open movie " + path.string() + ": " + ff_error(code));
        require_ff(avformat_find_stream_info(format, nullptr), "Movie stream info");
        video_stream = av_find_best_stream(format, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
        if (video_stream < 0) throw std::runtime_error("Movie has no video stream: " + path.string());
        video = open_codec(video_stream, false);
        const int best_audio = av_find_best_stream(format, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
        if (best_audio >= 0 && audio_out) {
            audio_stream = best_audio;
            audio = open_codec(audio_stream, true);
            if (audio && audio->ch_layout.nb_channels > 0 && audio->sample_rate > 0) {
                AVChannelLayout stereo{};
                av_channel_layout_default(&stereo, 2);
                const int code = swr_alloc_set_opts2(&swr, &stereo, AV_SAMPLE_FMT_FLT, 48000,
                    &audio->ch_layout, audio->sample_fmt, audio->sample_rate, 0, nullptr);
                av_channel_layout_uninit(&stereo);
                require_ff(code, "Movie audio resampler");
                require_ff(swr_init(swr), "Movie audio resampler init");
                audio_out->movie_begin();
                audio_started = true;
                audio_clock_.store(true);
            }
        }
        audio_pcm.resize(4096 * 2);
        packet = av_packet_alloc();
        frame = av_frame_alloc();
        if (!packet || !frame) throw std::runtime_error("Movie decoder allocation failed");
        worker_ = std::thread([this] { decode_loop(); });
    }
    ~Impl() {
        stop_.store(true);
        available_.notify_all();
        if (worker_.joinable()) worker_.join();
        if (audio_started && audio_out) audio_out->movie_end();
        if (swr) swr_free(&swr);
        if (sws) sws_freeContext(sws);
        if (audio) avcodec_free_context(&audio);
        if (video) avcodec_free_context(&video);
        if (format) avformat_close_input(&format);
        if (packet) av_packet_free(&packet);
        if (frame) av_frame_free(&frame);
    }
    AVCodecContext* open_codec(int stream_index, bool audio_kind) {
        const AVCodecParameters* parameters = format->streams[stream_index]->codecpar;
        const AVCodec* codec = avcodec_find_decoder(parameters->codec_id);
        if (!codec) throw std::runtime_error(std::string("Unsupported movie codec: ") + avcodec_get_name(parameters->codec_id));
        AVCodecContext* context = avcodec_alloc_context3(codec);
        if (!context) throw std::runtime_error("Movie decoder allocation failed");
        require_ff(avcodec_parameters_to_context(context, parameters), "Movie codec parameters");
        context->pkt_timebase = format->streams[stream_index]->time_base;
        if (!audio_kind) context->thread_count = 0;
        require_ff(avcodec_open2(context, codec, nullptr), "Movie decoder open");
        return context;
    }
    void fail(const std::string& message) {
        {
            std::lock_guard lock(mutex_);
            failure_ = message;
        }
        failed_.store(true);
        available_.notify_all();
    }
    void finish_worker() {
        eof_.store(true);
        available_.notify_all();
    }
    void wake_if_stopped() {
        if (stop_.load()) throw std::runtime_error("Movie playback stopped");
    }
    void push_frame(DecodedFrame&& decoded) {
        std::unique_lock lock(mutex_);
        available_.wait(lock, [this] { return stop_.load() || frames.size() < max_queued_frames; });
        if (stop_.load()) throw std::runtime_error("Movie playback stopped");
        frames.push_back(std::move(decoded));
        available_.notify_all();
    }
    void push_audio(const float* samples, std::size_t frame_count) {
        if (!audio_out) return;
        std::size_t done = 0;
        while (done < frame_count && !stop_.load()) {
            const std::size_t pushed = audio_out->movie_push(samples + done * 2, frame_count - done);
            done += pushed;
            if (pushed == 0) std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    void scale_frame() {
        const AVFrame* source = frame;
        if (!sws) {
            sws = sws_getContext(source->width, source->height, static_cast<AVPixelFormat>(source->format),
                output_width, output_height, AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr);
            if (!sws) throw std::runtime_error("Movie scaler allocation failed");
            scaled.resize(static_cast<std::size_t>(output_width) * static_cast<std::size_t>(output_height) * 4);
        }
        if (source->width != video->width || source->height != video->height)
            throw std::runtime_error("Movie frame dimensions changed mid-stream");
        std::uint8_t* planes[4]{scaled.data(), nullptr, nullptr, nullptr};
        int strides[4]{output_width * 4, 0, 0, 0};
        const int rows = sws_scale(sws, source->data, source->linesize, 0, source->height, planes, strides);
        if (rows != output_height) throw std::runtime_error("Movie scaler returned a partial frame");
    }
    void queue_video_frame() {
        scale_frame();
        const AVRational time_base = format->streams[video_stream]->time_base;
        const std::int64_t stamp = frame->best_effort_timestamp != AV_NOPTS_VALUE
            ? frame->best_effort_timestamp : (frame->pts != AV_NOPTS_VALUE ? frame->pts : 0);
        const double pts = static_cast<double>(stamp) * av_q2d(time_base);
        push_frame({scaled, pts});
    }
    void queue_audio_frame() {
        if (!swr || !audio_out) return;
        const int in_samples = frame->nb_samples;
        const int out_capacity = static_cast<int>(av_rescale_rnd(swr_get_delay(swr, audio->sample_rate) + in_samples,
            48000, audio->sample_rate, AV_ROUND_UP));
        const auto needed = static_cast<std::size_t>(std::max(out_capacity, 0)) * 2;
        if (audio_pcm.size() < needed) audio_pcm.resize(needed);
        std::uint8_t* output = reinterpret_cast<std::uint8_t*>(audio_pcm.data());
        const int converted = swr_convert(swr, &output, out_capacity,
            const_cast<const std::uint8_t**>(frame->extended_data), in_samples);
        if (converted > 0) push_audio(audio_pcm.data(), static_cast<std::size_t>(converted));
    }
    void drain_video() {
        while (true) {
            const int code = avcodec_receive_frame(video, frame);
            if (code == AVERROR(EAGAIN) || code == AVERROR_EOF) break;
            require_ff(code, "Movie video decode");
            queue_video_frame();
            av_frame_unref(frame);
        }
    }
    void drain_audio() {
        if (!audio) return;
        while (true) {
            const int code = avcodec_receive_frame(audio, frame);
            if (code == AVERROR(EAGAIN) || code == AVERROR_EOF) break;
            require_ff(code, "Movie audio decode");
            queue_audio_frame();
            av_frame_unref(frame);
        }
    }
    void send_packet(AVCodecContext* decoder, AVPacket* input, bool audio_kind) {
        int code = avcodec_send_packet(decoder, input);
        if (code == AVERROR(EAGAIN)) {
            if (audio_kind) drain_audio(); else drain_video();
            code = avcodec_send_packet(decoder, input);
        }
        require_ff(code, audio_kind ? "Movie audio packet" : "Movie video packet");
        if (audio_kind) drain_audio(); else drain_video();
    }
    void flush_codecs() {
        if (!video_eof) {
            send_packet(video, nullptr, false);
            video_eof = true;
        }
        if (audio && !audio_eof) {
            send_packet(audio, nullptr, true);
            audio_eof = true;
        }
    }
    void decode_loop() {
        try {
            while (!stop_.load()) {
                // Bound the decode-ahead window; the control thread releases space as it
                // displays frames.
                {
                    std::unique_lock lock(mutex_);
                    available_.wait_for(lock, std::chrono::milliseconds(20),
                        [this] { return stop_.load() || frames.size() < max_queued_frames; });
                    if (stop_.load()) break;
                    if (frames.size() >= max_queued_frames) continue;
                }
                // Keep the audio ring around a second full; this prevents a fast decoder from
                // dropping samples while the device consumes at real time.
                while (!stop_.load() && audio_clock_.load() && audio_out && audio_out->movie_queued_frames() > 48000)
                    std::this_thread::sleep_for(std::chrono::milliseconds(3));
                wake_if_stopped();
                if (demux_eof) {
                    flush_codecs();
                    finish_worker();
                    break;
                }
                const int code = av_read_frame(format, packet);
                if (code == AVERROR_EOF) {
                    demux_eof = true;
                    flush_codecs();
                    finish_worker();
                    break;
                }
                require_ff(code, "Movie read");
                if (packet->stream_index == video_stream) {
                    send_packet(video, packet, false);
                } else if (packet->stream_index == audio_stream && audio) {
                    send_packet(audio, packet, true);
                }
                av_packet_unref(packet);
            }
        } catch (const std::exception& e) {
            if (!stop_.load()) fail(e.what());
        }
    }
    // Control thread: wait for the first decoded frame (or a decoder error) so the first
    // rendered frame is never blank.
    void wait_initial() {
        std::unique_lock lock(mutex_);
        const bool ready = available_.wait_for(lock, std::chrono::seconds(10),
            [this] { return stop_.load() || failed_.load() || eof_.load() || !frames.empty(); });
        if (failed_.load()) throw std::runtime_error(failure_.empty() ? "Movie decode failed" : failure_);
        if (!ready) throw std::runtime_error("Movie decoder stalled before the first frame");
        if (!frames.empty()) {
            current = std::move(frames.front().pixels);
            displayed_pts_ = frames.front().pts;
            frames.pop_front();
            available_.notify_all();
        }
        if (current.empty()) throw std::runtime_error("Movie decoded no video frames");
    }
    void advance_clock(double wall_seconds) {
        if (audio_clock_.load() && audio_out) {
            clock = static_cast<double>(audio_out->movie_played_frames()) / 48000.0;
        } else {
            clock += std::max(0.0, wall_seconds);
        }
    }
    void select_due_frames() {
        std::deque<DecodedFrame> due;
        {
            std::lock_guard lock(mutex_);
            while (!frames.empty() && frames.front().pts <= clock + 1e-6) {
                due.push_back(std::move(frames.front()));
                frames.pop_front();
            }
            if (current.empty() && !frames.empty()) {
                due.push_back(std::move(frames.front()));
                frames.pop_front();
            }
            if (!due.empty()) available_.notify_all();
        }
        for (auto& decoded : due) {
            if (displayed_pts_ >= 0 && decoded.pts > displayed_pts_)
                display_gap_ = std::min(1.0, std::max(0.001, decoded.pts - displayed_pts_));
            displayed_pts_ = std::max(displayed_pts_, decoded.pts);
            current = std::move(decoded.pixels);
        }
    }
};
MoviePlayer::MoviePlayer(const std::filesystem::path& path, int width, int height, AudioOutput* audio)
    : impl_(std::make_unique<Impl>(path, width, height, audio)) {
    impl_->wait_initial();
}
MoviePlayer::~MoviePlayer() = default;
void MoviePlayer::update(double wall_seconds) {
    if (impl_->finished) return;
    if (impl_->failed_.load()) {
        std::lock_guard lock(impl_->mutex_);
        throw std::runtime_error(impl_->failure_.empty() ? "Movie decode failed" : impl_->failure_);
    }
    // If the audio track ends before the video, continue on wall time so the final frames
    // are reachable instead of freezing on the last audio sample.
    if (impl_->audio_clock_.load() && impl_->eof_.load() &&
        (!impl_->audio_out || impl_->audio_out->movie_queued_frames() == 0))
        impl_->audio_clock_.store(false);
    impl_->advance_clock(wall_seconds);
    impl_->select_due_frames();
    if (impl_->eof_.load() && impl_->current.size() && !impl_->finished) {
        const double end = impl_->displayed_pts_ + impl_->display_gap_ * 0.5;
        bool frames_empty = false;
        {
            std::lock_guard lock(impl_->mutex_);
            frames_empty = impl_->frames.empty();
        }
        const bool audio_drained = !impl_->audio_clock_.load() || !impl_->audio_out ||
            impl_->audio_out->movie_queued_frames() == 0;
        if (frames_empty && impl_->clock >= end && audio_drained) {
            impl_->finished = true;
            impl_->stop_.store(true);
            impl_->available_.notify_all();
            if (impl_->audio_started && impl_->audio_out) impl_->audio_out->movie_end();
        }
    }
}
void MoviePlayer::skip() {
    if (impl_->finished) return;
    impl_->finished = true;
    impl_->stop_.store(true);
    impl_->available_.notify_all();
    if (impl_->audio_started && impl_->audio_out) impl_->audio_out->movie_end();
}
bool MoviePlayer::finished() const { return impl_->finished; }
const std::vector<std::uint8_t>& MoviePlayer::rgba() const { return impl_->current; }
int MoviePlayer::width() const { return impl_->output_width; }
int MoviePlayer::height() const { return impl_->output_height; }
double MoviePlayer::media_time() const { return impl_->clock; }
}
