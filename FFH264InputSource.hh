#pragma once

#include <memory>
#include <list>
#include <vector>
#include <deque>
#include <thread>
#include <map>
#include <atomic>
#include <mutex>
#include <condition_variable>

#include "LiveMediaInputSource.hh"
#include "LiveMediaTypeDef.h"
#include "FFHelper.hh"

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}

namespace LiveRTSP {
class FFH264InputSource : public LiveMediaInputSource {
public:
    static FFH264InputSource *CreateNew(UsageEnvironment &env, const ParamTypeKeyValMap &tkv);
protected:
    FFH264InputSource(UsageEnvironment &env);
    ~FFH264InputSource();
    virtual void doStopGettingFrames() override;

private:
    static void selfDestructTriggerHandler(void *udata);
    static void frameNotifyTriggerHandler(void *udata);

    bool initialize(const ParamTypeKeyValMap &tkv);
    bool startCapture();
    void stopCapture();

    int decodePacket(AVStream *video_st, AVCodecContext *dec, const AVPacket *pkt, AVFrame *frame, SwsContext *sws_ctx);
    void decodingTask(AVFormatContext *s, AVCodecContext *decctx, int stream_idx, SwsContext *sws_ctx);

    int encodePacket(AVCodecContext *c, const AVFrame *frame, AVPacket *pkt);
    void encodingTask(AVCodecContext *c);

    virtual void doGetNextFrame() override;

    // parameters
    int width;
    int height;
    int framerate;
    // dumpfile=yuv
    // dumpfile=pgm
    bool dumpfile;
    bool pgm;
    std::string device;

    std::atomic_bool started;
    bool decoding;
    bool encoding;

    std::thread decodingThread;
    std::thread encodingThread;

    EventTriggerId selfDestructTriggerId;
    std::string sTimestamp;

    std::mutex decodedFrames_lock;
    std::condition_variable decodedFrames_cond;
    std::deque<FFFrame> decodedFrames;

    std::mutex encodedPackets_lock;
    std::deque<FFPacket> encodedPackets;

    EventTriggerId frameNotifyTriggerId;

    struct timeval last_tv;
    int64_t last_pts;
};
}
