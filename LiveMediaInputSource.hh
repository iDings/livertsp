#pragma once

#include <memory>
#include <map>

#include "FramedSource.hh"

namespace LiveRTSP {

class LiveMediaInputSource;
// ref:FramedFileSource
// TODO: builder to builder paramters
class LiveMediaInputSource : public FramedSource {
protected:
    LiveMediaInputSource(UsageEnvironment &env);
    virtual ~LiveMediaInputSource();

protected:
    void fpsStat(const std::string &ch, uint64_t size);

private:
    struct Stats {
        uint64_t ts_ms;
        uint64_t sizeps;
        uint32_t fps;
    };

    std::map<std::string, Stats> statMap;
};
}
