#pragma once
#include <vector>
#include <memory>
#include <thread>
#include <unistd.h>
#include <fcntl.h>
#include <mutex>
#include <atomic>
#include <condition_variable>

#include "mcore/unique_fd.h"

#include "BasicUsageEnvironment.hh"
#include "GroupsockHelper.hh"

namespace LiveRTSP {
// https://abseil.io/tips/42
class LiveRTSPServer {
public:
    static std::unique_ptr<LiveRTSPServer> MakeLiveRTSPServer();
    ~LiveRTSPServer();

    LiveRTSPServer(const LiveRTSPServer &) = delete;
    LiveRTSPServer& operator=(const LiveRTSPServer &) = delete;

    bool Start();
    bool Stop();
    uint32_t Control(const std::string &msg);

protected:
    LiveRTSPServer();

private:
    static void LiveTask(LiveRTSPServer *livertsp);
    static void ControlHandler(LiveRTSPServer *livertsp, int mask);

    bool Poking(std::vector<uint8_t> &messageBuf);
    bool Initialize();

    static uint32_t genid;

    mcore::unique_fd pipe_r;
    mcore::unique_fd pipe_w;
    std::thread liveThread;

    bool start;
    std::mutex startMutex;
    std::condition_variable startCondVar;

    TaskScheduler *scheduler;
    UsageEnvironment *env;
    std::vector<uint8_t> messageBuf;
    char stoppedFlag;
};
}
