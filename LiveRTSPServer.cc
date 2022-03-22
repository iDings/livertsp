#define LOGID "LiveRTSPServer"
#include "LiveRTSPServer.hh"

#include <unistd.h>
#include <limits.h>
#include <string.h>

#include "easyloggingpp/easylogging++.h"
#include "nlohmann/json.hpp"
#include "StreamReplicator.hh"

namespace LiveRTSP {
enum message_type {
    TYPE_STRING = 0,
    TYEP_JSON,
};

struct MessageHeader {
    uint32_t length;
    int8_t type;
};

uint32_t LiveRTSPServer::genid = 0;

std::unique_ptr<LiveRTSPServer> LiveRTSPServer::MakeLiveRTSPServer() {
    std::unique_ptr<LiveRTSPServer> self(new LiveRTSPServer());
    if (!self->Initialize()) {
        LOG(ERROR) << "Initialize Fail";
        return nullptr;
    }

    return self;
}

LiveRTSPServer::LiveRTSPServer() : start(false), stoppedFlag(0) {}
LiveRTSPServer::~LiveRTSPServer() {
    env->reclaim();
    delete scheduler;
}

bool LiveRTSPServer::Initialize() {
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

    scheduler = BasicTaskScheduler::createNew();
    env = BasicUsageEnvironment::createNew(*scheduler);
    scheduler->setBackgroundHandling(pipe_r.get(), SOCKET_READABLE | SOCKET_EXCEPTION,
            (TaskScheduler::BackgroundHandlerProc*)&ControlHandler, this);
    return true;
}

// private static callback
void LiveRTSPServer::LiveTask(LiveRTSPServer *lrs) {
    LOG(INFO) << "LiveRTSP Running";
    {
        std::lock_guard<std::mutex> lock(lrs->startMutex);
        lrs->start = true;
    }
    lrs->startCondVar.notify_one();

    lrs->env->taskScheduler().doEventLoop(&lrs->stoppedFlag);
    LOG(INFO) << "LiveRTSP Stoped";
}

bool LiveRTSPServer::Start() {
    liveThread = std::thread(LiveTask, this);
    {
        std::unique_lock<std::mutex> lock(startMutex);
        startCondVar.wait(lock, [this] {return start;});
    }
    return true;
}

bool LiveRTSPServer::Stop() {
    if (stoppedFlag) return true;

    stoppedFlag = 1;
    std::string stop("stop");
    Control(stop);
    liveThread.join();

    start = false;
    return true;
}

bool LiveRTSPServer::Control(const std:: string &msg) {
    if (!start) return false;

    MessageHeader message;
    message.length = sizeof(MessageHeader) + msg.length() + 1;
    message.type = TYPE_STRING;

    std::vector<uint8_t> messagebuf;
    uint8_t *msghdr = reinterpret_cast<uint8_t *>(&message);
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

// @LiveTask private static callback
void LiveRTSPServer::ControlHandler(LiveRTSPServer *lrs, int mask) {
    int ret;
    uint8_t buf[1024];
    int fd = lrs->pipe_r.get();

    ret = read(fd, buf, sizeof(buf));
    if (ret > 0) {
        lrs->messageBuf.insert(lrs->messageBuf.end(), buf, buf + ret);
    } else if (ret == 0) {
        lrs->stoppedFlag = 1;
        LOG(ERROR) << "pipe closed";
    } else if (ret < 0 && (errno != EAGAIN || errno != EWOULDBLOCK)) {
        lrs->stoppedFlag = 1;
        LOG(ERROR) << "read failure " << strerror(errno);
    }

    do {
        if (lrs->stoppedFlag) return;
        if (lrs->messageBuf.size() < sizeof(MessageHeader)) return;

        uint8_t *mbuf = lrs->messageBuf.data();
        MessageHeader *msgHdr = reinterpret_cast<MessageHeader *>(mbuf);
        if (lrs->messageBuf.size() < msgHdr->length) return;

        if (msgHdr->type != TYPE_STRING) {
            LOG(ERROR) << "unsupport type: " << msgHdr->type;
            lrs->messageBuf.erase(lrs->messageBuf.begin(), lrs->messageBuf.begin() + msgHdr->length);
            continue;
        }

        const char *cmdstr = reinterpret_cast<char *>(mbuf) + sizeof(MessageHeader);
        LOG(INFO) << "command: " << cmdstr;
        // TODO: dispatch handler

        lrs->messageBuf.erase(lrs->messageBuf.begin(), lrs->messageBuf.begin() + msgHdr->length);
    } while (1);

    return;
}
}
