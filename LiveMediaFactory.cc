#include "LiveMediaFactory.hh"

#include <ctime>

#include "easyloggingpp/easylogging++.h"

#include "H264LiveMediaSubsession.hh"
#include "H265LiveMediaSubsession.hh"
#include "FFH264InputSource.hh"
#include "FFADTSInputSource.hh"

namespace LiveRTSP {
LiveMediaFactory::LiveMediaFactory() {
    InputSourceTypeCreator ffmpeg;
    ffmpeg["h264"] = FFH264InputSource::MakeUnique;
    inputSources["ffmpeg"] = ffmpeg;
}

LiveMediaFactory::~LiveMediaFactory() {
}

ServerMediaSession *LiveMediaFactory::MakeServerMediaSession(UsageEnvironment &env,
        std::string session_name, std::string description, const ParamTypeKeyValMap &insrc,
        const ParamTypeKeyValMap &video, const ParamTypeKeyValMap &audio) {
    if (!ParamValidation(insrc, video, audio)) {
        LOG(ERROR) << "parameter validation error";
        return nullptr;
    }

    std::time_t t = time(NULL);
    std::tm tm{};
    char date[32]{};
    strftime(date, sizeof(date), "%a, %d %b %Y %H:%M:%S %Z", gmtime_r(&t, &tm));

    ServerMediaSession *sms = ServerMediaSession::createNew(env,
            session_name.c_str(), date, description.c_str(), false, NULL);
    //sms->addSubsession();

    return sms;
}

bool LiveMediaFactory::ParamValidation(const ParamTypeKeyValMap &insrc,
        const ParamTypeKeyValMap &video, const ParamTypeKeyValMap &audio) {
    return true;
}
}
