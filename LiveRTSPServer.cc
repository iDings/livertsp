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
            (TaskScheduler::BackgroundHandlerProc*)&ControlProcess, this);
    return true;
}

// private static callback
void LiveRTSPServer::LiveTask(LiveRTSPServer *lrs) {
    {
        std::lock_guard<std::mutex> lock(lrs->startMutex);
        lrs->start = true;
    }
    lrs->startCondVar.notify_one();

    LOG(INFO) << "LiveRTSP Running";
    lrs->env->taskScheduler().doEventLoop(&lrs->stoppedFlag);

    lrs->start = false;
    lrs->stoppedFlag = 0;
    LOG(INFO) << "LiveRTSP Running Done";
}

bool LiveRTSPServer::Start() {
    if (start) return true;
    if (stoppedFlag) return false;
    LOG(INFO) << "Starting";

    liveThread = std::thread(LiveTask, this);
    {
        std::unique_lock<std::mutex> lock(startMutex);
        startCondVar.wait(lock, [this] {return start;});
    }
    LOG(INFO) << "Starting Done";
    return true;
}

bool LiveRTSPServer::Stop() {
    if (!start) return true;
    if (stoppedFlag) return true;

    LOG(INFO) << "Stop";
    stoppedFlag = 1;
    std::string stop("stop");
    Control(stop);
    liveThread.join();

    LOG(INFO) << "Stop Done";
    return true;
}

uint32_t LiveRTSPServer::Control(const std:: string &msg) {
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

// @LiveTask private static callback
void LiveRTSPServer::ControlProcess(LiveRTSPServer *lrs, int mask) {
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
            LOG(INFO) << "comamnd: " << tokens[0];
            std::map<std::string, std::string> kvmap;
            for (auto &keyval : tokens) {
                std::vector<std::string> kv;
                split(keyval, kv, "=");
                if (kv.empty() || kv.size() != 2) continue;
                kvmap[kv[0]] = kv[1];
            }

            for (auto &kv : kvmap) {
                LOG(INFO) << "key:" << kv.first << " val:" << kv.second;
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
}
