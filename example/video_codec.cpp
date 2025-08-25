#include <iostream>
#include <fstream>
#include <memory>
#include <vector>
#include <stdexcept>
#include <string>
#include <queue>
#include <mutex>

// Assuming pipef.h exists and works as a pipeline framework
#include "pipef.h"

// --- FFmpeg Headers ---
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
#include <libavutil/error.h>
}

// --- Constants ---
const int WIDTH = 1280;
const int HEIGHT = 720;
const int FPS = 30;
const AVPixelFormat PIX_FMT = AV_PIX_FMT_YUV420P;

// --- Custom Smart Pointer Deleters for FFmpeg objects ---
// These ensure resources are automatically freed when they go out of scope.
struct AVCodecContextDeleter {
    void operator()(AVCodecContext* ctx) const {
        if (ctx) avcodec_free_context(&ctx);
    }
};

struct AVFormatContextDeleter {
    void operator()(AVFormatContext* fmt_ctx) const {
        if (fmt_ctx) {
            if (fmt_ctx->pb) avio_closep(&fmt_ctx->pb);
            avformat_free_context(fmt_ctx);
        }
    }
};

struct AVPacketDeleter {
    void operator()(AVPacket* pkt) const {
        if (pkt) av_packet_free(&pkt);
    }
};

struct AVFrameDeleter {
    void operator()(AVFrame* frame) const {
        if (frame) av_frame_free(&frame);
    }
};

using UniqueAVCodecContext = std::unique_ptr<AVCodecContext, AVCodecContextDeleter>;
using UniqueAVFormatContext = std::unique_ptr<AVFormatContext, AVFormatContextDeleter>;
using UniqueAVPacket = std::unique_ptr<AVPacket, AVPacketDeleter>;
using UniqueAVFrame = std::unique_ptr<AVFrame, AVFrameDeleter>;

// --- Helper function for FFmpeg error handling ---
void check_ffmpeg_error(int result, const std::string& message) {
    if (result < 0 && result != AVERROR(EAGAIN) && result != AVERROR_EOF) {
        char err_buf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(result, err_buf, sizeof(err_buf));
        throw std::runtime_error(message + ": " + err_buf);
    }
}

// --- Frame Reader and Encoder Functors ---

// Encapsulates the state for reading YUV frames.
class YUVFrameReader {
public:
    YUVFrameReader(const std::string& filename) : frame_size(WIDTH * HEIGHT * 3 / 2) {
        yuv_file.open(filename, std::ios::binary);
        if (!yuv_file.is_open()) {
            throw std::runtime_error("Could not open input YUV file: " + filename);
        }
    }

    UniqueAVFrame operator()() {
        UniqueAVFrame frame(av_frame_alloc());
        if (!frame) return nullptr;

        frame->width = WIDTH;
        frame->height = HEIGHT;
        frame->format = PIX_FMT;
        check_ffmpeg_error(av_frame_get_buffer(frame.get(), 32), "Failed to allocate frame buffer");

        if (yuv_file.read(reinterpret_cast<char*>(frame->data[0]), frame_size)) {
            // Correctly set stride and data pointers for YUV420P
            frame->linesize[0] = WIDTH;
            frame->linesize[1] = WIDTH / 2;
            frame->linesize[2] = WIDTH / 2;

            frame->data[1] = frame->data[0] + frame->linesize[0] * HEIGHT;
            frame->data[2] = frame->data[1] + frame->linesize[1] * HEIGHT / 2;
            
            return frame;
        }
        return nullptr; // End of file
    }

private:
    std::ifstream yuv_file;
    const size_t frame_size;
};


// Encapsulates the state for encoding frames.
class VideoEncoder {
public:
    VideoEncoder(AVCodecContext* ctx) : codec_ctx(ctx), frame_counter(0) {}

    UniqueAVPacket operator()(UniqueAVFrame frame) {
        if (frame) {
            frame->pts = frame_counter++;
            check_ffmpeg_error(avcodec_send_frame(codec_ctx, frame.get()), "Failed to send frame to encoder");
        } else { // Flush the encoder
            check_ffmpeg_error(avcodec_send_frame(codec_ctx, nullptr), "Failed to send flush command");
        }

        UniqueAVPacket packet(av_packet_alloc());
        if (!packet) throw std::runtime_error("Failed to allocate AVPacket.");

        int ret = avcodec_receive_packet(codec_ctx, packet.get());
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            return nullptr;
        }
        check_ffmpeg_error(ret, "Error receiving packet from encoder");
        
        return packet;
    }

private:
    AVCodecContext* codec_ctx;
    int frame_counter;
};

// --- Initialization Functions ---
UniqueAVCodecContext initialize_codec_context() {
    const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!codec) {
        throw std::runtime_error("H.264 codec not found.");
    }

    UniqueAVCodecContext codec_ctx(avcodec_alloc_context3(codec));
    if (!codec_ctx) {
        throw std::runtime_error("Failed to allocate codec context.");
    }

    codec_ctx->width = WIDTH;
    codec_ctx->height = HEIGHT;
    codec_ctx->time_base = {1, FPS};
    codec_ctx->framerate = {FPS, 1};
    codec_ctx->pix_fmt = PIX_FMT;
    codec_ctx->gop_size = 12;

    check_ffmpeg_error(avcodec_open2(codec_ctx.get(), codec, nullptr), "Failed to open codec");
    return codec_ctx;
}

UniqueAVFormatContext initialize_output_format(const char* output_mp4, AVCodecContext* codec_ctx, AVStream*& video_stream) {
    AVFormatContext* fmt_ctx_raw = nullptr;
    check_ffmpeg_error(avformat_alloc_output_context2(&fmt_ctx_raw, nullptr, nullptr, output_mp4), "Could not create output context");
    UniqueAVFormatContext fmt_ctx(fmt_ctx_raw);

    video_stream = avformat_new_stream(fmt_ctx.get(), nullptr);
    if (!video_stream) {
        throw std::runtime_error("Failed to create video stream.");
    }

    check_ffmpeg_error(avcodec_parameters_from_context(video_stream->codecpar, codec_ctx), "Failed to copy codec parameters");
    video_stream->time_base = codec_ctx->time_base;
    
    if (!(fmt_ctx->oformat->flags & AVFMT_NOFILE)) {
        check_ffmpeg_error(avio_open(&fmt_ctx->pb, output_mp4, AVIO_FLAG_WRITE), "Could not open output file");
    }

    check_ffmpeg_error(avformat_write_header(fmt_ctx.get(), nullptr), "Failed to write header");
    return fmt_ctx;
}

// --- Main function ---
int main() {
    const char* input_yuv = "input.yuv";
    const char* output_mp4 = "output.mp4";

    try {
        UniqueAVCodecContext codec_ctx = initialize_codec_context();
        AVStream* video_stream = nullptr;
        UniqueAVFormatContext fmt_ctx = initialize_output_format(output_mp4, codec_ctx.get(), video_stream);

        auto engine = pipef::engine::create();

        auto file_reader = engine->create<source<UniqueAVFrame>>(YUVFrameReader(input_yuv));
        auto encoder = engine->create<transformer<UniqueAVFrame, UniqueAVPacket>>(VideoEncoder(codec_ctx.get()));
        auto file_writer = engine->create<sink<UniqueAVPacket>>(
            [&](UniqueAVPacket packet) {
                if (packet) {
                    av_packet_rescale_ts(packet.get(), codec_ctx->time_base, video_stream->time_base);
                    packet->stream_index = video_stream->index;
                    check_ffmpeg_error(av_interleaved_write_frame(fmt_ctx.get(), packet.get()), "Error writing packet to file");
                }
            });

        file_reader | encoder | file_writer;
        engine->run(INFINITE, 10000);

        // Flush the encoder to get any delayed frames
        VideoEncoder encoder_flusher(codec_ctx.get());
        UniqueAVPacket packet;
        while ((packet = encoder_flusher(nullptr))) {
            av_packet_rescale_ts(packet.get(), codec_ctx->time_base, video_stream->time_base);
            packet->stream_index = video_stream->index;
            check_ffmpeg_error(av_interleaved_write_frame(fmt_ctx.get(), packet.get()), "Error writing flushed packet");
        }

        check_ffmpeg_error(av_write_trailer(fmt_ctx.get()), "Failed to write trailer");

        std::cout << "Encoding completed: " << output_mp4 << std::endl;
    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << std::endl;
        return 1;
    }
    return 0;
}
