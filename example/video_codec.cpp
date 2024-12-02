#include <iostream>
#include <fstream>
#include <memory>
#include <string>
#include "pipef.h" // Hypothetical pipeline framework

// Constants
const int WIDTH = 1280;
const int HEIGHT = 720;
const int FPS = 30;

// Step 1: Read YUV File
std::shared_ptr<std::vector<uint8_t>> read_yuv_frame(std::ifstream& yuv_file) {
    const size_t frame_size = WIDTH * HEIGHT * 3 / 2; // YUV420P
    auto frame_data = std::make_shared<std::vector<uint8_t>>(frame_size);

    if (yuv_file.read(reinterpret_cast<char*>(frame_data->data()), frame_size)) {
        return frame_data;
    }
    return nullptr; // End of file or error
}

// Step 2: Encode Frame with H.264
std::shared_ptr<AVPacket> encode_frame(AVCodecContext* codec_ctx, AVFrame* frame) {
    if (avcodec_send_frame(codec_ctx, frame) < 0) {
        std::cerr << "Error sending frame to encoder.\n";
        return nullptr;
    }

    auto packet = std::make_shared<AVPacket>();
    av_init_packet(packet.get());
    packet->data = nullptr;
    packet->size = 0;

    if (avcodec_receive_packet(codec_ctx, packet.get()) == 0) {
        return packet;
    }

    av_packet_unref(packet.get());
    return nullptr;
}

// Step 3: Write Encoded Packet to MP4
void write_packet(AVFormatContext* fmt_ctx, AVPacket* packet, AVStream* video_stream) {
    packet->stream_index = video_stream->index;
    if (av_interleaved_write_frame(fmt_ctx, packet) < 0) {
        std::cerr << "Error writing packet to file.\n";
    }
    av_packet_unref(packet);
}

// Main Function
int main() {
    const char* input_yuv = "input.yuv";
    const char* output_mp4 = "output.mp4";

    // Initialize FFmpeg
    av_register_all();

    // Open YUV file
    std::ifstream yuv_file(input_yuv, std::ios::binary);
    if (!yuv_file.is_open()) {
        std::cerr << "Could not open input YUV file.\n";
        return -1;
    }

    // Set up FFmpeg output
    AVFormatContext* fmt_ctx = nullptr;
    if (avformat_alloc_output_context2(&fmt_ctx, nullptr, nullptr, output_mp4) < 0) {
        std::cerr << "Could not create output context.\n";
        return -1;
    }

    const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!codec) {
        std::cerr << "H.264 codec not found.\n";
        return -1;
    }

    AVCodecContext* codec_ctx = avcodec_alloc_context3(codec);
    codec_ctx->width = WIDTH;
    codec_ctx->height = HEIGHT;
    codec_ctx->time_base = AVRational{1, FPS};
    codec_ctx->framerate = AVRational{FPS, 1};
    codec_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
    codec_ctx->gop_size = 12;

    if (avcodec_open2(codec_ctx, codec, nullptr) < 0) {
        std::cerr << "Could not open codec.\n";
        return -1;
    }

    AVStream* video_stream = avformat_new_stream(fmt_ctx, nullptr);
    avcodec_parameters_from_context(video_stream->codecpar, codec_ctx);

    if (!(fmt_ctx->oformat->flags & AVFMT_NOFILE)) {
        avio_open(&fmt_ctx->pb, output_mp4, AVIO_FLAG_WRITE);
    }

    avformat_write_header(fmt_ctx, nullptr);

    // Create pipeline components
    auto engine = pipef::engine::create();
    auto file_reader = engine->create<source<std::shared_ptr<std::vector<uint8_t>>>>(
        [&]() -> std::shared_ptr<std::vector<uint8_t>> {
            return read_yuv_frame(yuv_file);
        });

    auto encoder = engine->create<transformer<std::shared_ptr<std::vector<uint8_t>>, std::shared_ptr<AVPacket>>>(
        [&](std::shared_ptr<std::vector<uint8_t>> frame_data) -> std::shared_ptr<AVPacket> {
            if (!frame_data) return nullptr;

            AVFrame* frame = av_frame_alloc();
            frame->width = WIDTH;
            frame->height = HEIGHT;
            frame->format = AV_PIX_FMT_YUV420P;
            av_frame_get_buffer(frame, 32);

            frame->data[0] = frame_data->data();
            frame->data[1] = frame_data->data() + WIDTH * HEIGHT;
            frame->data[2] = frame_data->data() + WIDTH * HEIGHT * 5 / 4;

            auto packet = encode_frame(codec_ctx, frame);
            av_frame_free(&frame);
            return packet;
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
    return 0;
}
