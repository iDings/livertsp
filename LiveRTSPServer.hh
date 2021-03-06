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

#include "LiveMediaFactory.hh"

namespace LiveRTSP {

class LiveRTSPServer {
public:
    static std::unique_ptr<LiveRTSPServer> MakeLiveRTSPServer(Port ourPort, unsigned reclamationSeconds, bool log_debug = false);

    ~LiveRTSPServer();

    LiveRTSPServer(const LiveRTSPServer &) = delete;
    LiveRTSPServer& operator=(const LiveRTSPServer &) = delete;

    bool Start();
    bool Stop();

    // TODO:
    // message string builder
    // reliable control, control sync
    // result notify callback
    // cancelable
    bool Control(const std::string &msg);

protected:
    LiveRTSPServer(bool log_debug);

private:
    class RTSPServerImpl : public RTSPServer {
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
            LOG(INFO) << "Destroy RTSPServer";
            // Media::close will finally delete this
            impl->Close();
        }
    };

    struct UsageEnvironmentDeleter {
        void operator()(UsageEnvironment *env) {
            if (!env->reclaim())
                LOG(ERROR) << "-->reclaiming fail\n";
        }
    };
    using RTSPServerImplUnique = std::unique_ptr<RTSPServerImpl, RTSPServerImplDeleter>;
    using UsageEnvUnique = std::unique_ptr<UsageEnvironment, UsageEnvironmentDeleter>;

private:
    static void LiveTask(LiveRTSPServer *livertsp);
    static void ControlMethodDispatch(LiveRTSPServer *livertsp, int mask);

    bool Initialize(Port ourPort, unsigned reclamationSeconds);
    bool Poking(std::vector<uint8_t> &messageBuf);

    /**
     * info
     *
     * @param kv ignore
     */
    bool ControlMethodInfo(const std::map<std::string, std::string> &kv);
    /**
     * stop
     *
     * @param kv ignore
     */
    bool ControlMethodStop(const std::map<std::string, std::string> &kv);
    /**
     * add_session
     *
     * @param kv name= input=ffmpeg video= audio=
     */
    bool ControlMethodAddSession(const std::map<std::string, std::string> &kv);
    /**
     * del_session
     *
     * @param kv name=
     */
    bool ControlMethodDelSession(const std::map<std::string, std::string> &kv);

    // RAII
    static uint32_t genid;
    bool start;
    char stoppedFlag;

    mcore::unique_fd pipe_r;
    mcore::unique_fd pipe_w;
    std::thread liveThread;
    std::mutex startMutex;
    std::condition_variable startCondVar;

    std::unique_ptr<TaskScheduler> scheduler;
    UsageEnvUnique env;
    RTSPServerImplUnique rtspServer;
    std::vector<uint8_t> messageBuf;
    using ControlHandlerKVMap = std::map<std::string, std::string>;
    using ControlHandler = std::function<bool (const std::map<std::string,std::string> &keyval)>;
    std::map<std::string, ControlHandler> controlMethodHandlerMap;
    bool debug;
    LiveMediaFactory mediaFactory;
};
}
