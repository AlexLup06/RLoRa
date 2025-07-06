//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
// 
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
// 
// You should have received a copy of the GNU Lesser General Public License
// along with this program.  If not, see http://www.gnu.org/licenses/.
// 

#ifndef LORAALOHA_LORAALOHA_H_
#define LORAALOHA_LORAALOHA_H_

#include "inet/physicallayer/wireless/common/contract/packetlevel/IRadio.h"
#include "inet/linklayer/contract/IMacProtocol.h"
#include "inet/linklayer/base/MacProtocolBase.h"
#include "inet/common/FSMA.h"
#include "inet/queueing/contract/IPacketQueue.h"
#include "inet/common/Protocol.h"
#include "inet/queueing/contract/IActivePacketSink.h"
#include "inet/queueing/contract/IPacketQueue.h"
#include "inet/linklayer/contract/IMacProtocol.h"

#include "../LoRa/LoRaRadio.h"
#include "../LoRa/LoRaMacFrame_m.h"
#include "../LoRa/LoRaTagInfo_m.h"

#include "../helpers/CustomPacketQueue.h"
#include "../helpers/IncompletePacketList.h"
#include "../helpers/LatestMessageIdMap.h"

using namespace inet;
using namespace physicallayer;

namespace rlora {

/**
 * Based on CSMA class
 */

class LoRaAloha : public MacProtocolBase, public IMacProtocol, public queueing::IActivePacketSink
{

protected:
    double bitrate = NaN;
    int headerLength = -1;
    MacAddress address;

    enum State
    {
        SWITCHING, TRANSMITING, LISTENING, RECEIVING
    };

    IRadio *radio = nullptr;
    IRadio::TransmissionState transmissionState = IRadio::TRANSMISSION_STATE_UNDEFINED;
    IRadio::ReceptionState receptionState = IRadio::RECEPTION_STATE_UNDEFINED;

    cFSM fsm;

    simtime_t measurementInterval = 1.0;  // 1 second
    long bytesReceivedInInterval = 0;
    long effectiveBytesReceivedInInterval = 0;
    cMessage *throughputTimer = nullptr;
    simsignal_t throughputSignal;
    simsignal_t effectiveThroughputSignal;
    simsignal_t timeInQueue;

    map<int, SimTime> idToAddedTimeMap;

    cMessage *mediumStateChange = nullptr;
    cMessage *droppedPacket = nullptr;
    cMessage *endTransmission = nullptr;
    cMessage *transmitSwitchDone = nullptr;
    cMessage *nodeAnnounce = nullptr;
    cMessage *receptionStated = nullptr;

    int nodeId = -1;
    IncompletePacketList incompletePacketList;
    CustomPacketQueue packetQueue;
    LatestMessageIdMap latestMessageIdMap;
    LoRaRadio *loRaRadio;

//    Packet *currentNodeAnnounceFrame = nullptr;
public:
    /**
     * @name Construction functions
     */
    //@{
    virtual ~LoRaAloha();
    //@}
    virtual MacAddress getAddress();
    virtual queueing::IPassivePacketSource* getProvider(cGate *gate) override;
    virtual void handleCanPullPacketChanged(cGate *gate) override;
    virtual void handlePullPacketProcessed(Packet *packet, cGate *gate, bool successful) override;

protected:
    /**
     * @name Initialization functions
     */
    //@{
    /** @brief Initialization of the module and its variables */
    virtual void initialize(int stage) override;
    virtual void finish() override;
    virtual void configureNetworkInterface() override;
    //@}

    /**
     * @name Message handing functions
     * @brief Functions called from other classes to notify about state changes and to handle messages.
     */
    //@{
    virtual void handleSelfMessage(cMessage *msg) override;
    virtual void handleUpperPacket(Packet *packet) override;
    virtual void handleLowerPacket(Packet *packet) override;
    virtual void handleWithFsm(cMessage *msg);
    void handlePacket(Packet *packet);

    virtual void receiveSignal(cComponent *source, simsignal_t signalID, intval_t value, cObject *details) override;

    virtual Packet* encapsulate(Packet *msg);
    virtual Packet* decapsulate(Packet *frame);
    //@}

    // OperationalBase:
    virtual void handleStartOperation(LifecycleOperation *operation) override
    {
    } // TODO implementation
    virtual void handleStopOperation(LifecycleOperation *operation) override
    {
    }  // TODO implementation
    virtual void handleCrashOperation(LifecycleOperation *operation) override
    {
    } // TODO implementation

    /**
     * @name Frame transmission functions
     */
    //@{
    virtual void sendDataFrame();

    /**
     * @name Utility functions
     */
    virtual void finishCurrentTransmission();
    virtual Packet* getCurrentTransmission();

    virtual bool isReceiving();

    void turnOnReceiver(void);
    void turnOffReceiver(void);

    void senderWaitDelay(int waitTime);
    void announceNodeId(int respond);
    void createBroadcastPacket(int packetSize, int messageId, int hopId, int source, bool retransmit);
    void retransmitPacket(FragmentedPacket fragmentedPacket);
};

} // namespace inet

#endif // ifndef __LoRaAloha_H
