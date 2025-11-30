#ifndef RLORA__MESHROUTER_H
#define RLORA__MESHROUTER_H

#include "../../common/common.h"
#include "../../mac/MacBase.h"

using namespace inet;

namespace rlora
{
    class MeshRouter : public MacBase
    {

    protected:
        enum State
        {
            SWITCHING,
            TRANSMITING,
            LISTENING,
            RECEIVING
        };

        cFSM fsm;

        cMessage *waitDelay = nullptr;

    protected:
        void initializeProtocol() override;
        void finishProtocol() override;

        void handleLowerPacket(Packet *packet) override;
        void handleWithFsm(cMessage *msg) override;
        void handlePacket(Packet *packet) override;

        void createPacket(int payloadSize, int missionId, int source, bool retransmit) override;

        void scheduleWaitTimer();
        void senderWaitDelay(int waitTime);
    };

}

#endif
