#pragma once

#include <memory>

#include "LiveMediaInputSource.hh"
#include "LiveMediaTypeDef.h"

namespace LiveRTSP {
class FFH264InputSource : public LiveMediaInputSource {
public:
    static FFH264InputSource *CreateNew(UsageEnvironment &env, const ParamTypeKeyValMap &tkv);
protected:
    FFH264InputSource(UsageEnvironment &env);
    ~FFH264InputSource();
private:
    virtual void doGetNextFrame();
};
}
