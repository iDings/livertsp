#include "FFH264InputSource.hh"
#include "easyloggingpp/easylogging++.h"

namespace LiveRTSP {
FFH264InputSource *FFH264InputSource::CreateNew(UsageEnvironment &env, const ParamTypeKeyValMap &video_tkv) {
    return new FFH264InputSource(env);
}

FFH264InputSource::FFH264InputSource(UsageEnvironment &env) : LiveMediaInputSource(env) {
    LOG(DEBUG) << "+FFH264InputSource";
}

FFH264InputSource::~FFH264InputSource() {
    LOG(DEBUG) << "~FFH264InputSource";
}

void FFH264InputSource::doGetNextFrame() {
    LOG(DEBUG) << "doGetNextFrame";
}
}
