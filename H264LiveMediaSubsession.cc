#include "H264LiveMediaSubsession.hh"
#include "easyloggingpp/easylogging++.h"
#include "H264VideoStreamDiscreteFramer.hh"
#include "H264VideoStreamFramer.hh"
#include "H264VideoRTPSink.hh"

namespace LiveRTSP {

H264LiveMediaSubsession *H264LiveMediaSubsession::createNew(
        UsageEnvironment &env, StreamReplicator *replicator, const ParamTypeKeyValMap &tkv)
{
    H264LiveMediaSubsession *self = new H264LiveMediaSubsession(env, replicator);
    return self;
}

H264LiveMediaSubsession::H264LiveMediaSubsession(UsageEnvironment &env, StreamReplicator *replicator) :
    LiveMediaSubsession(env, replicator),
    pollingDoneFlag(0), pollingCount(0), dummyRTPSink(NULL)
{
    LOG(INFO) << "H264LiveMediaSubsession";
}

H264LiveMediaSubsession::~H264LiveMediaSubsession()
{
    LOG(INFO) << "~H264LiveMediaSubsession";
}

static void afterPlayingDummy(void* clientData) {
    LOG(INFO) << "afterPlayingDummy";
    H264LiveMediaSubsession* subsess = (H264LiveMediaSubsession*)clientData;
    subsess->afterPlayingDummy1();
}

void H264LiveMediaSubsession::afterPlayingDummy1() {
    // Unschedule any pending 'checking' task:
    envir().taskScheduler().unscheduleDelayedTask(nextTask());
    // Signal the event loop that we're done:
    setDoneFlag();
}

static void pollingAuxSDPLine(void* clientData) {
  H264LiveMediaSubsession* subsess = (H264LiveMediaSubsession*)clientData;
  subsess->pollingAuxSDPLine1();
}

void H264LiveMediaSubsession::pollingAuxSDPLine1() {
    nextTask() = NULL;

    char const* dasl;
    if (!auxSDPLine.empty()) {
        // Signal the event loop that we're done:
        setDoneFlag();
    } else if (dummyRTPSink != NULL && (dasl = dummyRTPSink->auxSDPLine()) != NULL) {
        auxSDPLine = dasl;
        dummyRTPSink = NULL;

        // Signal the event loop that we're done:
        setDoneFlag();
    } else if (!pollingDoneFlag) {
        if (pollingCount++ >= 10) {
            LOG(WARNING) << "polling auxSDPLine timeout";
            setDoneFlag();
        } else {
            // try again after a brief delay:
            int uSecsToDelay = 100000; // 100 ms
            nextTask() = envir().taskScheduler().scheduleDelayedTask(uSecsToDelay,
                    (TaskFunc*)pollingAuxSDPLine, this);
        }
    }
}

char const *H264LiveMediaSubsession::getAuxSDPLine(RTPSink *rtpSink, FramedSource *inputSource)
{
    LOG(INFO) << "getAuxSDPLine";
    if (!auxSDPLine.empty()) return auxSDPLine.c_str();

    if (dummyRTPSink == NULL) {
        dummyRTPSink = rtpSink;
        dummyRTPSink->startPlaying(*inputSource, afterPlayingDummy, this);
        pollingAuxSDPLine(this);
    }

    envir().taskScheduler().doEventLoop(&pollingDoneFlag);

    return auxSDPLine.empty() ? "" : auxSDPLine.c_str();
}

// TOOD: H264VideoStreamFramer ffplay will lose some when docoding
// TODO: H264VideoStreamDiscreteFramer need remove start and feed one NALU one time
// reference v4l2rtspserver
FramedSource *H264LiveMediaSubsession::createNewStreamSource(unsigned clientSessionId, unsigned &estBitrate) {
    LOG(INFO) << "createNewStreamSource";
    estBitrate = 1000; // kps, estimate
    FramedSource *source = streamReplicator().createStreamReplica();
    return H264VideoStreamDiscreteFramer::createNew(envir(), source);
    //return H264VideoStreamFramer::createNew(envir(), source, true);
}

RTPSink *H264LiveMediaSubsession::createNewRTPSink(Groupsock *rtpGroupsock,
            unsigned char rtpPayloadTypeIfDynamic, FramedSource *inputSource)
{
    LOG(INFO) << "createNewRTPSink";
    return H264VideoRTPSink::createNew(envir(), rtpGroupsock, rtpPayloadTypeIfDynamic);
}
}
