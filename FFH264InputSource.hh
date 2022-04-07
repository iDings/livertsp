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
    bool initialize(const ParamTypeKeyValMap &tkv);
    bool startCapture();
    void stopCapture();

    void decodingTask(AVFormatContext *s, AVCodecContext *decctx);
    void encodingTask();

    virtual void doGetNextFrame() override;

    // parameters
    int width;
    int height;
    int framerate;
    std::string dumpfile;
    std::string device;

    std::atomic_bool started;
    bool decoding;
    bool encoding;

    std::thread decodingThread;
    std::thread encodingThread;
};
}
