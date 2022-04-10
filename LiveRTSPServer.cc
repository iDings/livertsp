#define LOGID "LiveRTSPServer"
#include "LiveRTSPServer.hh"

#include <unistd.h>
#include <limits.h>
#include <string.h>
#include <algorithm>
#include <ctime>

#include "easyloggingpp/easylogging++.h"
#include "nlohmann/json.hpp"
#include "mcore/string_format.h"

#include "StreamReplicator.hh"
#include "ServerMediaSession.hh"

#include "LiveMediaTypeDef.h"

namespace LiveRTSP {
enum message_type {
    TYPE_STRING = 0,
    TYEP_JSON,
};

struct MessageHeader {
    uint32_t length;
    int8_t type;
};

static const char *kControlMethodInfo = "info";
static const char *kControlMethodStop = "stop";
static const char *kControlMethodAddSession = "add_session";
static const char *kControlMethodDelSession = "del_session";

uint32_t LiveRTSPServer::genid = 0;

std::unique_ptr<LiveRTSPServer> LiveRTSPServer::MakeLiveRTSPServer(Port ourPort, unsigned reclamationTestSeconds, bool log_debug) {
    std::unique_ptr<LiveRTSPServer> self(new LiveRTSPServer(log_debug));
    if (!self || !self->Initialize(ourPort, reclamationTestSeconds)) {
        LOG(ERROR) << "Initialize Fail";
        return nullptr;
    }

    return self;
}

LiveRTSPServer::LiveRTSPServer(bool log_debug) :
    start(false), stoppedFlag(0), debug(log_debug) {
    LOG_IF(log_debug, TRACE) << "LiveRTSPServer Ctor";
}

LiveRTSPServer::~LiveRTSPServer() {
    LOG_IF(debug, TRACE) << "LiveRTSPServer Dtor";
}

bool LiveRTSPServer::Initialize(Port ourPort, unsigned reclamationTestSeconds) {
    int pipefd[2];
    // packet mode
    int ret = pipe2(pipefd, O_CLOEXEC | O_DIRECT);
    if (ret < 0) {
        LOG(ERROR) << "pipe2 failure: " << strerror(errno);
        return false;
    }

    makeSocketNonBlocking(pipefd[0]);
    pipe_r.reset(pipefd[0]);
    pipe_w.reset(pipefd[1]);

    scheduler.reset(BasicTaskScheduler::createNew());
    env.reset(BasicUsageEnvironment::createNew(*scheduler));
    rtspServer.reset(RTSPServerImpl::MakeRTSPServerImpl(*env, ourPort, NULL, reclamationTestSeconds));
    if (rtspServer == nullptr) return false;

    controlMethodHandlerMap[kControlMethodInfo] = std::bind(&LiveRTSPServer::ControlMethodInfo, this, std::placeholders::_1);
    controlMethodHandlerMap[kControlMethodStop] =
        [this](const std::map<std::string, std::string> &kv) {
            return ControlMethodStop(kv);
        };
    controlMethodHandlerMap[kControlMethodAddSession] =
        [this](const std::map<std::string, std::string> &kv) {
            return ControlMethodAddSession(kv);
        };
    controlMethodHandlerMap[kControlMethodDelSession] =
        [this](const std::map<std::string, std::string> &kv) {
            return ControlMethodDelSession(kv);
        };

    scheduler->setBackgroundHandling(pipe_r.get(), SOCKET_READABLE | SOCKET_EXCEPTION,
            (TaskScheduler::BackgroundHandlerProc*)&ControlMethodDispatch, this);
    return true;
}

// private static callback
void LiveRTSPServer::LiveTask(LiveRTSPServer *lrs) {
    LOG(INFO) << "LiveRTSPTask Starting";
    {
        std::lock_guard<std::mutex> lock(lrs->startMutex);
        lrs->start = true;
    }
    lrs->startCondVar.notify_one();

    lrs->env->taskScheduler().doEventLoop(&lrs->stoppedFlag);

    lrs->start = false;
    lrs->stoppedFlag = 0;
    LOG(INFO) << "LiveRTSPTask Exited";
}

// TODO: wait timeout
bool LiveRTSPServer::Start() {
    if (start) return true;
    if (stoppedFlag) return false;
    LOG_IF(debug, DEBUG) << "Starting";

    liveThread = std::thread(LiveTask, this);
    {
        std::unique_lock<std::mutex> lock(startMutex);
        startCondVar.wait(lock, [this] {return start;});
    }

    LOG_IF(debug, DEBUG) << "Starting Done";
    return true;
}

// TODO: review stop flow, selfdone
bool LiveRTSPServer::Stop() {
    if (!start) return true;
    if (stoppedFlag) return true;

    LOG_IF(debug, DEBUG) << "Stopping";

    if (liveThread.joinable()) {
        stoppedFlag = 1;
        std::string stop("stop");
        Control(stop);
        liveThread.join();
    }

    LOG_IF(debug, DEBUG) << "Stop Done";
    return true;
}

bool LiveRTSPServer::Control(const std:: string &msg) {
    if (!start) return false;

    MessageHeader message;
    message.length = sizeof(MessageHeader) + msg.length() + 1;
    message.type = TYPE_STRING;

    std::vector<uint8_t> messagebuf;
    const uint8_t *msghdr = reinterpret_cast<uint8_t *>(&message);
    messagebuf.insert(messagebuf.end(), msghdr, msghdr + sizeof(MessageHeader));
    messagebuf.insert(messagebuf.end(), msg.begin(), msg.end());
    messagebuf.emplace_back('\0');

    return Poking(messagebuf);
}

bool LiveRTSPServer::Poking(std::vector<uint8_t> &messagebuf) {
    int ret = 0;
    size_t writen = 0;

    do {
        ret = write(pipe_w.get(), messagebuf.data() + writen, messagebuf.size() - writen);
        if (ret > 0) writen += ret;
    } while (writen != messagebuf.size() && (ret == -1 && errno == EINTR));

    if (writen != messagebuf.size()) {
        LOG(ERROR) << "Poking fail " << strerror(errno);
        return false;
    }

    return true;
}

__attribute_maybe_unused__
static std::string& strip(std::string& s, const std::string& chars = " ") {
    s.erase(0, s.find_first_not_of(chars.c_str()));
    s.erase(s.find_last_not_of(chars.c_str()) + 1);
    return s;
}

static void split(const std::string& s, std::vector<std::string>& tokens, const std::string& delimiters = " ") {
    std::string::size_type lastPos = s.find_first_not_of(delimiters, 0);
    std::string::size_type pos = s.find_first_of(delimiters, lastPos);
    while (std::string::npos != pos || std::string::npos != lastPos) {
        tokens.push_back(s.substr(lastPos, pos - lastPos));
        lastPos = s.find_first_not_of(delimiters, pos);
        pos = s.find_first_of(delimiters, lastPos);
    }
}

// type,key0:val0,key1:val0,key2:val2
static void sessionParameterParse(const std::string &param, ParamTypeKeyValMap *tkv) {
    if (param.empty()) return;

    std::string::size_type tpos = param.find_first_of(',');
    std::string::size_type kvpos = param.find_first_not_of(',', tpos);
    if (tpos == std::string::npos || kvpos == std::string::npos) {
        tkv->first = param.substr(0, tpos);
        return;
    }

    tkv->first = param.substr(0, tpos);
    std::vector<std::string> kvs;
    do {
        std::string::size_type nkvpos = param.find_first_of(',', kvpos);
        if (nkvpos == std::string::npos) kvs.push_back(param.substr(kvpos));
        else kvs.push_back(param.substr(kvpos, nkvpos - kvpos));

        kvpos = param.find_first_not_of(',', nkvpos);
    } while (kvpos != std::string::npos);

    std::for_each(kvs.begin(), kvs.end(),
            [tkv](const std::string &kv) {
                std::vector<std::string> k_v;
                split(kv, k_v, ":");
                tkv->second[k_v[0]] = k_v[1];
            });
    return;
}

bool LiveRTSPServer::ControlMethodInfo(const std::map<std::string, std::string> &kv) {
    LOG(INFO) << "method: info";
    return true;
}

bool LiveRTSPServer::ControlMethodStop(const std::map<std::string, std::string> &kv) {
    LOG(INFO) << "method: stop";
    return true;
}

bool LiveRTSPServer::ControlMethodAddSession(const std::map<std::string, std::string> &kv) {
    const std::string *session_name = nullptr;
    const std::string *video_param = nullptr;
    const std::string *audio_param = nullptr;
    const std::string *insrc_param = nullptr;
    std::string description;
    ParamTypeKeyValMap insrc_tkv;
    ParamTypeKeyValMap video_tkv;
    ParamTypeKeyValMap audio_tkv;

    auto name = std::find_if(kv.begin(), kv.end(),
            [](const std::pair<std::string, std::string> &pair) {
                return pair.first == "name";
            });
    if (name == kv.end()) {
        LOG(ERROR) << "need [name] key";
        return false;
    }
    session_name = &(name->second);

    auto insrc = std::find_if(kv.begin(), kv.end(),
            [](const std::pair<std::string, std::string> &pair) {
                return pair.first == "insrc";
            });
    if (insrc == kv.end()) {
        LOG(ERROR) << "need [insrc] key";
        return false;
    }
    insrc_param = &(insrc->second);

    auto video = std::find_if(kv.begin(), kv.end(),
            [](const std::pair<std::string, std::string> &pair) {
                return pair.first == std::string("video");
            });
    auto audio = std::find_if(kv.begin(), kv.end(),
            [](const std::pair<std::string, std::string> &pair) {
                return pair.first == "audio";
            });

    if ((video == kv.end() || video->second.empty()) &&
            (audio == kv.end() || audio->second.empty())) {
        LOG(ERROR) << "need video or audio parameter";
        return false;
    }

    // reference, or pointer
    if (video != kv.end()) {
        video_param = &(video->second);
    }

    if (audio != kv.end()) {
        audio_param = &(audio->second);
    }

    sessionParameterParse(*insrc_param, &insrc_tkv);
    LOG_IF(debug, DEBUG) << "insrc type:" << insrc_tkv.first;
    std::for_each(insrc_tkv.second.begin(), insrc_tkv.second.end(),
            [this](const std::pair<std::string, std::string> &pair) {
                LOG_IF(debug, DEBUG) << " param:" << pair.first << ":" << pair.second;
            });

    if (video_param) {
        sessionParameterParse(*video_param, &video_tkv);
        LOG_IF(debug, DEBUG) << "video type:" << video_tkv.first;
        std::for_each(video_tkv.second.begin(), video_tkv.second.end(),
                [this](const std::pair<std::string, std::string> &pair) {
                    LOG_IF(debug, DEBUG) << " param:" << pair.first << ":" << pair.second;
                });
    }
    if (audio_param) {
        sessionParameterParse(*audio_param, &audio_tkv);
        LOG_IF(debug, DEBUG) << "audio type:" << audio_tkv.first;
        std::for_each(audio_tkv.second.begin(), audio_tkv.second.end(),
                [this](const std::pair<std::string, std::string> &pair) {
                    LOG_IF(debug, DEBUG) << " param:" << pair.first << ":" << pair.second;
                });
    }

    if (video_param) description += "video:" + *video_param;
    if (audio_param) description += ",audio:" + *audio_param;
    description += ",streamed by LiveRTSP(live555)";

    ServerMediaSession *sms = mediaFactory.MakeServerMediaSession(*env,
                                *session_name, description, insrc_tkv, video_tkv, audio_tkv);
    if (!sms) {
        LOG(ERROR) << "add_session make media session error";
        return false;
    }

    //LOG(INFO) << "add_session " << *session_name << " insrc:"
    //    << *insrc_param << " v:" << (video_param ? *video_param : "")
    //    << " a:" << (audio_param ? *audio_param : "");
    rtspServer->addServerMediaSession(sms);
    return true;
}

bool LiveRTSPServer::ControlMethodDelSession(const std::map<std::string, std::string> &kv) {
    auto name = std::find_if(kv.begin(), kv.end(),
            [](const std::pair<std::string, std::string> &pair) {
                return pair.first == "name";
            });
    if (name == kv.end()) {
        LOG(ERROR) << "need [name] key";
        return true;
    }

    rtspServer->removeServerMediaSession(name->second.c_str());
    return true;
}

// @LiveTask private static callback
void LiveRTSPServer::ControlMethodDispatch(LiveRTSPServer *lrs, int mask) {
    int ret;
    bool selfdone = false;
    uint8_t buf[1024];
    int fd = lrs->pipe_r.get();

    do {
        ret = read(fd, buf, sizeof(buf));
        if (ret > 0) {
            lrs->messageBuf.insert(lrs->messageBuf.end(), buf, buf + ret);
        } else if (ret == 0) {
            selfdone = true;
            LOG(ERROR) << "pipe closed";
            break;
        } else if (ret < 0) {
            if (errno == EINTR) continue;

            if ((errno != EAGAIN || errno != EWOULDBLOCK)) {
                selfdone = true;
                LOG(ERROR) << "read failure " << strerror(errno);
            }
            break;
        }
    } while (1);

    do {
        if (lrs->messageBuf.size() < sizeof(MessageHeader)) break;

        uint8_t *mbuf = lrs->messageBuf.data();
        MessageHeader *msgHdr = reinterpret_cast<MessageHeader *>(mbuf);
        if (lrs->messageBuf.size() < msgHdr->length) break;

        if (msgHdr->type != TYPE_STRING) {
            LOG(ERROR) << "unsupport type: " << msgHdr->type;
            lrs->messageBuf.erase(lrs->messageBuf.begin(), lrs->messageBuf.begin() + msgHdr->length);
            continue;
        }

        char *cmdstr = reinterpret_cast<char *>(mbuf) + sizeof(MessageHeader);
        std::string command(cmdstr);
        std::vector<std::string> tokens;
        split(command, tokens);
        if (!tokens.empty()) {
            LOG_IF(lrs->debug, DEBUG) << "comamnd: " << tokens[0];
            std::string &cmd = tokens[0];

            std::map<std::string, std::string> kvmap;
            for (auto &keyval : tokens) {
                std::vector<std::string> kv;
                split(keyval, kv, "=");
                if (kv.empty() || kv.size() != 2) continue;
                kvmap[kv[0]] = kv[1];
            }

            for (auto &kv : kvmap) LOG_IF(lrs->debug, DEBUG) << "key:" << kv.first << " val:" << kv.second;
            auto it = std::find_if(
                    lrs->controlMethodHandlerMap.begin(), lrs->controlMethodHandlerMap.end(),
                    [&](const std::pair<std::string, ControlHandler> &kf) {
                        return kf.first == cmd;
                    });
            if (it != lrs->controlMethodHandlerMap.end()) {
                it->second(kvmap);
            } else {
                LOG(WARNING) << "unsupport command:" << cmd;
            }
        }

        lrs->messageBuf.erase(lrs->messageBuf.begin(), lrs->messageBuf.begin() + msgHdr->length);
    } while (1);

    if (selfdone) {
        lrs->liveThread.detach();
        lrs->stoppedFlag = 1;
    }

    if (lrs->stoppedFlag) {
        lrs->messageBuf.clear();
    }
    return;
}

LiveRTSPServer::RTSPServerImpl *
LiveRTSPServer::RTSPServerImpl::MakeRTSPServerImpl(UsageEnvironment &env,
        Port ourPort, UserAuthenticationDatabase *authDatabase, unsigned reclamationSeconds) {
    int ourSocketIPv4 = setUpOurSocket(env, ourPort, AF_INET);
    int ourSocketIPv6 = setUpOurSocket(env, ourPort, AF_INET6);
    if (ourSocketIPv4 < 0 || ourSocketIPv6 < 0) {
        LOG(ERROR) << "socket ipv4 failure:" << env.getResultMsg();
        return nullptr;
    }

    return new RTSPServerImpl(env, ourSocketIPv4, ourSocketIPv6, ourPort, authDatabase, reclamationSeconds);
}

LiveRTSPServer::RTSPServerImpl::~RTSPServerImpl() {
    //LOG(INFO) << "RTSPServerImpl Dtor:" << name() << std::endl;
}

LiveRTSPServer::RTSPServerImpl::RTSPServerImpl(UsageEnvironment &env, int ourSocketIPv4, int ourSocketIPv6,
        Port ourPort, UserAuthenticationDatabase *authDatabase, unsigned reclamationTestSeconds) :
    RTSPServer(env, ourSocketIPv4, ourSocketIPv6, ourPort, authDatabase, reclamationTestSeconds) {
    //LOG(INFO) << "RTSPServerImpl Ctor";
}

void LiveRTSPServer::RTSPServerImpl::Close() {
    Medium::close(this);
}
}
