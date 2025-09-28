#include <iostream>
#include <fstream>
#include <memory>
#include <string>
#include <stdexcept>

// --- FFmpeg Headers ---
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
#include <libavutil/error.h>

// --- Constants ---
constexpr int WIDTH = 1920;
constexpr int HEIGHT = 1080;
constexpr int FPS = 60;
constexpr AVPixelFormat PIX_FMT = AV_PIX_FMT_YUV420P;

// --- RAII Smart Pointers ---
template <typename T, void (*Deleter)(T**)> 
using FFmpegPtr = std::unique_ptr<T, std::function<void(T*)>>;

using CodecCtxPtr  = std::unique_ptr<AVCodecContext, decltype(&avcodec_free_context)>;
using FormatCtxPtr = std::unique_ptr<AVFormatContext, std::function<void(AVFormatContext*)>>;
using FramePtr     = std::unique_ptr<AVFrame, decltype(&av_frame_free)>;
using PacketPtr    = std::unique_ptr<AVPacket, decltype(&av_packet_free)>;

inline CodecCtxPtr make_codec_ctx(const AVCodec* codec) {
    return { avcodec_alloc_context3(codec), avcodec_free_context };
}
inline FramePtr make_frame() {
    return { av_frame_alloc(), av_frame_free };
}
inline PacketPtr make_packet() {
    return { av_packet_alloc(), av_packet_free };
}
inline FormatCtxPtr make_format_ctx(AVFormatContext* raw) {
    return { raw, [](AVFormatContext* ctx) {
        if (ctx) {
            if (ctx->pb) avio_closep(&ctx->pb);
            avformat_free_context(ctx);
        }
    }};
}

// --- Error Handling ---
inline void check_ffmpeg(int ret, const std::string& msg) {
    if (ret < 0 && ret != AVERROR(EAGAIN) && ret != AVERROR_EOF) {
        char buf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, buf, sizeof(buf));
        throw std::runtime_error(msg + ": " + buf);
    }
}

// --- YUV Reader ---
class YUVReader {
public:
    explicit YUVReader(const std::string& filename)
        : frame_size(WIDTH * HEIGHT * 3 / 2), file(filename, std::ios::binary) {
        if (!file) throw std::runtime_error("Failed to open: " + filename);
    }

    FramePtr next() {
        auto frame = make_frame();
        frame->width  = WIDTH;
        frame->height = HEIGHT;
        frame->format = PIX_FMT;
        check_ffmpeg(av_frame_get_buffer(frame.get(), 32), "Frame buffer alloc failed");

        if (!file.read(reinterpret_cast<char*>(frame->data[0]), frame_size))
            return nullptr;

        frame->linesize[0] = WIDTH;
        frame->linesize[1] = WIDTH / 2;
        frame->linesize[2] = WIDTH / 2;
        frame->data[1] = frame->data[0] + WIDTH * HEIGHT;
        frame->data[2] = frame->data[1] + WIDTH * HEIGHT / 4;
        return frame;
    }

private:
    std::ifstream file;
    size_t frame_size;
};

// --- Encoder ---
class Encoder {
public:
    explicit Encoder(AVCodecContext* ctx) : ctx(ctx) {}

    PacketPtr encode(FramePtr& frame) {
        if (frame) frame->pts = pts++;
        check_ffmpeg(avcodec_send_frame(ctx, frame ? frame.get() : nullptr),
                     "Send frame failed");

        auto pkt = make_packet();
        int ret = avcodec_receive_packet(ctx, pkt.get());
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) return nullptr;
        check_ffmpeg(ret, "Receive packet failed");
        return pkt;
    }

private:
    AVCodecContext* ctx;
    int64_t pts = 0;
};

// --- Init Helpers ---
CodecCtxPtr init_codec() {
    const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!codec) throw std::runtime_error("H.264 encoder not found");

    auto ctx = make_codec_ctx(codec);
    ctx->width = WIDTH;
    ctx->height = HEIGHT;
    ctx->time_base = {1, FPS};
    ctx->framerate = {FPS, 1};
    ctx->pix_fmt = PIX_FMT;
    ctx->gop_size = 12;
    check_ffmpeg(avcodec_open2(ctx.get(), codec, nullptr), "Codec open failed");
    return ctx;
}

FormatCtxPtr init_output(const std::string& filename, AVCodecContext* codec, AVStream*& stream) {
    AVFormatContext* raw = nullptr;
    check_ffmpeg(avformat_alloc_output_context2(&raw, nullptr, nullptr, filename.c_str()),
                 "Output ctx failed");
    auto fmt = make_format_ctx(raw);

    stream = avformat_new_stream(fmt.get(), nullptr);
    if (!stream) throw std::runtime_error("Stream creation failed");
    check_ffmpeg(avcodec_parameters_from_context(stream->codecpar, codec),
                 "Copy codec params failed");
    stream->time_base = codec->time_base;

    if (!(fmt->oformat->flags & AVFMT_NOFILE)) {
        check_ffmpeg(avio_open(&fmt->pb, filename.c_str(), AVIO_FLAG_WRITE),
                     "File open failed");
    }
    check_ffmpeg(avformat_write_header(fmt.get(), nullptr), "Write header failed");
    return fmt;
}

// --- Main ---
int main() {
    try {
        auto codec_ctx = init_codec();
        AVStream* stream = nullptr;
        auto fmt_ctx = init_output("output.mp4", codec_ctx.get(), stream);

        YUVReader reader("input.yuv");
        Encoder encoder(codec_ctx.get());

        while (auto frame = reader.next()) {
            if (auto pkt = encoder.encode(frame)) {
                av_packet_rescale_ts(pkt.get(), codec_ctx->time_base, stream->time_base);
                pkt->stream_index = stream->index;
                check_ffmpeg(av_interleaved_write_frame(fmt_ctx.get(), pkt.get()),
                             "Write frame failed");
            }
        }

        // flush
        for (;;) {
            if (auto pkt = encoder.encode(nullptr)) {
                av_packet_rescale_ts(pkt.get(), codec_ctx->time_base, stream->time_base);
                pkt->stream_index = stream->index;
                check_ffmpeg(av_interleaved_write_frame(fmt_ctx.get(), pkt.get()),
                             "Flush failed");
            } else break;
        }

        check_ffmpeg(av_write_trailer(fmt_ctx.get()), "Write trailer failed");
        std::cout << "Encoding completed." << std::endl;

    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << std::endl;
        return 1;
    }
    return 0;
}
