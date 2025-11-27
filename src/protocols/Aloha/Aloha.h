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

#ifndef LORAALOHA_ALOHA_H_
#define LORAALOHA_ALOHA_H_

#include "../../common/common.h"

using namespace inet;
using namespace physicallayer;

namespace rlora {

/**
 * Based on CSMA class
 */

class Aloha : public MacProtocolBase, public IMacProtocol, public queueing::IActivePacketSink
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

    simsignal_t effectiveThroughputSignal;
    simsignal_t timeInQueue;
    simsignal_t missionIdFragmentSent;
    simsignal_t receivedMissionId;
    simsignal_t throughputSignal;
    simsignal_t timeOfLastTrajectorySignal;

    map<int, SimTime> idToAddedTimeMap;

    cMessage *throughputTimer = nullptr;
    cMessage *mediumStateChange = nullptr;
    cMessage *droppedPacket = nullptr;
    cMessage *endTransmission = nullptr;
    cMessage *transmitSwitchDone = nullptr;
    cMessage *nodeAnnounce = nullptr;
    cMessage *receptionStated = nullptr;
    cMessage *moreMessagesToSend = nullptr;

    int nodeId = -1;
    IncompletePacketList incompleteMissionPktList;
    IncompletePacketList incompleteNeighbourPktList;
    CustomPacketQueue packetQueue;
    LoRaRadio *loRaRadio;
    TimeOfLastTrajectory timeOfLastTrajectory;

public:
    /**
     * @name Construction functions
     */
    //@{
    Aloha();
    virtual ~Aloha();
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
    }
    virtual void handleStopOperation(LifecycleOperation *operation) override
    {
    }
    virtual void handleCrashOperation(LifecycleOperation *operation) override
    {
    }

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
    void createBroadcastPacket(int payloadSize, int missionId, int source, bool retransmit);
    void retransmitPacket(FragmentedPacket fragmentedPacket);
};

} // namespace inet

#endif // ifndef __LoRaAloha_H
