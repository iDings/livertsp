#include <stdio.h>
#include <stdlib.h>
#include "easyloggingpp/easylogging++.h"

#include "LiveRTSPServer.hh"

INITIALIZE_EASYLOGGINGPP

using namespace LiveRTSP;

static bool running = true;

static void signal_handler(int sig, siginfo_t *si, void *ucontext) {
    if (sig == SIGINT) running = false;

    printf("si_signo=%d, si_code=%d (%s), ", si->si_signo, si->si_code,
            (si->si_code == SI_USER) ? "SI_USER" :
            (si->si_code == SI_QUEUE) ? "SI_QUEUE" : "other");
    printf("si_value=%d\n", si->si_value.sival_int);
    printf("si_pid=%ld, si_uid=%ld\n", (long) si->si_pid, (long) si->si_uid);
    return;
}

const char *get_systid(const el::LogMessage *) {
    static char buf[64];
    snprintf(buf, sizeof(buf) - 1, "%d", gettid());
    return buf;
}

int main(int argc, char **argv) {
    START_EASYLOGGINGPP(argc, argv);
    el::Helpers::installCustomFormatSpecifier(el::CustomFormatSpecifier("%tid", get_systid));
    el::Loggers::reconfigureAllLoggers(el::ConfigurationType::Format, "%datetime %tid %logger/%level %loc %msg");
    LOG(INFO) << "Starting Live Streaming";

    struct sigaction sa;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_SIGINFO;
    sa.sa_sigaction = signal_handler;
    sigaction(SIGINT, &sa, NULL);

    portNumBits rtspServerPortNum = 8554;
    unsigned reclamationTestSeconds = 60;
    bool debug = true;
    std::unique_ptr<LiveRTSPServer> lrs =
        LiveRTSPServer::MakeLiveRTSPServer(rtspServerPortNum, reclamationTestSeconds, debug);
    if (lrs == NULL) {
        LOG(ERROR) << "make live rtsp server port " << rtspServerPortNum << " failure";
        exit(1);
    }

    lrs->Start();
    lrs->Control("add_session name=livestream0 insrc=ffmpeg video=h264,width:640,heigh:480");

    while (running) {
        sleep(1);
    }

    //lrs->Control("del_session name=livestream0");
    lrs->Stop();
    return 0;
}
