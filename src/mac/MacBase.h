#ifndef MAC_BASE_H_
#define MAC_BASE_H_

#include "../common/common.h"
#include "PacketBase.h"

using namespace inet;
using namespace physicallayer;

namespace rlora
{

    class MacBase : public PacketBase
    {

    public:
        MacBase() {}
        virtual ~MacBase() {}

        void initialize(int stage) override;
        void finish() override;

        virtual void initializeProtocol() {};
        virtual void finishProtocol() {};

        virtual void createPacket(int payloadSize, int missionId, int source, bool retransmit) = 0;

        void handleSelfMessage(cMessage *msg) override;
        virtual void handleUpperPacket(Packet *packet) override;
        virtual void handlePacket(Packet *packet) = 0;

        virtual void sendDataFrame();

        void finishCurrentTransmission();
        Packet *getCurrentTransmission();

        void retransmitPacket(Result result);

    protected:
        cMessage *moreMessagesToSend = nullptr;

        simsignal_t receivedMissionId;
        simsignal_t missionIdFragmentSent;
        simsignal_t missionIdRtsSent;

    private:
    };
}
#endif