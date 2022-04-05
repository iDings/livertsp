#include "LiveMediaFactory.hh"

#include <ctime>

#include "easyloggingpp/easylogging++.h"

#include "H264LiveMediaSubsession.hh"
#include "H265LiveMediaSubsession.hh"
#include "FFH264InputSource.hh"
#include "FFADTSInputSource.hh"

namespace LiveRTSP {
LiveMediaFactory::LiveMediaFactory() {
    // sources
    LiveMediaInputSourceTypeCreator ff;
    ff["h264"] = FFH264InputSource::CreateNew;
    insrcs["ffmpeg"] = std::move(ff);

    // sessions
    subsess["h264"] = H264LiveMediaSubsession::createNew;
}

LiveMediaFactory::~LiveMediaFactory() {
}

ServerMediaSession *LiveMediaFactory::MakeServerMediaSession(UsageEnvironment &env,
        std::string session_name, std::string description, const ParamTypeKeyValMap &insrc_tkv,
        const ParamTypeKeyValMap &video_tkv, const ParamTypeKeyValMap &audio_tkv) {
    if (!ParamValidation(insrc_tkv, video_tkv, audio_tkv)) {
        LOG(ERROR) << "parameter validation error";
        return nullptr;
    }

    std::time_t t = time(NULL);
    std::tm tm{};
    char date[32]{};
    strftime(date, sizeof(date), "%a, %d %b %Y %H:%M:%S %Z", gmtime_r(&t, &tm));

    ServerMediaSession *sms = ServerMediaSession::createNew(env,
            session_name.c_str(), date, description.c_str(), false, NULL);

    LiveMediaInputSourceTypeCreator &lmis = insrcs[insrc_tkv.first];
    if (!video_tkv.first.empty()) {
        LiveMediaInputSource *vinput = lmis[video_tkv.first](env, video_tkv);
        StreamReplicator *replicator = StreamReplicator::createNew(env, vinput);
        LiveMediaSubsession *lms = subsess[video_tkv.first](env, replicator, video_tkv);
        sms->addSubsession(lms);
    }

    if (!audio_tkv.first.empty()) {
        LiveMediaInputSource *ainput = lmis[audio_tkv.first](env, audio_tkv);
        StreamReplicator *replicator = StreamReplicator::createNew(env, ainput);
        LiveMediaSubsession *lms = subsess[audio_tkv.first](env, replicator, audio_tkv);
        sms->addSubsession(lms);
    }
    return sms;
}

// TODO: add param validate in insrc type
bool LiveMediaFactory::ParamValidation(const ParamTypeKeyValMap &insrc_tkv,
        const ParamTypeKeyValMap &video_tkv, const ParamTypeKeyValMap &audio_tkv) {
    auto insrc_factory = std::find_if(insrcs.begin(), insrcs.end(),
            [&insrc_tkv] (const std::pair<std::string, LiveMediaInputSourceTypeCreator> &pair) {
                return insrc_tkv.first == pair.first;
            });

    if (insrc_factory == insrcs.end()) {
        LOG(ERROR) << "insrcs without " << insrc_tkv.first;
        return false;
    }

    LiveMediaInputSourceTypeCreator &insrc_type_factory = insrc_factory->second;
    if (!video_tkv.first.empty()) {
        auto vinsrc = std::find_if(insrc_type_factory.begin(), insrc_type_factory.end(),
                [&video_tkv] (const std::pair<std::string, LiveMediaInputSourceCreator> &pair) {
                    return video_tkv.first == pair.first;
                });
        if (vinsrc == insrc_type_factory.end()) {
            LOG(ERROR) << "insrc_tkv:" << insrc_tkv.first << " without " << video_tkv.first;
            return false;
        }

        auto vsubs = std::find_if(subsess.begin(), subsess.end(),
                [&video_tkv] (const std::pair<std::string, LiveMediaSubsessionCreator> &pair) {
                    return video_tkv.first == pair.first;
                });
        if (vsubs == subsess.end()) {
            LOG(ERROR) << "subsess:" << " without " << video_tkv.first;
            return false;
        }
    }

    if (!audio_tkv.first.empty()) {
        auto ainsrc = std::find_if(insrc_type_factory.begin(), insrc_type_factory.end(),
                [&audio_tkv] (const std::pair<std::string, LiveMediaInputSourceCreator> &pair) {
                    return audio_tkv.first == pair.first;
                });
        if (ainsrc == insrc_type_factory.end()) {
            LOG(ERROR) << "insrc_tkv:" << insrc_tkv.first << " without " << video_tkv.first;
            return false;
        }

        auto asubs = std::find_if(subsess.begin(), subsess.end(),
                [&audio_tkv] (const std::pair<std::string, LiveMediaSubsessionCreator> &pair) {
                    return audio_tkv.first == pair.first;
                });
        if (asubs == subsess.end()) {
            LOG(ERROR) << "subsess:" << " without " << audio_tkv.first;
            return false;
        }
    }

    return true;
}
}
