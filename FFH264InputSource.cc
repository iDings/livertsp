#include "FFH264InputSource.hh"
#include "easyloggingpp/easylogging++.h"
#include "FFUniqueWrapper.hh"
#include "FFHelper.hh"
#include "Base64.hh"

#include <future>
#include <chrono>
#include <thread>
#include <iostream>
#include <sys/stat.h>
#include <cassert>
#include <cstdint>
#include <iomanip>
#include <sys/prctl.h>

extern "C" {
#include <libavutil/pixdesc.h>
#include <libavutil/imgutils.h>
}

#ifndef NSEC_PER_SEC
#define NSEC_PER_SEC 1000000000L
#endif

#define USEC_PER_SEC 1000000L

#ifndef NSEC_PER_MSEC
#define NSEC_PER_MSEC 1000000L
#endif

namespace LiveRTSP {
const char H264marker[] = {0,0,0,1};
const char H264shortmarker[] = {0,0,1};

const int FFH264InputSource::kEncoderFramerate = 30;

FFH264InputSource *FFH264InputSource::CreateNew(UsageEnvironment &env, const ParamTypeKeyValMap &video_tkv) {
    FFH264InputSource *self = new FFH264InputSource(env);
    if (!self->initialize(video_tkv)) {
        delete self;
        return nullptr;
    }

    return self;
}

FFH264InputSource::FFH264InputSource(UsageEnvironment &env) :
        LiveMediaInputSource(env),
        width(480),
        height(360),
        framerate(kEncoderFramerate),
        dumpfile(false),
        pgm(false),
        device("/dev/video0"),
        started(false),
        decoding(false),
        encoding(false),
        last_tv({0, 0}),
        last_pts(0),
        next_pts(0),
        m_repeatConfig(false),
        m_keepMarker(false)
{
    //LOG(DEBUG) << "+FFH264InputSource";
    FFHelper::Init();
    selfDestructTriggerId = envir().taskScheduler().createEventTrigger(FFH264InputSource::selfDestructTriggerHandler);
    frameNotifyTriggerId = envir().taskScheduler().createEventTrigger(FFH264InputSource::frameNotifyTriggerHandler);
}

FFH264InputSource::~FFH264InputSource() {
    //LOG(DEBUG) << "~FFH264InputSource";
    envir().taskScheduler().deleteEventTrigger(selfDestructTriggerId);
    envir().taskScheduler().deleteEventTrigger(frameNotifyTriggerId);
}

// trigger close to cleanup if something errors
void FFH264InputSource::selfDestructTriggerHandler(void *udata) {
    FFH264InputSource *thiz = (FFH264InputSource *)udata;
    thiz->handleClosure();
    return;
}

void FFH264InputSource::frameNotifyTriggerHandler(void *udata) {
    FFH264InputSource *thiz = (FFH264InputSource *)udata;
    if (thiz->isCurrentlyAwaitingData()) thiz->doGetNextFrame();
    return;
}

bool FFH264InputSource::initialize(const ParamTypeKeyValMap &tkv) {
    const std::map<std::string, std::string> &parameters = tkv.second;

    if (parameters.count("height")) height = std::stoi(parameters.at("height"));
    if (parameters.count("width")) width = std::stoi(parameters.at("width"));
    if (parameters.count("dumpfile") && (parameters.at("dumpfile") != "none")) {
        dumpfile = true;
        std::string mine = parameters.at("dumpfile");
        if (mine == "pgm") pgm = true;
    }

    LOG(INFO) << "width:"<< width << " height:" << height;
    return true;
}

// seperate reading + decoding, just rawvideo, decoding seems fine
void FFH264InputSource::decodingTask(AVFormatContext *fmtctx, AVCodecContext *decctx, int stream_idx, SwsContext *sws_ctx) {
    LOG(INFO) << "bringup decoding task";
    prctl(PR_SET_NAME, "decoding");

    int ret = 0;
    AVStream *video_st = fmtctx->streams[stream_idx];

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

        ret = decodePacket(video_st, decctx, pkt, frame, sws_ctx);
        av_packet_unref(pkt);
        if (ret < 0) {
            LOG(ERROR) << "decodePacket fail:" << ret;
            break;
        }
    }

    av_frame_free(&frame);
    av_packet_free(&pkt);
    sws_freeContext(sws_ctx);
    avcodec_free_context(&decctx);
    avformat_close_input(&fmtctx);

    // waiting recyle
    if (decoding) {
        envir().taskScheduler().triggerEvent(selfDestructTriggerId, this);

        do {
            sched_yield();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        } while (decoding);
    }
    LOG(INFO) << "teardown decoding task";
}

int FFH264InputSource::encodePacket(AVCodecContext *c, const AVFrame *frame, AVPacket *pkt) {
    int ret = 0;
    char estring[AV_ERROR_MAX_STRING_SIZE] = { 0 };
    bool retry = false;

    do {
        ret = avcodec_send_frame(c, frame);
        if (ret == AVERROR(EAGAIN)) {
            if (retry) {
                LOG(INFO) << "already retry, but still EAGAIN";
                return AVERROR_BUG;
            }

            retry = true;
            ret = 0;
        } else if (ret < 0) {
            LOG(ERROR) << "Error submitting a packet for decoding " << av_make_error_string(estring, AV_ERROR_MAX_STRING_SIZE, ret);
            return ret;
        }

        while (ret >= 0) {
            ret = avcodec_receive_packet(c, pkt);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                break;

            if (ret < 0) {
                LOG(ERROR) << "Error during encoding " << av_make_error_string(estring, AV_ERROR_MAX_STRING_SIZE, ret);
                return ret;
            }

            if (dumpfile) {
                char filename[1024]{0};
                snprintf(filename, sizeof(filename), "dump_%s/encoding.h264", sTimestamp.c_str());
                FILE *fp = fopen(filename, "a+");
                if (fp) {
                    fwrite(pkt->data, 1, pkt->size, fp);
                    fclose(fp);
                }
            }

            //fpsStat("encoding", pkt->size);
            //LOG(INFO) << "Write packet " << pkt->pts << " size=" << pkt->size;
            {
                std::lock_guard<std::mutex> lock(encodedPackets_lock);
                encodedPackets.emplace_back(pkt);
            }

            envir().taskScheduler().triggerEvent(frameNotifyTriggerId, this);
            //fwrite(pkt->data, 1, pkt->size, outfile);
            av_packet_unref(pkt);
        }
    } while (retry);

    return 0;
}

void FFH264InputSource::encodingTask(AVCodecContext *c) {
    LOG(INFO) << "bringup encoding task";
    prctl(PR_SET_NAME, "encoding");

    FFFrame frame;
    bool got_frame = false;
    AVPacket *pkt = av_packet_alloc();
    int ret = 0;

    while (encoding) {
        {
            got_frame = false;
            std::unique_lock<std::mutex> ul(decodedFrames_lock);
            decodedFrames_cond.wait_for(ul,
                    std::chrono::microseconds(1000), [this] {
                        return !decodedFrames.empty();
                    });
            if (!decodedFrames.empty()) {
                got_frame = true;
                frame = std::move(decodedFrames.front());
                decodedFrames.pop_front();
            }
        }
        if (!got_frame) continue;

        if (dumpfile) {
            char filename[1024]{0};
            uint8_t *dst_data[4] = {NULL};
            int dst_linesize[4];
            snprintf(filename, sizeof(filename), "dump_%s/encoding_%dx%d.%s",
                    sTimestamp.c_str(), frame.frame->width, frame.frame->height,
                    av_get_pix_fmt_name((enum AVPixelFormat)frame.frame->format));
            enum AVPixelFormat pix_fmt = static_cast<enum AVPixelFormat>(frame.frame->format);
            int bufsize = av_image_alloc(dst_data, dst_linesize, frame.frame->width, frame.frame->height, pix_fmt, 1);
            if (bufsize >= 0) {
                av_image_copy(dst_data, dst_linesize, (const uint8_t **)(frame.frame->data),
                        frame.frame->linesize, static_cast<enum AVPixelFormat>(frame.frame->format),
                        frame.frame->width, frame.frame->height);
                FILE *fp = fopen(filename, "a+");
                if (fp) {
                    fwrite(dst_data[0], 1, bufsize, fp);
                    fclose(fp);
                }
                av_free(dst_data[0]);
            }
        }

        //LOG(INFO) << "frame pts:" << frame.frame->pts << " pts:" << frame.pts;
        // pts need liner
        // doc/example/muxing.c
        frame.frame->pts = next_pts++;
        ret = encodePacket(c, frame.frame, pkt);
        if (ret < 0) {
            LOG(ERROR) << "encoding packet failure";
            break;
        }

        frame.unref();
    }

    // release resource
    // FFFrame RAII
    av_packet_free(&pkt);
    avcodec_free_context(&c);

    if (encoding) {
        envir().taskScheduler().triggerEvent(selfDestructTriggerId, this);
        do {
            sched_yield();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        } while (encoding);
    }
    LOG(INFO) << "teardown encoding task";
}

static int open_decoder_context(int *stream_idx, AVCodecContext **dec_ctx, AVFormatContext *fmt_ctx, enum AVMediaType type) {
    int ret, stream_index;
    AVStream *st;
    const AVCodec *dec = NULL;
    el::Logger* logger = el::Loggers::getLogger("default");

    ret = av_find_best_stream(fmt_ctx, type, -1, -1, NULL, 0);
    if (ret < 0) {
        logger->error("Could not find %s stream\n", av_get_media_type_string(type));
        return ret;
    }

    stream_index = ret;
    st = fmt_ctx->streams[stream_index];

    /* find decoder for the stream */
    dec = avcodec_find_decoder(st->codecpar->codec_id);
    if (!dec) {
        logger->error("Failed to find %s codec\n", av_get_media_type_string(type));
        return AVERROR(EINVAL);
    }

    /* Allocate a codec context for the decoder */
    *dec_ctx = avcodec_alloc_context3(dec);
    if (!*dec_ctx) {
        logger->error("Failed to allocate the %s codec context\n", av_get_media_type_string(type));
        return AVERROR(ENOMEM);
    }

    /* Copy codec parameters from input stream to output codec context */
    if ((ret = avcodec_parameters_to_context(*dec_ctx, st->codecpar)) < 0) {
        logger->error("Failed to copy %s codec parameters to decoder context\n", av_get_media_type_string(type));
        return ret;
    }

    /* Init the decoders */
    if ((ret = avcodec_open2(*dec_ctx, dec, NULL)) < 0) {
        logger->error("Failed to open %s codec\n", av_get_media_type_string(type));
        return ret;
    }
    *stream_idx = stream_index;

    return 0;
}

static void pgm_save(unsigned char *buf, int wrap, int xsize, int ysize, char *filename)
{
    FILE *f;
    int i;

    f = fopen(filename,"wb");
    fprintf(f, "P5\n%d %d\n%d\n", xsize, ysize, 255);
    for (i = 0; i < ysize; i++)
        fwrite(buf + i * wrap, 1, xsize, f);
    fclose(f);
}

int FFH264InputSource::decodePacket(AVStream *video_st, AVCodecContext *c, const AVPacket *pkt, AVFrame *frame, SwsContext *sws_ctx) {
    int ret = 0;
    bool retry = false;
    char estring[AV_ERROR_MAX_STRING_SIZE] = { 0 };
    el::Logger* logger = el::Loggers::getLogger("default");

    do {
        // submit the packet to the decoder
        ret = avcodec_send_packet(c, pkt);
        if (ret == AVERROR(EAGAIN)) {
            if (retry) {
                logger->error("already retry, but still EAGAIN");
                return AVERROR_BUG;
            }
            retry = true;
            ret = 0;
        } else if (ret < 0) {
            logger->error("Error submitting a packet for decoding (%s)", av_make_error_string(estring, AV_ERROR_MAX_STRING_SIZE, ret));
            return ret;
        }

        // get all the available frames from the decoder
        while (ret >= 0) {
            ret = avcodec_receive_frame(c, frame);
            if (ret < 0) {
                // those two return values are special and mean there is no output
                // frame available, but there were no errors during decoding
                if (ret == AVERROR_EOF || ret == AVERROR(EAGAIN)) break;

                logger->error("Error during decoding (%s)", av_make_error_string(estring, AV_ERROR_MAX_STRING_SIZE, ret));
                return ret;
            }

            // queue to encoding
            int width = frame->width;
            int height = frame->height;
            int format = frame->format;
            AVRational tb = video_st->time_base;
            double pts = (frame->pts == AV_NOPTS_VALUE) ? NAN : frame->pts * av_q2d(tb);
            //LOG(INFO) << "frame width " << frame->width << " height:" << frame->height;
            //LOG(INFO) << " frame->pts:" << frame->pts << " pts:" << pts;
            // XXX: frame pkt_size ??
            //fpsStat("decoding", frame->pkt_size);

            if (sws_ctx) {
                AVFrame *picture;
                picture = av_frame_alloc();
                picture->format = AV_PIX_FMT_YUV420P;
                picture->width  = width;
                picture->height = height;
                ret = av_frame_get_buffer(picture, 0);
                assert(ret == 0);
                sws_scale(sws_ctx, (const uint8_t * const *) frame->data,
                        frame->linesize, 0, frame->height, picture->data, picture->linesize);

                if (dumpfile) {
                    char filename[1024]{0};
                    uint8_t *dst_data[4] = {NULL};
                    int dst_linesize[4];
                    snprintf(filename, sizeof(filename), "dump_%s/decoding_%dx%d.%s",
                                sTimestamp.c_str(), picture->width, picture->height,
                                av_get_pix_fmt_name((enum AVPixelFormat)picture->format));
                    enum AVPixelFormat pix_fmt = static_cast<enum AVPixelFormat>(picture->format);
                    int bufsize = av_image_alloc(dst_data, dst_linesize, picture->width, picture->height, pix_fmt, 1);
                    if (bufsize >= 0) {
                        av_image_copy(dst_data, dst_linesize, (const uint8_t **)(picture->data),
                                picture->linesize, static_cast<enum AVPixelFormat>(picture->format),
                                picture->width, picture->height);
                        FILE *fp = fopen(filename, "a+");
                        if (fp) {
                            fwrite(dst_data[0], 1, bufsize, fp);
                            fclose(fp);
                        }
                        av_free(dst_data[0]);
                    }
                }

                {
                    std::lock_guard<std::mutex> lock(decodedFrames_lock);
                    decodedFrames.emplace_back(picture, pts, width, height, format);
                }
                av_frame_free(&picture);
            } else {
                // write the frame data to output file
                // ffmpeg/example/decode_video.c
                // ffmpeg/example/demuxing_decoding.c
                if (dumpfile) {
                    char filename[1024]{0};
                    if (pgm) {
                        snprintf(filename, sizeof(filename), "%s/%d", sTimestamp.c_str(), c->frame_number);
                        pgm_save(frame->data[0], frame->linesize[0], frame->width, frame->height, filename);
                    } else {
                        uint8_t *dst_data[4] = {NULL};
                        int dst_linesize[4];
                        snprintf(filename, sizeof(filename), "dump_%s/decoding_%dx%d.%s",
                                sTimestamp.c_str(), frame->width, frame->height, av_get_pix_fmt_name((enum AVPixelFormat)frame->format));
                        enum AVPixelFormat pix_fmt = static_cast<enum AVPixelFormat>(frame->format);
                        int bufsize = av_image_alloc(dst_data, dst_linesize, frame->width, frame->height, pix_fmt, 1);
                        if (bufsize >= 0) {
                            av_image_copy(dst_data, dst_linesize, (const uint8_t **)(frame->data),
                                    frame->linesize, pix_fmt, frame->width, frame->height);
                            FILE *fp = fopen(filename, "a+");
                            if (fp) {
                                fwrite(dst_data[0], 1, bufsize, fp);
                                fclose(fp);
                            }
                            av_free(dst_data[0]);
                        }
                    }
                }

                std::lock_guard<std::mutex> lock(decodedFrames_lock);
                decodedFrames.emplace_back(frame, pts, width, height, format);
            }
            decodedFrames_cond.notify_one();

            av_frame_unref(frame);
        }
    } while (retry);

    return 0;
}

// XXX: std::async
//      avfilter transcoding rawvideo to h264
bool FFH264InputSource::startCapture() {
    LOG(INFO) << "capture starting";
    int ret = 0;

    //av_log_set_level(AV_LOG_DEBUG);

    // . prepare reading+decoding context
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
    av_dict_set(&opts, "probesize", "256000", 0);

    std::string video_size = std::to_string(width) + "x" + std::to_string(height);
    av_dict_set(&opts, "video_size", video_size.c_str(), 0);
    std::string fr = std::to_string(kEncoderFramerate);
    av_dict_set(&opts, "framerate", fr.c_str(), 0);
    ret = avformat_open_input(&fmtctx, device.c_str(), ifmt, &opts);
    av_dict_free(&opts);
    if (ret != 0) {
        LOG(ERROR) << "avformat_open_input fail:" << ret;
        avformat_free_context(fmtctx);
        return false;
    }

    if (avformat_find_stream_info(fmtctx, NULL) < 0) {
        LOG(ERROR) << "avformat_find_stream_info fail:" << ret;
        avformat_close_input(&fmtctx);
        return false;
    }

    int stream_idx = -1;
    AVCodecContext *decctx = NULL;
    ret = open_decoder_context(&stream_idx, &decctx, fmtctx, AVMEDIA_TYPE_VIDEO);
    if (ret != 0) {
        LOG(ERROR) << "open_decoder_context fail:" << ret;
        avformat_close_input(&fmtctx);
        return false;
    }
    av_dump_format(fmtctx, 0, device.c_str(), 0);

    struct SwsContext *sws_ctx = NULL;
    if (decctx->pix_fmt != AV_PIX_FMT_YUV420P) {
        sws_ctx = sws_getContext(decctx->width, decctx->height,
                decctx->pix_fmt,
                decctx->width, decctx->height,
                AV_PIX_FMT_YUV420P,
                SWS_BICUBIC, NULL, NULL, NULL);
        if (!sws_ctx) {
            LOG(ERROR) << "Could not initialize the conversion context";
            avformat_close_input(&fmtctx);
            return false;
        }
    }

    // .. prepare encoding context
    const AVCodec *codec = avcodec_find_encoder_by_name("libx264");
    if (!codec) {
        LOG(ERROR) << "can't find h264 encoder";
        sws_freeContext(sws_ctx);
        avcodec_free_context(&decctx);
        avformat_close_input(&fmtctx);
        return false;
    }

    AVCodecContext *encctx = avcodec_alloc_context3(codec);
    if (!encctx) {
        LOG(ERROR) << "avcodec_alloc_context3 failure";
        sws_freeContext(sws_ctx);
        avcodec_free_context(&decctx);
        avformat_close_input(&fmtctx);
        return false;
    }
    encctx->bit_rate = 200000;
    encctx->width = decctx->width;
    encctx->height = decctx->height;
    encctx->time_base = (AVRational){1, kEncoderFramerate};
    encctx->framerate = (AVRational){kEncoderFramerate, 1}; //decctx->framerate;
    encctx->gop_size = 10;
    //encctx->max_b_frames = 0;
    encctx->has_b_frames = 0;
    encctx->pix_fmt = AV_PIX_FMT_YUV420P; //all
    // https://trac.ffmpeg.org/wiki/Encode/H.264
    av_opt_set(encctx->priv_data, "preset", "slower", 0);
    av_opt_set(encctx->priv_data, "weightp", "none", 0);

    AVDictionary *codec_opts = NULL;
    av_dict_set(&codec_opts, "threads", "auto", 0);
    ret = avcodec_open2(encctx, codec, &codec_opts);
    av_dict_free(&codec_opts);
    if (ret < 0) {
        LOG(ERROR) << "avcodec_open2 failure:" << ret;
        avcodec_free_context(&encctx);
        sws_freeContext(sws_ctx);
        avcodec_free_context(&decctx);
        avformat_close_input(&fmtctx);
        return false;
    }

    auto ts = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch());
    sTimestamp = std::to_string(ts.count());
    LOG(INFO) << "session timestamp: " << sTimestamp;
    std::string dumpdir = "dump_" + sTimestamp;
    if (dumpfile) mkdir(dumpdir.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);

    next_pts = 0;
    decoding = true;
    decodingThread = std::thread(&FFH264InputSource::decodingTask, this, fmtctx, decctx, stream_idx, sws_ctx);
    encoding = true;
    encodingThread = std::thread(&FFH264InputSource::encodingTask, this, encctx);
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

static struct timeval timeval_normalise(struct timeval ts) {
    while(ts.tv_usec >= USEC_PER_SEC) {
        ++(ts.tv_sec);
        ts.tv_usec -= USEC_PER_SEC;
    }

    while(ts.tv_usec <= -USEC_PER_SEC) {
        --(ts.tv_sec);
        ts.tv_usec += USEC_PER_SEC;
    }

    if(ts.tv_usec < 0) {
        /* Negative nanoseconds isn't valid according to POSIX.
         * Decrement tv_sec and roll tv_nsec over.
         */

        --(ts.tv_sec);
        ts.tv_usec = (USEC_PER_SEC + ts.tv_usec);
    }

    return ts;
}

unsigned char* FFH264InputSource::extractFrame(unsigned char* frame, size_t& size, size_t& outsize, int& frameType)
{
	unsigned char * outFrame = NULL;
	outsize = 0;
	unsigned int markerlength = 0;
	frameType = 0;

	unsigned char *startFrame = (unsigned char*)memmem(frame,size,H264marker,sizeof(H264marker));
	if (startFrame != NULL) {
		markerlength = sizeof(H264marker);
	} else {
		startFrame = (unsigned char*)memmem(frame,size,H264shortmarker,sizeof(H264shortmarker));
		if (startFrame != NULL) {
			markerlength = sizeof(H264shortmarker);
		}
	}
	if (startFrame != NULL) {
		frameType = startFrame[markerlength];

		int remainingSize = size-(startFrame-frame+markerlength);
		unsigned char *endFrame = (unsigned char*)memmem(&startFrame[markerlength], remainingSize, H264marker, sizeof(H264marker));
		if (endFrame == NULL) {
			endFrame = (unsigned char*)memmem(&startFrame[markerlength], remainingSize, H264shortmarker, sizeof(H264shortmarker));
		}

		if (m_keepMarker)
		{
			size -=  startFrame-frame;
			outFrame = startFrame;
		}
		else
		{
			size -=  startFrame-frame+markerlength;
			outFrame = &startFrame[markerlength];
		}

		if (endFrame != NULL)
		{
			outsize = endFrame - outFrame;
		}
		else
		{
			outsize = size;
		}
		size -= outsize;
	} else if (size>= sizeof(H264shortmarker)) {
		 LOG(INFO) << "No marker found";
	}

	return outFrame;
}

std::list<std::pair<unsigned char*,size_t>> FFH264InputSource::splitNalu(unsigned char* frame, unsigned frameSize) {
	std::list<std::pair<unsigned char*,size_t>> frameList;

	size_t bufSize = frameSize;
	size_t size = 0;
	int frameType = 0;
	unsigned char* buffer = this->extractFrame(frame, bufSize, size, frameType);
	while (buffer != NULL) {
		switch (frameType&0x1F) {
			case 7: m_sps.assign((char*)buffer,size); break;
			case 8: m_pps.assign((char*)buffer,size); break;
			case 5:
				if (m_repeatConfig && !m_sps.empty() && !m_pps.empty()) {
					frameList.push_back(std::pair<unsigned char*,size_t>((unsigned char*)m_sps.c_str(), m_sps.size()));
					frameList.push_back(std::pair<unsigned char*,size_t>((unsigned char*)m_pps.c_str(), m_pps.size()));
				}
			break;
			default:
				break;
		}

		if (!m_sps.empty() && !m_pps.empty())
		{
			u_int32_t profile_level_id = 0;
			if (m_sps.size() >= 4) profile_level_id = (((unsigned char)m_sps[1])<<16)|(((unsigned char)m_sps[2])<<8)|((unsigned char)m_sps[3]);

			char* sps_base64 = base64Encode(m_sps.c_str(), m_sps.size());
			char* pps_base64 = base64Encode(m_pps.c_str(), m_pps.size());

			std::ostringstream os;
			os << "profile-level-id=" << std::hex << std::setw(6) << std::setfill('0') << profile_level_id;
			os << ";sprop-parameter-sets=" << sps_base64 <<"," << pps_base64;
			m_auxLine.assign(os.str());

			delete [] sps_base64;
			delete [] pps_base64;
		}
		frameList.push_back(std::pair<unsigned char*,size_t>(buffer, size));

		buffer = this->extractFrame(&buffer[size], bufSize, size, frameType);
	}
	return frameList;
}

// we use DiscreteFramer, need feed one NALU
// one time, for no data partition, seems
// one slice one NALU
void FFH264InputSource::doGetNextFrame() {
    if (!started && !startCapture()) {
        handleClosure();
        return;
    }
    if (!started) started = true;

    if (nalus_sending.empty()) {
        {
            std::lock_guard<std::mutex> lg(encodedPackets_lock);
            if (encodedPackets.empty()) return;

            pkt_sending = std::move(encodedPackets.front());
            encodedPackets.pop_front();

            // next future
            if (!encodedPackets.empty())
                envir().taskScheduler().triggerEvent(frameNotifyTriggerId, this);
        }

        if (!last_pts) {
            last_pts = pkt_sending.pkt->pts;
            gettimeofday(&last_tv, NULL);
            pts_sending = last_tv;
        } else {
            // TODO:
            AVRational time_base = (AVRational){1, kEncoderFramerate};
            int64_t elapsed_us = ((pkt_sending.pkt->pts - last_pts) * av_q2d(time_base)) * INT64_C(1000000);

            last_tv.tv_usec += elapsed_us;
            pts_sending = last_tv = timeval_normalise(last_tv);
            last_pts = pkt_sending.pkt->pts;
        }

        nalus_sending = splitNalu(pkt_sending.pkt->data, pkt_sending.pkt->size);
    }
    if (nalus_sending.empty()) {
        LOG(INFO) << "sending empty return";
        return;
    }

    std::pair<unsigned char*,size_t> nalu = nalus_sending.front();
    nalus_sending.pop_front();
    size_t size = nalu.second;
    const unsigned char *data = nalu.first;

#if 0
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    fprintf(stderr, "%d %ld.%06ld queue size:%ld nalue_size:%zu\n", gettid(), ts.tv_sec, (ts.tv_nsec / 1000), encodedPackets.size(), size);
#endif

    fFrameSize = size;
    if (size > fMaxSize) {
        fFrameSize = fMaxSize;
        fNumTruncatedBytes = size - fMaxSize;
    }

    //fDurationInMicroseconds = 60000;
    fPresentationTime = pts_sending;
    memcpy(fTo, data, fFrameSize);
    if (fFrameSize > 0) FramedSource::afterGetting(this);
}
}
