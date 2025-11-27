#ifndef RLORA__MESHROUTER_H
#define RLORA__MESHROUTER_H

#include "../../common/common.h"

using namespace inet;
using namespace physicallayer;

namespace rlora {

/**
 * Based on CSMA class
 */

class MeshRouter : public MacProtocolBase, public IMacProtocol, public queueing::IActivePacketSink
{

protected:
    double bitrate = NaN;
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
    simsignal_t missionIdFragmentSent;
    simsignal_t receivedMissionId;
    simsignal_t timeOfLastTrajectorySignal;

    map<int, SimTime> idToAddedTimeMap;

    cMessage *waitDelay = nullptr;
    cMessage *mediumStateChange = nullptr;
    cMessage *droppedPacket = nullptr;
    cMessage *endTransmission = nullptr;
    cMessage *transmitSwitchDone = nullptr;
    cMessage *nodeAnnounce = nullptr;
    cMessage *receptionStated = nullptr;

    int nodeId = -1;
    IncompletePacketList incompleteMissionPktList;
    IncompletePacketList incompleteNeighbourPktList;
    CustomPacketQueue packetQueue;
    LoRaRadio *loRaRadio;
    MissionIdTracker missionIdFragmentSentTracker;
    TimeOfLastTrajectory timeOfLastTrajectory;

public:
    /**
     * @name Construction functions
     */
    //@{
    virtual ~MeshRouter();
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
    void createBroadcastPacket(int payloadSize, int hopId, int source, bool retransmit);
    void retransmitPacket(FragmentedPacket fragmentedPacket);
};

} // namespace rlora

#endif // ifndef __LoRaMeshRouter_H
