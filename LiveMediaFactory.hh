#pragma once

#include <functional>
#include <string>
#include <map>
#include <vector>
#include <memory>

#include "UsageEnvironment.hh"
#include "ServerMediaSession.hh"
#include "StreamReplicator.hh"

#include "LiveMediaTypeDef.h"
#include "LiveMediaSubsession.hh"
#include "LiveMediaInputSource.hh"

namespace LiveRTSP {
// TODO: Reflector
class LiveMediaFactory {
public:
    LiveMediaFactory();
    ~LiveMediaFactory();

    ServerMediaSession *MakeServerMediaSession(UsageEnvironment &env, std::string session_name, std::string description,
            const ParamTypeKeyValMap &insrc, const ParamTypeKeyValMap &video, const ParamTypeKeyValMap &audio);
    bool ParamValidation(const ParamTypeKeyValMap &insrc,
            const ParamTypeKeyValMap &video, const ParamTypeKeyValMap &audio);

private:
    using LiveMediaInputSourceCreator = std::function<
        LiveMediaInputSource *(UsageEnvironment &env, const ParamTypeKeyValMap &tkv)>;
    using LiveMediaSubsessionCreator = std::function<
        LiveMediaSubsession *(UsageEnvironment &env, StreamReplicator *replicator, const ParamTypeKeyValMap &tkv)>;
    using LiveMediaInputSourceTypeCreator = std::map<std::string, LiveMediaInputSourceCreator>;

    // TODO: use struct?
    std::map<std::string, LiveMediaInputSourceTypeCreator> insrcs;
    std::map<std::string, LiveMediaSubsessionCreator> subsess;
};
}
