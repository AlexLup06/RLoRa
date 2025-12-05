#include "MacContext.h"

namespace rlora
{
    Define_Module(MacContext);

    void MacContext::initializeMacContext()
    {
        const char *addressString = par("address");
        if (!strcmp(addressString, "auto"))
        {
            address = MacAddress::generateAutoAddress();
            par("address").setStringValue(address.str().c_str());
        }
        else
        {
            address.setAddress(addressString);
        }

        receivedFragmentId = registerSignal("receivedFragmentId");

        txQueue = getQueue(gate(upperLayerInGateId));
        nodeId = intuniform(0, 16777216);
    }

    MacAddress MacContext::getAddress()
    {
        return address;
    }

    void MacContext::handleCanPullPacketChanged(cGate *gate)
    {
        Enter_Method("handleCanPullPacketChanged");
        Packet *packet = dequeuePacket();
        handleUpperMessage(packet);
    }

    void MacContext::handlePullPacketProcessed(Packet *packet, cGate *gate, bool successful)
    {
        Enter_Method("handlePullPacketProcessed");
        throw cRuntimeError("Not supported callback");
    }

    void MacContext::configureNetworkInterface()
    {
        networkInterface->setDatarate(bitrate);
        networkInterface->setMacAddress(address);

        networkInterface->setMtu(std::numeric_limits<int>::quiet_NaN());
        networkInterface->setMulticast(true);
        networkInterface->setBroadcast(true);
        networkInterface->setPointToPoint(false);
    }

    queueing::IPassivePacketSource *MacContext::getProvider(cGate *gate)
    {
        return (gate->getId() == upperLayerInGateId) ? txQueue.get() : nullptr;
    }

    double MacContext::predictOngoingMsgTime(int packetBytes)
    {
        double sf = loRaRadio->loRaSF;
        double bw = loRaRadio->loRaBW.get();
        double cr = loRaRadio->loRaCR;
        simtime_t Tsym = (pow(2, sf)) / (bw / 1000);

        double preambleSymbNb = 8;
        double headerSymbNb = 8;
        double payloadSymbNb = std::ceil((8 * packetBytes - 4 * sf + 28 + 16 - 20 * 0) / (4 * (sf - 2 * 0))) * (cr + 4);
        if (payloadSymbNb < 0)
            payloadSymbNb = 0;

        simtime_t Tpreamble = (preambleSymbNb + 4.25) * Tsym / 1000;
        simtime_t Theader = headerSymbNb * Tsym / 1000;
        ;
        simtime_t Tpayload = payloadSymbNb * Tsym / 1000;

        const simtime_t duration = Tpreamble + Theader + Tpayload;
        return duration.dbl();
    }

};