#ifndef LORACSMA_CSMA_H_
#define LORACSMA_CSMA_H_

#include "../../common/common.h"
#include "../../mac/MacBase.h"

using namespace inet;
namespace rlora
{
    class Csma : public MacBase
    {
    protected:
        cMessage *endBackoff = nullptr;
        simtime_t slotTime = 0.200;
        int cw = 16;

        enum State
        {
            SWITCHING,
            BACKOFF,
            TRANSMITTING,
            LISTENING,
            RECEIVING
        };
        cFSM fsm;

        BackoffHandler *backoffHandler;

        void initializeProtocol() override;
        void finishProtocol() override;

        void handlePacket(Packet *packet) override;
        void handleLowerPacket(Packet *packet) override;
        void handleWithFsm(cMessage *msg) override;

        void createPacket(int payloadSize, int missionId, int source, bool isMission) override;

        void handleLeaderFragment(const BroadcastLeaderFragment *msg);
        void handleFragment(const BroadcastFragment *msg);
    };

}

#endif
