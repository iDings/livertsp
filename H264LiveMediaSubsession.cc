#include "OnDemandServerMediaSubsession.hh"

namespace LiveRTSP {

class H264LiveMediaSubsession : public OnDemandServerMediaSubsession {
public:
    static H264LiveMediaSubsession *createNew(UsageEnvironment &env);
};

}
