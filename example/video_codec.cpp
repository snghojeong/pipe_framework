#include <iostream>
#include <fstream>
#include <memory>
#include <string>
#include <stdexcept>
#include <functional>
#include <vector>

// --- FFmpeg Headers ---
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/error.h>
}

// --- Constants ---
constexpr int WIDTH = 1280;
constexpr int HEIGHT = 720;
constexpr int FPS = 30;
constexpr AVPixelFormat PIX_FMT = AV_PIX_FMT_YUV420P;

// =================================================================================================
// ## 1. Utilities (RAII Wrappers & Error Handling)
//
// We define smart pointers for FFmpeg types and a single error-checking function.
// This ensures resources are managed automatically and errors are handled consistently.
// =================================================================================================

// --- Custom Deleters for FFmpeg types ---
struct AVFormatContextDeleter {
    void operator()(AVFormatContext* ctx) const {
        if (ctx) {
            // Close the output file if it was opened
            if (!(ctx->oformat->flags & AVFMT_NOFILE) && ctx->pb) {
                avio_closep(&ctx->pb);
            }
            avformat_free_context(ctx);
        }
    }
};

// --- RAII Smart Pointer Aliases ---
using CodecCtxPtr  = std::unique_ptr<AVCodecContext, decltype(&avcodec_free_context)>;
using FormatCtxPtr = std::unique_ptr<AVFormatContext, AVFormatContextDeleter>;
using FramePtr     = std::unique_ptr<AVFrame, decltype(&av_frame_free)>;
using PacketPtr    = std::unique_ptr<AVPacket, decltype(&av_packet_free)>;

// --- Error Handling Helper ---
inline void check_ffmpeg(int ret, const std::string& msg) {
    if (ret < 0) {
        std::vector<char> buf(AV_ERROR_MAX_STRING_SIZE);
        av_strerror(ret, buf.data(), buf.size());
        throw std::runtime_error(msg + ": " + buf.data());
    }
}

// =================================================================================================
// ## 2. YUV File Reader
//
// This class remains largely the same. It's a simple utility to read raw YUV frames from a file.
// NOTE: This implementation assumes the input .yuv file has a packed, non-padded format that
// matches the memory layout allocated by av_frame_get_buffer.
// =================================================================================================
class YUVReader {
public:
    explicit YUVReader(const std::string& filename)
        : file_(filename, std::ios::binary),
          frame_size_(WIDTH * HEIGHT * 3 / 2) {
        if (!file_) {
            throw std::runtime_error("Failed to open input file: " + filename);
        }
    }

    FramePtr nextFrame() {
        auto frame = FramePtr(av_frame_alloc(), av_frame_free);
        frame->width = WIDTH;
        frame->height = HEIGHT;
        frame->format = PIX_FMT;
        check_ffmpeg(av_frame_get_buffer(frame.get(), 0), "Failed to allocate frame buffer");

        std::vector<char> buffer(frame_size_);
        file_.read(buffer.data(), frame_size_);
        if (file_.gcount() < frame_size_) {
            return nullptr; // End of file
        }

        // Copy the raw YUV data into the frame's planes
        av_image_copy_to_buffer(
            frame->data[0], frame->linesize[0] * HEIGHT * 3/2,
            (const uint8_t* const*) &buffer[0], & (int&)(frame_size_),
            PIX_FMT, WIDTH, HEIGHT, 1
        );


        // Manually set plane pointers for the contiguous buffer
        // This is correct for the packed YUV420p format.
        const int y_size = WIDTH * HEIGHT;
        const int uv_size = WIDTH * HEIGHT / 4;
        frame->data[1] = frame->data[0] + y_size;
        frame->data[2] = frame->data[1] + uv_size;

        return frame;
    }

private:
    std::ifstream file_;
    const size_t frame_size_;
};

// =================================================================================================
// ## 3. Video Encoder Class
//
// This new class encapsulates all FFmpeg-related state and operations.
// The constructor handles all initialization, and the destructor handles cleanup via RAII.
// =================================================================================================
class VideoEncoder {
public:
    VideoEncoder(const std::string& filename, int width, int height, int fps)
        : next_pts_(0)
        , codec_ctx_(nullptr, avcodec_free_context)
    {
        // --- 1. Find Codec and Create Context ---
        const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_H264);
        if (!codec) throw std::runtime_error("H.264 encoder not found");
        codec_ctx_.reset(avcodec_alloc_context3(codec));
        if (!codec_ctx_) throw std::runtime_error("Could not allocate codec context");

        // --- 2. Configure Codec Context ---
        codec_ctx_->width = width;
        codec_ctx_->height = height;
        codec_ctx_->pix_fmt = PIX_FMT;
        codec_ctx_->time_base = {1, fps};
        codec_ctx_->framerate = {fps, 1};
        codec_ctx_->gop_size = 12; // Optional: I-frame interval
        av_opt_set(codec_ctx_->priv_data, "preset", "slow", 0); // Optional: Encoding speed
        check_ffmpeg(avcodec_open2(codec_ctx_.get(), codec, nullptr), "Could not open codec");

        // --- 3. Create Format Context and Stream ---
        AVFormatContext* raw_fmt_ctx = nullptr;
        check_ffmpeg(avformat_alloc_output_context2(&raw_fmt_ctx, nullptr, nullptr, filename.c_str()), "Could not create output context");
        format_ctx_.reset(raw_fmt_ctx);

        stream_ = avformat_new_stream(format_ctx_.get(), nullptr);
        if (!stream_) throw std::runtime_error("Could not create new stream");
        stream_->id = format_ctx_->nb_streams - 1;
        stream_->time_base = codec_ctx_->time_base;
        check_ffmpeg(avcodec_parameters_from_context(stream_->codecpar, codec_ctx_.get()), "Could not copy codec parameters");

        // --- 4. Open Output File and Write Header ---
        if (!(format_ctx_->oformat->flags & AVFMT_NOFILE)) {
            check_ffmpeg(avio_open(&format_ctx_->pb, filename.c_str(), AVIO_FLAG_WRITE), "Could not open output file");
        }
        check_ffmpeg(avformat_write_header(format_ctx_.get(), nullptr), "Could not write header");
    }

    void encodeFrame(AVFrame* frame) {
        if (frame) {
            frame->pts = next_pts_++;
        }
        // Send the frame to the encoder
        int ret = avcodec_send_frame(codec_ctx_.get(), frame);
        check_ffmpeg(ret, "Failed to send frame to encoder");

        // Receive and write all available packets
        while (ret >= 0) {
            auto packet = PacketPtr(av_packet_alloc(), av_packet_free);
            ret = avcodec_receive_packet(codec_ctx_.get(), packet.get());
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                break; // Need more input or encoding is finished
            }
            check_ffmpeg(ret, "Failed to receive packet from encoder");

            writePacket(packet.get());
        }
    }

    void finalize() {
        // Flush the encoder by sending a null frame
        encodeFrame(nullptr);
        // Write the stream trailer
        check_ffmpeg(av_write_trailer(format_ctx_.get()), "Failed to write trailer");
        std::cout << "Encoding completed successfully." << std::endl;
    }

private:
    void writePacket(AVPacket* packet) {
        av_packet_rescale_ts(packet, codec_ctx_->time_base, stream_->time_base);
        packet->stream_index = stream_->index;
        check_ffmpeg(av_interleaved_write_frame(format_ctx_.get(), packet), "Failed to write packet");
    }

    FormatCtxPtr format_ctx_;
    CodecCtxPtr codec_ctx_;
    AVStream* stream_ = nullptr; // Raw pointer, owned by format_ctx_
    int64_t next_pts_;
};

// =================================================================================================
// ## 4. Main Function
//
// The main function is now simple and clean. It clearly expresses the program's purpose:
// 1. Create a reader and an encoder.
// 2. Loop through frames, encoding each one.
// 3. Finalize the video file.
// =================================================================================================
int main() {
    try {
        YUVReader reader("input.yuv");
        VideoEncoder encoder("output.mp4", WIDTH, HEIGHT, FPS);

        while (auto frame = reader.nextFrame()) {
            encoder.encodeFrame(frame.get());
        }

        encoder.finalize();

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
