#include "video/movie_player.hpp"
#include "audio/output.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <stdexcept>
#include <string>
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
    // Inputs
    AVFormatContext* format{};
    AVCodecContext* video{};
    AVCodecContext* audio{};
    AVPacket* packet{};
    AVFrame* frame{};
    SwsContext* sws{};
    SwrContext* swr{};
    int video_stream{-1}, audio_stream{-1};
    AudioOutput* audio_out{};
    bool audio_clock{};
    int output_width{}, output_height{};
    // Decoded video frames (bounded) and audio scratch buffers.
    std::deque<DecodedFrame> frames;
    std::vector<std::uint8_t> current;
    std::vector<std::uint8_t> scaled;
    std::vector<float> audio_pcm;
    // Clock/end state. With audio the SDL device consumption drives the clock so the
    // video follows the audible stream; otherwise wall time is used.
    double clock{};
    double last_pts{-1};
    double frame_gap{1.0 / 24.0};
    bool demux_eof{}, video_eof{}, audio_eof{}, finished{};
    bool audio_started{};
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
                audio_clock = true;
            }
        }
        audio_pcm.resize(4096 * 2);
        packet = av_packet_alloc();
        frame = av_frame_alloc();
        if (!packet || !frame) throw std::runtime_error("Movie decoder allocation failed");
    }
    ~Impl() {
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
    void scale_frame() {
        const AVFrame* source = frame;
        if (!sws) {
            sws = sws_getContext(source->width, source->height, static_cast<AVPixelFormat>(source->format),
                output_width, output_height, AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr);
            if (!sws) throw std::runtime_error("Movie scaler allocation failed");
            scaled.resize(static_cast<std::size_t>(output_width) * static_cast<std::size_t>(output_height) * 4);
            current.resize(scaled.size());
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
        if (last_pts >= 0 && pts > last_pts) frame_gap = std::min(1.0, std::max(0.001, pts - last_pts));
        last_pts = std::max(last_pts, pts);
        frames.push_back({scaled, pts});
        // Decode only a few frames ahead during normal playback; the larger cap tolerates
        // the burst a decoder may emit while flushing at EOF.
        if (frames.size() > 16) frames.pop_front();
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
        if (converted > 0) (void)audio_out->movie_push(audio_pcm.data(), static_cast<std::size_t>(converted));
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
    void pump() {
        int budget = 400;
        while (budget-- > 0 && !finished) {
            const bool need_video = !video_eof && frames.size() < 3;
            const bool need_audio = audio_clock && !audio_eof && audio_out && audio_out->movie_queued_frames() < 24000;
            if (!need_video && !need_audio) break;
            if (demux_eof) { flush_codecs(); break; }
            const int code = av_read_frame(format, packet);
            if (code == AVERROR_EOF) { demux_eof = true; flush_codecs(); continue; }
            require_ff(code, "Movie read");
            if (packet->stream_index == video_stream) {
                send_packet(video, packet, false);
            } else if (packet->stream_index == audio_stream && audio) {
                send_packet(audio, packet, true);
            }
            av_packet_unref(packet);
        }
    }
    void select_frame() {
        // Display every frame that has become due; keep future frames queued until the clock
        // reaches them. A movie always shows its first decoded frame immediately.
        while (!frames.empty() && frames.front().pts <= clock + 1e-6) {
            current = frames.front().pixels;
            frames.pop_front();
        }
        if (current.empty() && !frames.empty()) {
            current = frames.front().pixels;
            frames.pop_front();
        }
    }
    void advance_clock(double wall_seconds) {
        if (audio_clock && audio_out) {
            clock = static_cast<double>(audio_out->movie_played_frames()) / 48000.0;
        } else {
            clock += std::max(0.0, wall_seconds);
        }
    }
};
MoviePlayer::MoviePlayer(const std::filesystem::path& path, int width, int height, AudioOutput* audio)
    : impl_(std::make_unique<Impl>(path, width, height, audio)) {
    impl_->pump();
    if (!impl_->frames.empty()) impl_->current = impl_->frames.front().pixels;
    // Prime one frame so the first rendered frame is never blank for a normal movie.
    impl_->select_frame();
    if (impl_->current.empty()) throw std::runtime_error("Movie decoded no video frames");
}
MoviePlayer::~MoviePlayer() = default;
void MoviePlayer::update(double wall_seconds) {
    if (impl_->finished) return;
    // If the audio track ends before the video, continue on wall time so the final
    // frames are still reachable instead of freezing on the last audio sample.
    if (impl_->audio_clock && impl_->demux_eof && impl_->audio_eof &&
        (!impl_->audio_out || impl_->audio_out->movie_queued_frames() == 0))
        impl_->audio_clock = false;
    impl_->advance_clock(wall_seconds);
    impl_->pump();
    impl_->select_frame();
    if (impl_->demux_eof && impl_->video_eof && impl_->frames.empty() && !impl_->current.empty()) {
        const double end = impl_->last_pts + impl_->frame_gap * 0.5;
        const bool audio_drained = !impl_->audio_clock || !impl_->audio_out || impl_->audio_out->movie_queued_frames() == 0;
        if (impl_->clock >= end && audio_drained) {
            impl_->finished = true;
            if (impl_->audio_started && impl_->audio_out) impl_->audio_out->movie_end();
        }
    }
}
void MoviePlayer::skip() {
    if (impl_->finished) return;
    impl_->finished = true;
    if (impl_->audio_started && impl_->audio_out) impl_->audio_out->movie_end();
}
bool MoviePlayer::finished() const { return impl_->finished; }
const std::vector<std::uint8_t>& MoviePlayer::rgba() const { return impl_->current; }
int MoviePlayer::width() const { return impl_->output_width; }
int MoviePlayer::height() const { return impl_->output_height; }
double MoviePlayer::media_time() const { return impl_->clock; }
}
