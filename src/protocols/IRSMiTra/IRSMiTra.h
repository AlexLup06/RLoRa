#ifndef IRSMITRA_H_
#define IRSMITRA_H_

#include "../../common/common.h"
#include "../../mac/RtsCtsBase.h"

using namespace inet;

namespace rlora
{
    class IRSMiTra : public RtsCtsBase
    {

    protected:
        enum State
        {
            SWITCHING,
            BACKOFF,
            SEND_RTS,
            WAIT_CTS,
            TRANSMITING,
            SEND_CTS,
            LISTENING,
            RECEIVING,
            CW_CTS,
            AWAIT_TRANSMISSION,
        };
        cFSM fsm;

        virtual void initializeRtsCtsProtocol() override;

        void handleLowerPacket(Packet *packet) override;
        void handleWithFsm(cMessage *msg) override;
        void handlePacket(Packet *packet) override;

        void createPacket(int payloadSize, int missionId, int source, bool retransmit) override;
    };

}

#endif
