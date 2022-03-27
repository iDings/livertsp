#include "FFH264InputSource.hh"

namespace LiveRTSP {
std::unique_ptr<LiveMediaInputSource> FFH264InputSource::MakeUnique(UsageEnvironment &env, const ParamTypeKeyValMap &tkv) {
    return nullptr;
}

FFH264InputSource::FFH264InputSource(UsageEnvironment &env) : LiveMediaInputSource(env) {
}

FFH264InputSource::~FFH264InputSource() {
}

}
