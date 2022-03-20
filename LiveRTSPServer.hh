#pragma once
#include <memory>
#include <thread>
#include <unistd.h>
#include <fcntl.h>

#include "mcore/unique_fd.h"

namespace LiveRTSP {
// https://abseil.io/tips/42
class LiveRTSPServer {
public:
    static std::unique_ptr<LiveRTSPServer> MakeLiveRTSPServer();

    ~LiveRTSPServer();
    LiveRTSPServer(const LiveRTSPServer &) = delete;
    LiveRTSPServer& operator=(const LiveRTSPServer &) = delete;

    bool Control(std::string msg);
protected:
    LiveRTSPServer();

private:
    bool Initialize();
    mcore::unique_fd pipe_r;
    mcore::unique_fd pipe_w;
};
}
