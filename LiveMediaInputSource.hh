#pragma once

#include <memory>
#include "FramedSource.hh"

namespace LiveRTSP {

class LiveMediaInputSource;
// ref:FramedFileSource
// TODO: builder to builder paramters
class LiveMediaInputSource : public FramedSource {
protected:
    LiveMediaInputSource(UsageEnvironment &env);
    virtual ~LiveMediaInputSource();
};
}
