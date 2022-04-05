#pragma once

#include <memory>
#include "FramedSource.hh"

namespace LiveRTSP {

class LiveMediaInputSource;
// ref:FramedFileSource
class LiveMediaInputSource : public FramedSource {
protected:
    LiveMediaInputSource(UsageEnvironment &env);
    virtual ~LiveMediaInputSource();
};
}
