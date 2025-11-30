#ifndef LORAALOHA_ALOHA_H_
#define LORAALOHA_ALOHA_H_

#include "../../common/common.h"
#include "../../mac/MacBase.h"

using namespace inet;
using namespace physicallayer;

namespace rlora
{
    class Aloha : public MacBase
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

        void initializeProtocol() override;

        void handleLowerPacket(Packet *packet) override;
        void handleWithFsm(cMessage *msg) override;
        void handlePacket(Packet *packet) override;

        void createPacket(int payloadSize, int missionId, int source, bool isMission) override;

        void handleLeaderFragment(const BroadcastLeaderFragment *msg);
        void handleFragment(const BroadcastFragment *msg);
    };

}

#endif