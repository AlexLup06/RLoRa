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

#ifndef LORAMESHROUTERTUNEDRTSCTS_V1_LORAMESHROUTERTUNEDRTSCTSV1_H_
#define LORAMESHROUTERTUNEDRTSCTS_V1_LORAMESHROUTERTUNEDRTSCTSV1_H_

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
#include "../helpers/MissionIdTracker.h"
#include "../helpers/TimeOfLastTrajectory.h"

using namespace inet;
using namespace physicallayer;

namespace rlora {

/**
 * Based on CSMA class
 */

class LoRaMeshRouterTunedRTSCTSv1 : public MacProtocolBase, public IMacProtocol, public queueing::IActivePacketSink
{

protected:
    double bitrate = NaN;
    int headerLength = -1;
    MacAddress address;

    enum State
    {
        SWITCHING, DEFER, BACKOFF, SEND_RTS, WAIT_CTS, TRANSMITING, SEND_CTS, LISTENING, RECEIVING, CW_CTS, AWAIT_TRANSMISSION
    };

    IRadio *radio = nullptr;
    IRadio::TransmissionState transmissionState = IRadio::TRANSMISSION_STATE_UNDEFINED;
    IRadio::ReceptionState receptionState = IRadio::RECEPTION_STATE_UNDEFINED;

    cFSM fsm;

    simsignal_t throughputSignal;
    simsignal_t effectiveThroughputSignal;
    simsignal_t timeInQueue;
    simsignal_t missionIdFragmentSent;
    simsignal_t missionIdRtsSent;
    simsignal_t receivedMissionId;
    simsignal_t timeOfLastTrajectorySignal;

    simtime_t measurementInterval = 1.0;
    simtime_t backoffPeriod = -1;
    simtime_t ctsCWPeriod = -1;
    simtime_t sifs = 0.002;
    simtime_t backoffFS = 0.021; // timeOnAir of Header + 0.001 puffer
    simtime_t ctsFS = 0.018; // timeOnAir of Header + 0.0005 puffer
    int cwCTS = 16;
    int cwBackoff = 8;

    long bytesReceivedInInterval = 0;
    long effectiveBytesReceivedInInterval = 0;
    int sizeOfFragment_CTSData = -1;
    int sourceOfRTS_CTSData = -1;
    int rtsSource = -1;
    int nodeId = -1;

    cMessage *mediumStateChange = nullptr;
    cMessage *endBackoff = nullptr;
    cMessage *droppedPacket = nullptr;
    cMessage *endTransmission = nullptr;
    cMessage *transmitSwitchDone = nullptr;
    cMessage *nodeAnnounce = nullptr;
    cMessage *CTSWaitTimeout = nullptr;
    cMessage *receivedCTS = nullptr;
    cMessage *endOngoingMsg = nullptr;
    cMessage *initiateCTS = nullptr;
    cMessage *transmissionStartTimeout = nullptr;
    cMessage *gotNewMessagToSend = nullptr;
    cMessage *throughputTimer = nullptr;
    cMessage *ctsCWTimeout = nullptr;
    cMessage *moreMessagesToSend = nullptr;
    cMessage *transmissionEndTimeout = nullptr;
    cMessage *shortWait = nullptr;

    IncompletePacketList incompleteMissionPktList;
    IncompletePacketList incompleteNeighbourPktList;
    CustomPacketQueue packetQueue;
    LoRaRadio *loRaRadio;
    map<int, SimTime> idToAddedTimeMap;
    MissionIdTracker missionIdRtsSentTracker;
    MissionIdTracker missionIdFragmentSentTracker;
    TimeOfLastTrajectory timeOfLastTrajectory;

public:
    /**
     * @name Construction functions
     */
    //@{
    virtual ~LoRaMeshRouterTunedRTSCTSv1();
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
    void handleCTSTimeout();
    void handleCTS(Packet *pkt);

    virtual void receiveSignal(cComponent *source, simsignal_t signalID, intval_t value, cObject *details) override;

    //@}

    // backoff
    virtual void invalidateBackoffPeriod();
    virtual bool isInvalidBackoffPeriod();
    virtual void generateBackoffPeriod();
    virtual void decreaseBackoffPeriod();
    virtual void scheduleBackoffTimer();
    virtual void cancelBackoffTimer();

    // CTS cw period
    void invalidateCTSCWPeriod();
    bool isInvalidCTWCWPeriod();
    void generateCTWCWPeriod();
    void scheduleCTSCWTimer();
    void cancelCTSCWTimer();

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
    void retransmitPacket(FragmentedPacket fragmentedPacket);
    void announceNodeId(int respond);
    void sendCTS();
    void sendRTS();

    /**
     * @name Utility functions
     */
    virtual void finishCurrentTransmission();
    virtual Packet* getCurrentTransmission();
    virtual Packet* encapsulate(Packet *msg);
    virtual Packet* decapsulate(Packet *frame);
    double predictOngoingMsgTime(int packetBytes);

    virtual bool isMediumFree();
    virtual bool isReceiving();

    void turnOnReceiver(void);
    void turnOffReceiver(void);
    void turnOnTransmitter();

    void createBroadcastPacket(int payloadSize, int missionId, int source, bool retransmit);
    void createNeighbourPacket(int payloadSize, int source, bool retransmit);
    Packet* createHeader(int missionId, int source, int payloadSize, bool retransmit);
    Packet* createContinuousHeader(int messageId, int source, int payloadSize, bool retransmit);

    bool isOurCTS(cMessage *msg);
    bool isCTSForSameRTSSource(cMessage *msg);
    bool isPacketFromRTSSource(cMessage *msg);
    bool isInvalidCTSCWPeriod();
    bool isFreeToSend();
    bool withRTS();
    void clearRTSsource();
    void generateCTSCWPeriod();
    void setRTSsource(int rtsSourceId);
//    bool currentlyReceivingOurCTS();
};

} // namespace rlora

#endif /* LORAMESHROUTERTUNEDRTSCTS_V1_LORAMESHROUTERTUNEDRTSCTSV1_H_ */
