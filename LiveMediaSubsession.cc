#include "LiveMediaSubsession.hh"
#include "easyloggingpp/easylogging++.h"

namespace LiveRTSP {
LiveMediaSubsession::LiveMediaSubsession(UsageEnvironment &env, StreamReplicator *replicator) :
    OnDemandServerMediaSubsession(env, False),
    replicator(replicator) {
    //LOG(DEBUG) << "+LiveMediaSubsession";
}

LiveMediaSubsession::~LiveMediaSubsession() {
    //LOG(DEBUG) << "~LiveMediaSubsession: numReplicas:" << replicator->numReplicas();
    if (!replicator->numReplicas()) Medium::close(replicator);
}
}
