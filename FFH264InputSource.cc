#include "FFH264InputSource.hh"
#include "easyloggingpp/easylogging++.h"
#include "FFUniqueWrapper.hh"
#include "FFGlobal.hh"

#include <future>
#include <chrono>
#include <thread>
#include <iostream>

extern "C" {
#include <libavutil/pixdesc.h>
}

namespace LiveRTSP {
static int decode_packet(AVCodecContext *dec, const AVPacket *pkt, AVFrame *fram);

FFH264InputSource *FFH264InputSource::CreateNew(UsageEnvironment &env, const ParamTypeKeyValMap &video_tkv) {
    FFH264InputSource *self = new FFH264InputSource(env);
    if (!self->initialize(video_tkv)) {
        delete self;
        return nullptr;
    }

    return self;
}

FFH264InputSource::FFH264InputSource(UsageEnvironment &env) :
        LiveMediaInputSource(env), width(480), height(360), framerate(15),
        device("/dev/video0"), started(false), decoding(false), encoding(false) {
    //LOG(DEBUG) << "+FFH264InputSource";
    FFGlobal::Init();
}

FFH264InputSource::~FFH264InputSource() {
    //LOG(DEBUG) << "~FFH264InputSource";
}

bool FFH264InputSource::initialize(const ParamTypeKeyValMap &tkv) {
    const std::map<std::string, std::string> &parameters = tkv.second;

    if (parameters.count("height")) height = std::stoi(parameters.at("height"));
    if (parameters.count("width")) width = std::stoi(parameters.at("width"));

    LOG(INFO) << "width:"<< width << " height:" << height;
    return true;
}

void FFH264InputSource::decodingTask(AVFormatContext *fmtctx, AVCodecContext *decctx) {
    LOG(INFO) << "bringup decoding task";
    int ret = 0;
    AVPacket *pkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();

    while (decoding) {
        ret = av_read_frame(fmtctx, pkt);
        if (ret < 0) {
            if (ret == AVERROR(EAGAIN)) {
                sched_yield();
                std::this_thread::sleep_for(std::chrono::milliseconds(8));
                continue;
            }

            LOG(ERROR) << "av_read_frame fail:" << ret;
            break;
        }

        ret = decode_packet(decctx, pkt, frame);
        av_packet_unref(pkt);
        if (ret < 0) {
            LOG(ERROR) << "decode_packet fail:" << ret;
            break;
        }
    }

    // flush
    decode_packet(decctx, NULL, frame);

    av_frame_free(&frame);
    av_packet_free(&pkt);
    avcodec_free_context(&decctx);
    avformat_close_input(&fmtctx);

    // TODO: waiting recyle
    if (decoding) {
        do {
            sched_yield();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        } while (decoding);
    }
    LOG(INFO) << "teardown decoding task";
}

void FFH264InputSource::encodingTask() {
    LOG(INFO) << "bringup encoding task";
    while (encoding) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    LOG(INFO) << "teardown encoding task";
}

static int open_codec_context(int *stream_idx, AVCodecContext **dec_ctx, AVFormatContext *fmt_ctx, enum AVMediaType type) {
    int ret, stream_index;
    AVStream *st;
    const AVCodec *dec = NULL;

    ret = av_find_best_stream(fmt_ctx, type, -1, -1, NULL, 0);
    if (ret < 0) {
        fprintf(stderr, "Could not find %s stream\n", av_get_media_type_string(type));
        return ret;
    }

    stream_index = ret;
    st = fmt_ctx->streams[stream_index];

    /* find decoder for the stream */
    dec = avcodec_find_decoder(st->codecpar->codec_id);
    if (!dec) {
        fprintf(stderr, "Failed to find %s codec\n", av_get_media_type_string(type));
        return AVERROR(EINVAL);
    }

    /* Allocate a codec context for the decoder */
    *dec_ctx = avcodec_alloc_context3(dec);
    if (!*dec_ctx) {
        fprintf(stderr, "Failed to allocate the %s codec context\n", av_get_media_type_string(type));
        return AVERROR(ENOMEM);
    }

    /* Copy codec parameters from input stream to output codec context */
    if ((ret = avcodec_parameters_to_context(*dec_ctx, st->codecpar)) < 0) {
        fprintf(stderr, "Failed to copy %s codec parameters to decoder context\n", av_get_media_type_string(type));
        return ret;
    }

    /* Init the decoders */
    if ((ret = avcodec_open2(*dec_ctx, dec, NULL)) < 0) {
        fprintf(stderr, "Failed to open %s codec\n", av_get_media_type_string(type));
        return ret;
    }
    *stream_idx = stream_index;

    return 0;
}

static int decode_packet(AVCodecContext *dec, const AVPacket *pkt, AVFrame *frame)
{
    int ret = 0;
    char estring[AV_ERROR_MAX_STRING_SIZE] = { 0 };

    // submit the packet to the decoder
    ret = avcodec_send_packet(dec, pkt);
    if (ret < 0) {
        fprintf(stderr, "Error submitting a packet for decoding (%s)\n", av_make_error_string(estring, AV_ERROR_MAX_STRING_SIZE, ret));
        return ret;
    }

    // get all the available frames from the decoder
    while (ret >= 0) {
        ret = avcodec_receive_frame(dec, frame);
        if (ret < 0) {
            // those two return values are special and mean there is no output
            // frame available, but there were no errors during decoding
            if (ret == AVERROR_EOF || ret == AVERROR(EAGAIN))
                return 0;

            fprintf(stderr, "Error during decoding (%s)\n", av_make_error_string(estring, AV_ERROR_MAX_STRING_SIZE, ret));
            return ret;
        }

        // write the frame data to output file
        //ret = output_video_frame(frame);

        LOG(INFO) << "decoded frame coded_n:" << frame->coded_picture_number;
        av_frame_unref(frame);
        if (ret < 0) return ret;
    }

    return 0;
}

// XXX: std::async
//      avfilter transcoding rawvideo to h264
bool FFH264InputSource::startCapture() {
    LOG(INFO) << "capture starting";
    int ret = 0;

    //av_log_set_level(AV_LOG_DEBUG);
    AVInputFormat *ifmt = av_find_input_format("video4linux2");
    if (ifmt == NULL) {
        LOG(ERROR) << "AVInputFormat: not support video4linux2";
        return false;
    }

    AVFormatContext *fmtctx = avformat_alloc_context();
    if (!fmtctx) {
        LOG(ERROR) << "avformat_alloc_context fail";
        return false;
    }
    fmtctx->flags |= AVFMT_FLAG_NONBLOCK;

    AVDictionary *opts = NULL;
    std::string video_size = std::to_string(width) + "x" + std::to_string(height);
    av_dict_set(&opts, "video_size", video_size.c_str(), 0);
    std::string fr = std::to_string(framerate);
    av_dict_set(&opts, "framerate", fr.c_str(), 0);
    ret = avformat_open_input(&fmtctx, device.c_str(), ifmt, &opts);
    av_dict_free(&opts);
    if (ret != 0) {
        LOG(ERROR) << "avformat_open_input fail:" << ret;
        avformat_free_context(fmtctx);
        return false;
    }

    int stream_idx = -1;
    AVCodecContext *decctx = NULL;
    ret = open_codec_context(&stream_idx, &decctx, fmtctx, AVMEDIA_TYPE_VIDEO);
    if (ret != 0) {
        LOG(ERROR) << "open_codec_context fail:" << ret;
        avformat_close_input(&fmtctx);
        return false;
    }

    av_dump_format(fmtctx, 0, device.c_str(), 0);

    decoding = true;
    decodingThread = std::thread(&FFH264InputSource::decodingTask, this, fmtctx, decctx);

    encoding = true;
    encodingThread = std::thread(&FFH264InputSource::encodingTask, this);
    return true;
}

void FFH264InputSource::stopCapture() {
    LOG(INFO) << "capture stopping";
    decoding = false;
    decodingThread.join();

    encoding = false;
    encodingThread.join();

    LOG(INFO) << "capture stopped";
}

void FFH264InputSource::doStopGettingFrames() {
    if (started) {
        stopCapture();
        started = false;
    }
}

void FFH264InputSource::doGetNextFrame() {
    if (!started && !startCapture()) {
        handleClosure();
        return;
    }

    if (!started) started = true;
}
}
