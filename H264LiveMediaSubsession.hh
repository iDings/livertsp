#pragma once

#include <string>

#include "OnDemandServerMediaSubsession.hh"
#include "StreamReplicator.hh"
#include "LiveMediaTypeDef.h"
#include "LiveMediaSubsession.hh"

namespace LiveRTSP {
class H264LiveMediaSubsession : public LiveMediaSubsession {
public:
    static H264LiveMediaSubsession *
        createNew(UsageEnvironment &env, StreamReplicator *replicator, const ParamTypeKeyValMap &tkv);

    // Used to implement "getAuxSDPLine()":
    void pollingAuxSDPLine1();
    void afterPlayingDummy1();

protected:
    H264LiveMediaSubsession(UsageEnvironment &env, StreamReplicator *replicator);
    virtual ~H264LiveMediaSubsession();

    void setDoneFlag() { pollingDoneFlag = ~0; pollingCount = 0; }

protected:
    virtual char const* getAuxSDPLine(RTPSink* rtpSink, FramedSource* inputSource) override;
    virtual FramedSource* createNewStreamSource(unsigned clientSessionId, unsigned& estBitrate) override;
    virtual RTPSink* createNewRTPSink(Groupsock* rtpGroupsock,
            unsigned char rtpPayloadTypeIfDynamic, FramedSource* inputSource) override;

private:
    std::string auxSDPLine;
    char pollingDoneFlag;
    int pollingCount;
    RTPSink *dummyRTPSink;
};
}
