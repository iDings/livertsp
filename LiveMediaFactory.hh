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
        std::unique_ptr<LiveMediaInputSource>(UsageEnvironment &env, const ParamTypeKeyValMap &tkv)>;
    using LiveMediaSubsessionCreator = std::function<
        std::unique_ptr<LiveMediaSubsession>(UsageEnvironment &env, StreamReplicator &replicator, const ParamTypeKeyValMap &tkv)>;
    using InputSourceTypeCreator = std::map<std::string, LiveMediaInputSourceCreator>;
    std::map<std::string, InputSourceTypeCreator> inputSources;
    std::map<std::string, LiveMediaSubsessionCreator> subsessions;
};
}
