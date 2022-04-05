#pragma once

#include "OnDemandServerMediaSubsession.hh"
#include "StreamReplicator.hh"

namespace LiveRTSP {
class LiveMediaSubsession : public OnDemandServerMediaSubsession {
protected:
    LiveMediaSubsession(UsageEnvironment &env, StreamReplicator *replicator);
    virtual ~LiveMediaSubsession();

    StreamReplicator& streamReplicator() const { return *replicator; };
private:
    StreamReplicator *replicator;
};
}
