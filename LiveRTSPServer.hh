#pragma once
#include <vector>
#include <memory>
#include <thread>
#include <unistd.h>
#include <fcntl.h>
#include <mutex>
#include <atomic>
#include <map>
#include <functional>
#include <condition_variable>
#include <iostream>

#include "mcore/unique_fd.h"
#include "easyloggingpp/easylogging++.h"

#include "RTSPServer.hh"
#include "BasicUsageEnvironment.hh"
#include "GroupsockHelper.hh"
#include "Media.hh"

namespace LiveRTSP {

class LiveRTSPServer {
public:
    static std::unique_ptr<LiveRTSPServer> MakeLiveRTSPServer(Port ourPort, unsigned reclamationSeconds);

    ~LiveRTSPServer();

    LiveRTSPServer(const LiveRTSPServer &) = delete;
    LiveRTSPServer& operator=(const LiveRTSPServer &) = delete;

    bool Start();
    bool Stop();
    uint32_t Control(const std::string &msg);

protected:
    LiveRTSPServer();

private:
    class RTSPServerImpl : RTSPServer {
        public:
            static RTSPServerImpl *MakeRTSPServerImpl(UsageEnvironment &env,
                    Port ourPort, UserAuthenticationDatabase *authDatabase, unsigned reclamationSeconds);
            void Close();
        protected:
            RTSPServerImpl(UsageEnvironment &env, int ourSocketIPv4, int ourSocketIPv6, Port ourPort,
                    UserAuthenticationDatabase *authDatabase, unsigned reclamationSeconds);
            ~RTSPServerImpl();
    };

    struct RTSPServerImplDeleter {
        void operator()(RTSPServerImpl *impl) {
            LOG(INFO) << "Release RTSPServer Media";
            // Media::close will finally delete this
            impl->Close();
        }
    };

    struct UsageEnvironmentDeleter {
        void operator()(UsageEnvironment *env) {
            if (!env->reclaim())
                LOG(INFO) << "-->reclaiming fail\n";
        }
    };
    using unique_rtspserver = std::unique_ptr<RTSPServerImpl, RTSPServerImplDeleter>;
    using unique_usageenv = std::unique_ptr<UsageEnvironment, UsageEnvironmentDeleter>;

private:
    bool Initialize(Port ourPort, unsigned reclamationSeconds);
    static void LiveTask(LiveRTSPServer *livertsp);
    static void ControlProcess(LiveRTSPServer *livertsp, int mask);
    bool Poking(std::vector<uint8_t> &messageBuf);

    // RAII
    static uint32_t genid;
    mcore::unique_fd pipe_r;
    mcore::unique_fd pipe_w;
    std::thread liveThread;
    bool start;
    std::mutex startMutex;
    std::condition_variable startCondVar;

    std::unique_ptr<TaskScheduler> scheduler;
    unique_usageenv env;
    unique_rtspserver rtspServer;

    std::vector<uint8_t> messageBuf;
    typedef std::function<void (const std::map<std::string,std::string> &keyval)> ControlHandler;
    std::map<std::string, ControlHandler> controlMap;
    char stoppedFlag;
};
}
