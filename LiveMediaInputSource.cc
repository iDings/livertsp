#include "LiveMediaInputSource.hh"
#include "easyloggingpp/easylogging++.h"

namespace LiveRTSP {
LiveMediaInputSource::LiveMediaInputSource(UsageEnvironment &env) : FramedSource(env) {
    //LOG(INFO) << "+LiveMediaInputSource";
}

LiveMediaInputSource::~LiveMediaInputSource() {
    //LOG(INFO) << "~LiveMediaInputSource";
}

void LiveMediaInputSource::fpsStat(const std::string &ch, uint64_t size) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    uint64_t ts_ms = ts.tv_sec * INT64_C(1000) + (ts.tv_nsec / INT64_C(1000000));

    if (!statMap.count(ch)) {
        statMap[ch] = {ts_ms, size, 1};
        return;
    }

    statMap[ch].fps++;
    statMap[ch].sizeps += size;
    uint64_t delta_ms = ts_ms - statMap[ch].ts_ms;
    if (delta_ms >= 1000) {
        float fps = static_cast<float>(statMap[ch].fps) / delta_ms * 1000;
        float bitrate = static_cast<float>(statMap[ch].sizeps) / delta_ms * 1000;
        LOG(INFO) << "channel:" << ch << " fps:" << fps << " bitrate:" <<bitrate;

        statMap[ch].ts_ms = ts_ms;
        statMap[ch].fps = 0;
        statMap[ch].sizeps = 0;
    }
    return;
}

}
