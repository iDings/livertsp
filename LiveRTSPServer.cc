#define LOGID "LiveRTSPServer"
#include "LiveRTSPServer.hh"

#include "easyloggingpp/easylogging++.h"
#include "nlohmann/json.hpp"

#include "StreamReplicator.hh"

namespace LiveRTSP {
enum message_type {
    TYPE_STRING = 0,
    TYEP_JSON,
};

struct Message {
    int8_t type;
    uint32_t length;
};

std::unique_ptr<LiveRTSPServer> LiveRTSPServer::MakeLiveRTSPServer() {
    std::unique_ptr<LiveRTSPServer> self(new LiveRTSPServer());
    if (!self->Initialize()) {
        CLOG(ERROR, LOGID) << "Initialize Fail";
        return nullptr;
    }

    return self;
}

LiveRTSPServer::LiveRTSPServer() {}
LiveRTSPServer::~LiveRTSPServer() {}

bool LiveRTSPServer::Initialize() {
    int pipefd[2];
    int ret = pipe2(pipefd, O_CLOEXEC | O_DIRECT);
    if (ret < 0) {
        CLOG(ERROR, LOGID) << "pipe2 failure: " << strerror(errno);
        return false;
    }

    pipe_r.reset(pipefd[0]);
    pipe_w.reset(pipefd[1]);
    return true;
}
}
