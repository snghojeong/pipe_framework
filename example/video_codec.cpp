#include <iostream>
#include <fstream>
#include <memory>
#include <vector>
#include "pipef.h" // Hypothetical pipeline framework

// Constants
const int WIDTH = 1280;
const int HEIGHT = 720;
const int FPS = 30;

// Function to initialize the codec context
AVCodecContext* initialize_codec_context() {
    const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!codec) {
        throw std::runtime_error("H.264 codec not found.");
    }

    AVCodecContext* codec_ctx = avcodec_alloc_context3(codec);
    codec_ctx->width = WIDTH;
    codec_ctx->height = HEIGHT;
    codec_ctx->time_base = {1, FPS};
    codec_ctx->framerate = {FPS, 1};
    codec_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
    codec_ctx->gop_size = 12;

    if (avcodec_open2(codec_ctx, codec, nullptr) < 0) {
        throw std::runtime_error("Failed to open codec.");
    }

    return codec_ctx;
}

// Function to initialize the output format context
AVFormatContext* initialize_output_format(const char* output_mp4, AVCodecContext* codec_ctx, AVStream*& video_stream) {
    AVFormatContext* fmt_ctx = nullptr;
    if (avformat_alloc_output_context2(&fmt_ctx, nullptr, nullptr, output_mp4) < 0) {
        throw std::runtime_error("Could not create output context.");
    }

    video_stream = avformat_new_stream(fmt_ctx, nullptr);
    avcodec_parameters_from_context(video_stream->codecpar, codec_ctx);

    if (!(fmt_ctx->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&fmt_ctx->pb, output_mp4, AVIO_FLAG_WRITE) < 0) {
            throw std::runtime_error("Could not open output file.");
        }
    }

    if (avformat_write_header(fmt_ctx, nullptr) < 0) {
        throw std::runtime_error("Failed to write header.");
    }

    return fmt_ctx;
}

// Function to read a single YUV frame
std::shared_ptr<std::vector<uint8_t>> read_yuv_frame(std::ifstream& yuv_file) {
    const size_t frame_size = WIDTH * HEIGHT * 3 / 2; // YUV420P
    auto frame_data = std::make_shared<std::vector<uint8_t>>(frame_size);

    if (yuv_file.read(reinterpret_cast<char*>(frame_data->data()), frame_size)) {
        return frame_data;
    }
    return nullptr;
}

// Function to encode a YUV frame
std::shared_ptr<AVPacket> encode_frame(AVCodecContext* codec_ctx, std::shared_ptr<std::vector<uint8_t>> frame_data) {
    if (!frame_data) return nullptr;

    AVFrame* frame = av_frame_alloc();
    frame->width = WIDTH;
    frame->height = HEIGHT;
    frame->format = AV_PIX_FMT_YUV420P;
    av_frame_get_buffer(frame, 32);

    frame->data[0] = frame_data->data();
    frame->data[1] = frame_data->data() + WIDTH * HEIGHT;
    frame->data[2] = frame_data->data() + WIDTH * HEIGHT * 5 / 4;

    if (avcodec_send_frame(codec_ctx, frame) < 0) {
        av_frame_free(&frame);
        return nullptr;
    }

    auto packet = std::make_shared<AVPacket>();
    av_init_packet(packet.get());
    packet->data = nullptr;
    packet->size = 0;

    if (avcodec_receive_packet(codec_ctx, packet.get()) == 0) {
        av_frame_free(&frame);
        return packet;
    }

    av_frame_free(&frame);
    return nullptr;
}

// Function to write encoded packets to the MP4 file
void write_packet(AVFormatContext* fmt_ctx, AVPacket* packet, AVStream* video_stream) {
    packet->stream_index = video_stream->index;
    if (av_interleaved_write_frame(fmt_ctx, packet) < 0) {
        std::cerr << "Error writing packet to file.\n";
    }
    av_packet_unref(packet);
}

int main() {
    const char* input_yuv = "input.yuv";
    const char* output_mp4 = "output.mp4";

    // Initialize FFmpeg
    av_register_all();

    try {
        // Open YUV file
        std::ifstream yuv_file(input_yuv, std::ios::binary);
        if (!yuv_file.is_open()) {
            throw std::runtime_error("Could not open input YUV file.");
        }

        // Initialize codec and format contexts
        AVCodecContext* codec_ctx = initialize_codec_context();
        AVStream* video_stream = nullptr;
        AVFormatContext* fmt_ctx = initialize_output_format(output_mp4, codec_ctx, video_stream);

        // Create pipeline components
        auto engine = pipef::engine::create();

        auto file_reader = engine->create<source<std::shared_ptr<std::vector<uint8_t>>>>(
            [&]() -> std::shared_ptr<std::vector<uint8_t>> {
                return read_yuv_frame(yuv_file);
            });

        auto encoder = engine->create<transformer<std::shared_ptr<std::vector<uint8_t>>, std::shared_ptr<AVPacket>>>(
            [&](std::shared_ptr<std::vector<uint8_t>> frame_data) -> std::shared_ptr<AVPacket> {
                return encode_frame(codec_ctx, frame_data);
            });

        auto file_writer = engine->create<sink<std::shared_ptr<AVPacket>>>(
            [&](std::shared_ptr<AVPacket> packet) {
                if (packet) write_packet(fmt_ctx, packet.get(), video_stream);
            });

        // Build and run the pipeline
        file_reader | encoder | file_writer;
        engine->run(INFINITE, 10000);

        // Finalize
        av_write_trailer(fmt_ctx);
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&fmt_ctx);
        avio_closep(&fmt_ctx->pb);

        std::cout << "Encoding completed: " << output_mp4 << std::endl;
    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << std::endl;
    }

    return 0;
}
