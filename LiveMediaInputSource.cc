#include "LiveMediaInputSource.hh"
#include "easyloggingpp/easylogging++.h"

namespace LiveRTSP {
LiveMediaInputSource::LiveMediaInputSource(UsageEnvironment &env) : FramedSource(env) {
    //LOG(INFO) << "+LiveMediaInputSource";
}

LiveMediaInputSource::~LiveMediaInputSource() {
    //LOG(INFO) << "~LiveMediaInputSource";
}
}
