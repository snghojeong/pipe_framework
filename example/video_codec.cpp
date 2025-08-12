#include <iostream>
#include <fstream>
#include <memory>
#include <vector>
#include <stdexcept>
#include <string>

// Assuming pipef.h exists and works as a pipeline framework
#include "pipef.h" 

// --- FFmpeg Headers ---
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

// --- Constants ---
const int WIDTH = 1280;
const int HEIGHT = 720;
const int FPS = 30;

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
    if (result < 0) {
        char err_buf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(result, err_buf, sizeof(err_buf));
        throw std::runtime_error(message + ": " + err_buf);
    }
}

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
    codec_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
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

    if (!(fmt_ctx->oformat->flags & AVFMT_NOFILE)) {
        check_ffmpeg_error(avio_open(&fmt_ctx->pb, output_mp4, AVIO_FLAG_WRITE), "Could not open output file");
    }

    check_ffmpeg_error(avformat_write_header(fmt_ctx.get(), nullptr), "Failed to write header");

    return fmt_ctx;
}

// --- Pipeline Component Logic ---
std::shared_ptr<std::vector<uint8_t>> read_yuv_frame(std::ifstream& yuv_file) {
    const size_t frame_size = WIDTH * HEIGHT * 3 / 2;
    auto frame_data = std::make_shared<std::vector<uint8_t>>(frame_size);
    if (yuv_file.read(reinterpret_cast<char*>(frame_data->data()), frame_size)) {
        return frame_data;
    }
    return nullptr;
}

UniqueAVPacket encode_frame(AVCodecContext* codec_ctx, std::shared_ptr<std::vector<uint8_t>> frame_data, int frame_index) {
    if (!frame_data) return nullptr;

    UniqueAVFrame frame(av_frame_alloc());
    if (!frame) throw std::runtime_error("Failed to allocate AVFrame.");
    
    frame->width = WIDTH;
    frame->height = HEIGHT;
    frame->format = AV_PIX_FMT_YUV420P;
    frame->pts = frame_index;

    check_ffmpeg_error(av_frame_get_buffer(frame.get(), 32), "Failed to allocate frame buffer");

    // Copy data from the input vector to the AVFrame
    frame->data[0] = frame_data->data();
    frame->data[1] = frame_data->data() + WIDTH * HEIGHT;
    frame->data[2] = frame_data->data() + WIDTH * HEIGHT * 5 / 4;

    check_ffmpeg_error(avcodec_send_frame(codec_ctx, frame.get()), "Failed to send frame to encoder");

    UniqueAVPacket packet(av_packet_alloc());
    if (!packet) throw std::runtime_error("Failed to allocate AVPacket.");

    if (avcodec_receive_packet(codec_ctx, packet.get()) == 0) {
        return packet;
    }
    return nullptr;
}

void write_packet(AVFormatContext* fmt_ctx, AVPacket* packet, AVStream* video_stream) {
    av_packet_rescale_ts(packet, {1, FPS}, video_stream->time_base);
    packet->stream_index = video_stream->index;
    check_ffmpeg_error(av_interleaved_write_frame(fmt_ctx, packet), "Error writing packet to file");
}

int main() {
    const char* input_yuv = "input.yuv";
    const char* output_mp4 = "output.mp4";
    int frame_counter = 0;

    try {
        std::ifstream yuv_file(input_yuv, std::ios::binary);
        if (!yuv_file.is_open()) {
            throw std::runtime_error("Could not open input YUV file.");
        }

        UniqueAVCodecContext codec_ctx = initialize_codec_context();
        AVStream* video_stream = nullptr;
        UniqueAVFormatContext fmt_ctx = initialize_output_format(output_mp4, codec_ctx.get(), video_stream);

        auto engine = pipef::engine::create();

        auto file_reader = engine->create<source<std::shared_ptr<std::vector<uint8_t>>>>(
            [&]() -> std::shared_ptr<std::vector<uint8_t>> {
                return read_yuv_frame(yuv_file);
            });

        auto encoder = engine->create<transformer<std::shared_ptr<std::vector<uint8_t>>, UniqueAVPacket>>(
            [&](std::shared_ptr<std::vector<uint8_t>> frame_data) -> UniqueAVPacket {
                return encode_frame(codec_ctx.get(), frame_data, frame_counter++);
            });

        auto file_writer = engine->create<sink<UniqueAVPacket>>(
            [&](UniqueAVPacket packet) {
                if (packet) write_packet(fmt_ctx.get(), packet.get(), video_stream);
            });

        file_reader | encoder | file_writer;
        engine->run(INFINITE, 10000);

        // Flush the encoder to get any delayed frames
        UniqueAVPacket packet;
        while ((packet = encode_frame(codec_ctx.get(), nullptr, frame_counter))) {
            write_packet(fmt_ctx.get(), packet.get(), video_stream);
        }

        check_ffmpeg_error(av_write_trailer(fmt_ctx.get()), "Failed to write trailer");

        std::cout << "Encoding completed: " << output_mp4 << std::endl;
    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << std::endl;
        return 1;
    }
    return 0;
}
