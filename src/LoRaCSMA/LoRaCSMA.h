/*
 * LoRaCSMA.h
 *
 *  Created on: 24 Mar 2025
 *      Author: alexanderlupatsiy
 */

#ifndef LORACSMA_LORACSMA_H_
#define LORACSMA_LORACSMA_H_

#include "inet/common/FSMA.h"
#include "inet/common/packet/Packet.h"
#include "inet/linklayer/base/MacProtocolBase.h"
#include "inet/linklayer/contract/IMacProtocol.h"
#include "inet/linklayer/csmaca/CsmaCaMacHeader_m.h"
#include "inet/physicallayer/wireless/common/contract/packetlevel/IRadio.h"
#include "inet/queueing/contract/IActivePacketSink.h"
#include "inet/queueing/contract/IPacketQueue.h"

#include "../LoRa/LoRaRadio.h"
#include "../LoRa/LoRaMacFrame_m.h"

#include "../helpers/CustomPacketQueue.h"
#include "../helpers/IncompletePacketList.h"
#include "../helpers/LatestMessageIdMap.h"

namespace rlora {

using namespace inet;

class LoRaCSMA : public MacProtocolBase, public IMacProtocol, public queueing::IActivePacketSink
{
protected:
    /**
     * @name Configuration parameters
     */
    //@{
    FcsMode fcsMode;
    bool useAck = true;
    double bitrate = NaN;
    B headerLength = B(-1);
    B ackLength = B(-1);
    simtime_t ackTimeout = -1;
    simtime_t slotTime = -1;
    simtime_t sifsTime = -1;
    simtime_t difsTime = -1;
    int retryLimit = -1;
    int cwMin = -1;
    int cwMax = -1;
    int cwMulticast = -1;
    //@}

    IRadio *radio = nullptr;
    LoRaRadio *loRaRadio;
    MacAddress address;
    IncompletePacketList incompletePacketList;
    CustomPacketQueue packetQueue;
    LatestMessageIdMap latestMessageIdMap;
    cMessage *nodeAnnounce = nullptr;
    cMessage *transmitSwitchDone = nullptr;
    cMessage *receptionStated = nullptr;
    int nodeId = -1;

    simtime_t measurementInterval = 1.0;  // 1 second
    long bytesReceivedInInterval = 0;
    long effectiveBytesReceivedInInterval = 0;
    cMessage *throughputTimer = nullptr;
    simsignal_t throughputSignal;
    simsignal_t effectiveThroughputSignal;
    simsignal_t timeInQueue;

    map<int, SimTime> idToAddedTimeMap;
    /**
     * @name LoRaCSMA state variables
     * Various state information checked and modified according to the state machine.
     */
    //@{
    enum State
    {
        SWITCHING, LISTENING, DEFER, BACKOFF, TRANSMIT, RECEIVE,
    };

    IRadio::TransmissionState transmissionState = IRadio::TRANSMISSION_STATE_UNDEFINED;
    IRadio::ReceptionState receptionState = IRadio::RECEPTION_STATE_UNDEFINED;

    cFSM fsm;

    /** Remaining backoff period in seconds */
    simtime_t backoffPeriod = -1;

    /** Number of frame retransmission attempts. */
    int retryCounter = -1;
    //@}

    /** @name Timer messages */
    //@{

    /** End of the backoff period */
    cMessage *endBackoff = nullptr;

    /** Timeout after the transmission of a Data frame */
    cMessage *endData = nullptr;

    /** Radio state change self message. Currently this is optimized away and sent directly */
    cMessage *mediumStateChange = nullptr;
    //@}

    /** @name Statistics */
    //@{
    long numRetry;
    long numSentWithoutRetry;
    long numGivenUp;
    long numCollision;
    long numSent;
    long numReceived;
    long numSentBroadcast;
    long numReceivedBroadcast;
    //@}

public:
    /**
     * @name Construction functions
     */
    //@{
    virtual ~LoRaCSMA();

    virtual MacAddress getAddress();
    void createBroadcastPacket(int packetSize, int messageId, int hopId, int source, bool retransmit);
    void announceNodeId(int respond);
    void handlePacket(Packet *packet);
    void retransmitPacket(FragmentedPacket fragmentedPacket);
    void turnOnReceiver(void);
    //@}

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

    virtual void receiveSignal(cComponent *source, simsignal_t signalID, intval_t value, cObject *details) override;

    virtual void encapsulate(Packet *frame);
    virtual void decapsulate(Packet *frame);
    //@}

    /**
     * @name Timer functions
     * @brief These functions have the side effect of starting the corresponding timers.
     */
    //@{

    virtual void invalidateBackoffPeriod();
    virtual bool isInvalidBackoffPeriod();
    virtual void generateBackoffPeriod();
    virtual void decreaseBackoffPeriod();
    virtual void scheduleBackoffTimer();
    virtual void cancelBackoffTimer();
    //@}

    /**
     * @name Frame transmission functions
     */
    //@{
    virtual void sendDataFrame();
    //@}

    /**
     * @name Utility functions
     */
    //@{
    virtual void finishCurrentTransmission();
    virtual Packet* getCurrentTransmission();
    virtual void resetTransmissionVariables();
    virtual void emitPacketDropSignal(Packet *frame, PacketDropReason reason, int limit = -1);

    virtual bool isMediumFree();
    virtual bool isReceiving();

    //@}

    // OperationalBase:
    virtual void handleStopOperation(LifecycleOperation *operation) override;
    virtual void handleCrashOperation(LifecycleOperation *operation) override;

    virtual void processUpperPacket();

public:
    // IActivePacketSink:
    virtual queueing::IPassivePacketSource* getProvider(cGate *gate) override;
    virtual void handleCanPullPacketChanged(cGate *gate) override;
    virtual void handlePullPacketProcessed(Packet *packet, cGate *gate, bool successful) override;
};

} /* namespace rlora */

#endif /* LORACSMA_LORACSMA_H_ */
