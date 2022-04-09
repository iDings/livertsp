#pragma once

#include <memory>
#include <list>
#include <vector>
#include <deque>
#include <thread>
#include <map>
#include <atomic>

#include "LiveMediaInputSource.hh"
#include "LiveMediaTypeDef.h"
#include "FFHelper.hh"

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
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

    bool initialize(const ParamTypeKeyValMap &tkv);
    bool startCapture();
    void stopCapture();

    int decodePacket(AVCodecContext *dec, const AVPacket *pkt, AVFrame *frame);
    void decodingTask(AVFormatContext *s, AVCodecContext *decctx);
    void encodingTask();

    virtual void doGetNextFrame() override;

    // parameters
    int width;
    int height;
    int framerate;
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

    std::deque<FFFrame> decodedFrames;
};
}
