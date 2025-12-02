#ifndef RSMITRANAV_MIRS_H_
#define RSMITRANAV_MIRS_H_

#include "../../common/common.h"
#include "../../mac/RtsCtsBase.h"

using namespace inet;

namespace rlora
{
    class RSMiTraNAV : public RtsCtsBase
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
            READY_TO_SEND
        };
        cFSM fsm;

        void initializeRtsCtsProtocol() override;

        void handleWithFsm(cMessage *msg) override;
        void handlePacket(Packet *packet) override;

        void createPacket(int payloadSize, int missionId, int source, bool isMission) override;
    };

}

#endif
