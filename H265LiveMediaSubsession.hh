#pragma once

#include <string>

#include "OnDemandServerMediaSubsession.hh"
#include "StreamReplicator.hh"

namespace LiveRTSP {
class H265LiveMediaSubsession : public OnDemandServerMediaSubsession {
public:
    static H265LiveMediaSubsession *createNew(UsageEnvironment &env, StreamReplicator &replicator);

protected:
    H265LiveMediaSubsession(UsageEnvironment &env, StreamReplicator &replicator);
    virtual ~H265LiveMediaSubsession();

protected:
    virtual char const* getAuxSDPLine(RTPSink* rtpSink, FramedSource* inputSource) override;
    virtual FramedSource* createNewStreamSource(unsigned clientSessionId, unsigned& estBitrate) override;
    virtual RTPSink* createNewRTPSink(Groupsock* rtpGroupsock,
            unsigned char rtpPayloadTypeIfDynamic, FramedSource* inputSource) override;

private:
    StreamReplicator &replicator;
};
}
