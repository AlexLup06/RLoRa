#ifndef MAC_CONTEXT_H_
#define MAC_CONTEXT_H_

#include "../common/common.h"

using namespace inet;
using namespace physicallayer;

namespace rlora
{
    class MacContext : public MacProtocolBase, public IMacProtocol, public queueing::IActivePacketSink
    {
    public:
        void initializeMacContext();

        void configureNetworkInterface() override;

        MacAddress getAddress();
        queueing::IPassivePacketSource *getProvider(cGate *gate) override;
        void handleCanPullPacketChanged(cGate *gate) override;
        void handlePullPacketProcessed(Packet *packet, cGate *gate, bool successful) override;

        virtual void handleWithFsm(cMessage *msg) {};
        double predictOngoingMsgTime(int packetBytes);

    protected:
        MacAddress address;
        double bitrate = NaN;
        int nodeId = -1;

        LoRaRadio *loRaRadio;

        simsignal_t receivedFragmentId;

    private:
    };
}

#endif