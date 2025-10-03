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

#include "LoRaMeshRouterRTSCTSv1.h"

#include "inet/common/ModuleAccess.h"
#include "inet/linklayer/common/Ieee802Ctrl.h"
#include "inet/linklayer/common/UserPriority.h"
#include "inet/linklayer/csmaca/CsmaCaMac.h"
#include "inet/common/ProtocolTag_m.h"
#include "inet/linklayer/common/MacAddressTag_m.h"
#include "inet/linklayer/csmaca/CsmaCaMac.h"
#include "inet/linklayer/common/InterfaceTag_m.h"

#include "../LoRa/LoRaTagInfo_m.h"
#include "../LoRaApp/LoRaRobotPacket_m.h"
#include "../helpers/generalHelpers.h"

#include "../messages/MessageInfoTag_m.h"
#include "../messages/BroadcastFragment_m.h"
#include "../messages/NodeAnnounce_m.h"
#include "../messages/BroadcastCTS_m.h"
#include "../messages/BroadcastHeader_m.h"
#include "../messages/BroadcastContinuousHeader_m.h"

namespace rlora {

Define_Module(LoRaMeshRouterRTSCTSv1);

LoRaMeshRouterRTSCTSv1::~LoRaMeshRouterRTSCTSv1()
{
}

/****************************************************************
 * Initialization functions.
 */
void LoRaMeshRouterRTSCTSv1::initialize(int stage)
{
    MacProtocolBase::initialize(stage);
    if (stage == INITSTAGE_LOCAL) {
        const char *addressString = par("address");
        if (!strcmp(addressString, "auto")) {
            address = MacAddress::generateAutoAddress();
            par("address").setStringValue(address.str().c_str());
        }
        else
            address.setAddress(addressString);

        // subscribe for the information of the carrier sense
        cModule *radioModule = getModuleFromPar<cModule>(par("radioModule"), this);
        radioModule->subscribe(IRadio::receptionStateChangedSignal, this);
        radioModule->subscribe(IRadio::transmissionStateChangedSignal, this);
        radioModule->subscribe(LoRaRadio::droppedPacket, this);
        radio = check_and_cast<IRadio*>(radioModule);
        loRaRadio = check_and_cast<LoRaRadio*>(radioModule);

        txQueue = getQueue(gate(upperLayerInGateId));
        packetQueue = CustomPacketQueue();

        nodeId = intuniform(0, 16777216); //16^6 -1

        mediumStateChange = new cMessage("mediumStateChange");
        droppedPacket = new cMessage("Dropped Packet");
        endTransmission = new cMessage("End Transmission");
        transmitSwitchDone = new cMessage("transmitSwitchDone");
        nodeAnnounce = new cMessage("Node Announce");
        CTSWaitTimeout = new cMessage("CTSWaitTimeout");
        receivedCTS = new cMessage("receivedCTS");
        throughputTimer = new cMessage("throughputTimer");
        endOngoingMsg = new cMessage("endOngoingMsg");
        initiateCTS = new cMessage("initiateCTS");
        endBackoff = new cMessage("endBackoff");
        transmissionStartTimeout = new cMessage("transmissionStartTimeout");
        gotNewMessagToSend = new cMessage("gotNewMessagToSend");
        ctsCWTimeout = new cMessage("ctsCWTimeout");
        moreMessagesToSend = new cMessage("moreMessagesToSend");
        transmissionEndTimeout = new cMessage("transmissionEndTimeout");
        shortWait = new cMessage("shortWait");

        throughputSignal = registerSignal("throughputBps");
        effectiveThroughputSignal = registerSignal("effectiveThroughputBps");
        timeInQueue = registerSignal("timeInQueue");
        missionIdRtsSent = registerSignal("missionIdRtsSent");
        missionIdFragmentSent = registerSignal("missionIdFragmentSent");
        receivedMissionId = registerSignal("receivedMissionId");
        timeOfLastTrajectorySignal = registerSignal("timeOfLastTrajectorySignal");

        scheduleAt(simTime() + measurementInterval, throughputTimer);
        scheduleAt(intuniform(0, 1000) / 1000.0, nodeAnnounce);
        fsm.setState(LISTENING); // We are always in Listening state
    }
    else if (stage == INITSTAGE_LINK_LAYER) {
        turnOnReceiver();
    }
}

void LoRaMeshRouterRTSCTSv1::finish()
{
    cancelAndDelete(mediumStateChange);
    cancelAndDelete(droppedPacket);
    cancelAndDelete(endTransmission);
    cancelAndDelete(transmitSwitchDone);
    cancelAndDelete(nodeAnnounce);
    cancelAndDelete(CTSWaitTimeout);
    cancelAndDelete(receivedCTS);
    cancelAndDelete(throughputTimer);
    cancelAndDelete(endOngoingMsg);
    cancelAndDelete(initiateCTS);
    cancelAndDelete(endBackoff);
    cancelAndDelete(transmissionStartTimeout);
    cancelAndDelete(gotNewMessagToSend);
    cancelAndDelete(ctsCWTimeout);
    cancelAndDelete(moreMessagesToSend);
    cancelAndDelete(transmissionEndTimeout);
    cancelAndDelete(shortWait);

    mediumStateChange = nullptr;
    droppedPacket = nullptr;
    endTransmission = nullptr;
    transmitSwitchDone = nullptr;
    nodeAnnounce = nullptr;
    CTSWaitTimeout = nullptr;
    receivedCTS = nullptr;
    throughputTimer = nullptr;
    endOngoingMsg = nullptr;
    initiateCTS = nullptr;
    endBackoff = nullptr;
    transmissionStartTimeout = nullptr;
    gotNewMessagToSend = nullptr;
    ctsCWTimeout = nullptr;
    moreMessagesToSend = nullptr;
    transmissionEndTimeout = nullptr;
    shortWait = nullptr;

    while (!packetQueue.isEmpty()) {
        auto *pkt = packetQueue.dequeuePacket();
        delete pkt;
    }

    currentTxFrame = nullptr;
}

void LoRaMeshRouterRTSCTSv1::configureNetworkInterface()
{
    // data rate
    networkInterface->setDatarate(bitrate);
    networkInterface->setMacAddress(address);

    // capabilities
    networkInterface->setMtu(std::numeric_limits<int>::quiet_NaN());
    networkInterface->setMulticast(true);
    networkInterface->setBroadcast(true);
    networkInterface->setPointToPoint(false);
}

queueing::IPassivePacketSource* LoRaMeshRouterRTSCTSv1::getProvider(cGate *gate)
{
    return (gate->getId() == upperLayerInGateId) ? txQueue.get() : nullptr;
}

// ================================================================================================
// Handle Messages and Signals
// ================================================================================================

void LoRaMeshRouterRTSCTSv1::handleWithFsm(cMessage *msg)
{
    EV << "Handlewigthfsms" << endl;
    EV << "Handlewigthfsms: " << msg << endl;
    EV << "CurrentTxFrame: " << currentTxFrame << endl;
    EV << "QUEUE: " << packetQueue.toString() << endl;
    EV << "We are in: " << fsm.getStateName() << endl;

    auto pkt = dynamic_cast<Packet*>(msg);

    if (msg == nodeAnnounce) {
        announceNodeId(0);
        scheduleAfter(5, nodeAnnounce);
    }

    if (msg == throughputTimer) {
        // Timer triggered
        emit(throughputSignal, bytesReceivedInInterval);  // bytes per interval
        emit(effectiveThroughputSignal, effectiveBytesReceivedInInterval);  // bytes per interval
        bytesReceivedInInterval = 0;  // reset counter
        effectiveBytesReceivedInInterval = 0;  // reset counter
        scheduleAt(simTime() + measurementInterval, throughputTimer);
        return;
    }

    FSMA_Switch(fsm){

    FSMA_State(SWITCHING)
    {
        FSMA_Enter(turnOnReceiver();EV<<"Switched To SWITCHING"<<endl;);
        FSMA_Event_Transition(able-to-listen,
                msg == mediumStateChange|| msg==shortWait,
                LISTENING,
        );
        FSMA_Event_Transition(we-got-rts-now--send-cts,
                msg==initiateCTS && isFreeToSend(),
                CW_CTS,
        );
    }

    FSMA_State(LISTENING)
    {
        FSMA_Enter(EV<<"Switched To LISTENING"<<endl;);
        FSMA_Event_Transition(able-to-receive,
                isReceiving(),
                RECEIVING,
        );
        FSMA_Event_Transition(medium-busy-waiting-it-out,
                !isMediumFree(),
                DEFER,
        );
        FSMA_Event_Transition(something-to-send-medium-free-start-backoff,
                currentTxFrame != nullptr && isMediumFree() && isFreeToSend(),
                BACKOFF,
        );
        FSMA_Event_Transition(we-got-rts-now--send-cts,
                msg==initiateCTS && isFreeToSend(),
                CW_CTS,
        );
    }
    FSMA_State(DEFER)
    {
        FSMA_Enter(EV<<"Switched To DEFER"<<endl;);
        FSMA_Event_Transition(something-to-send-medium-free-again,
                currentTxFrame != nullptr && isMediumFree() && isFreeToSend(),
                BACKOFF,
        );
        FSMA_Event_Transition(nothing-to-send-medium-free-again,
                currentTxFrame == nullptr && isMediumFree(),
                LISTENING,
        );
        FSMA_Event_Transition(able-to-receive-again,
                isReceiving(),
                RECEIVING,
        );
        FSMA_Event_Transition(we-got-rts-now--send-cts,
                msg==initiateCTS && isFreeToSend(),
                CW_CTS,
        );
    }
    FSMA_State(BACKOFF)
    {
        FSMA_Enter(scheduleBackoffTimer();EV<<"Switched To BACKOFF"<<endl;);
        FSMA_Event_Transition(backoff-finished-send-rts,
                msg == endBackoff && withRTS(),
                SEND_RTS,
                invalidateBackoffPeriod();
        );
        FSMA_Event_Transition(backoff-finished-message-without-rts-only-nodeannounce,
                msg == endBackoff && !withRTS(),
                TRANSMITING,
                invalidateBackoffPeriod();
        );
        FSMA_Event_Transition(receiving-msg-cancle-backoff-listen-now,
                isReceiving(),
                RECEIVING,
                cancelBackoffTimer();
                decreaseBackoffPeriod();
        );
        FSMA_Event_Transition(we-got-rts-now--send-cts,
                msg==initiateCTS && isFreeToSend(),
                CW_CTS,
                cancelBackoffTimer();
                decreaseBackoffPeriod();
        );
    }
    FSMA_State(SEND_RTS)
    {
        FSMA_Enter(turnOnTransmitter();EV<<"Switched To SEND_RTS"<<endl;);
        FSMA_Event_Transition(transmitter-is-ready-to-send,
                msg == transmitSwitchDone,
                SEND_RTS,
                sendRTS();
        );
        FSMA_Event_Transition(rts-was-sent-now-wait-cts,
                msg == endTransmission,
                WAIT_CTS,
        );
    }
    FSMA_State(WAIT_CTS)
    {
        FSMA_Enter(turnOnReceiver();EV<<"Switched To WAIT_CTS"<<endl;);
        FSMA_Event_Transition(we-didnt-get-cts-go-back-to-listening,
                msg == CTSWaitTimeout,
                SWITCHING,
                handleCTSTimeout();
                scheduleAfter(sifs,shortWait)

        );
        FSMA_Event_Transition(Listening-Receiving,
                isOurCTS(msg),
                TRANSMITING,
                handleCTS(pkt);
        );
    }

    FSMA_State(TRANSMITING)
    {
        FSMA_Enter(turnOnTransmitter();EV<<"Switched To TRANSMITING"<<endl;);
        FSMA_Event_Transition(send-data-now,
                msg == transmitSwitchDone,
                TRANSMITING,
                sendDataFrame();
        );
        FSMA_Event_Transition(finished-transmission-turn-to-receiver,
                msg == endTransmission,
                SWITCHING,
                finishCurrentTransmission();
        );
    }
    FSMA_State(CW_CTS)
    {
        FSMA_Enter(EV<<"Switched To CW_CTS"<<endl;scheduleCTSCWTimer(););
        FSMA_Event_Transition(had-to-wait-cw-to-send-cts,
                msg == ctsCWTimeout && !isReceiving(),
                SEND_CTS,
                invalidateCTSCWPeriod();
        );
        FSMA_Event_Transition(got-cts-sent-to-same-source-as-we-want-to-send-to,
                isCTSForSameRTSSource(msg),
                AWAIT_TRANSMISSION,
                invalidateCTSCWPeriod();
                cancelCTSCWTimer();
                scheduleAfter(sifs, transmissionStartTimeout);
        );
        FSMA_Event_Transition(got-packet-from-rts-source,
                isPacketFromRTSSource(msg),
                SWITCHING,
                invalidateCTSCWPeriod();
                cancelCTSCWTimer();
                handlePacket(pkt);
                scheduleAfter(sifs,shortWait);
        );
    }
    FSMA_State(SEND_CTS)
    {
        FSMA_Enter(turnOnTransmitter();EV<<"Switched To SEND_CTS"<<endl;);
        FSMA_Event_Transition(transmitter-on-now-send-cts,
                msg == transmitSwitchDone,
                SEND_CTS,
                sendCTS();
        );
        FSMA_Event_Transition(finished-cts-sending-turn-to-receiver,
                msg == endTransmission,
                AWAIT_TRANSMISSION,
        );
    }
    FSMA_State(AWAIT_TRANSMISSION)
    {
        FSMA_Enter(turnOnReceiver();EV<<"Switched To AWAIT_TRANSMISSION"<<endl;);
        FSMA_Event_Transition(source-didnt-get-cts-just-go-back-to-regular-listening,
                msg == transmissionStartTimeout && !isReceiving(),
                SWITCHING,
                clearRTSsource();
                cancelEvent(transmissionEndTimeout);
                scheduleAfter(sifs,shortWait);
        );
        FSMA_Event_Transition(received-packet-check-whether-from-rts-source-then-handle,
                isPacketFromRTSSource(msg),
                SWITCHING,
                handlePacket(pkt);
                cancelEvent(transmissionStartTimeout);
                cancelEvent(transmissionEndTimeout);
                scheduleAfter(sifs,shortWait);

        );
        // if we dont do this, we will be stuck here if a node get a message from a node other than the one from rts. should rarely happen
        FSMA_Event_Transition(got-some-random-message-just-remove-then-go-back-to-listening,
                msg==transmissionEndTimeout,
                SWITCHING,
                scheduleAfter(sifs,shortWait);
        );
    }
    FSMA_State(RECEIVING)
    {
        FSMA_Enter(EV<<"Switched To RECEIVING"<<endl;);
        FSMA_Event_Transition(received-message-handle-keep-listening,
                isLowerMessage(msg),
                SWITCHING,
                handlePacket(pkt);
                scheduleAfter(sifs,shortWait)
        );
        FSMA_Event_Transition(receive-below-sensitivity,
                msg == droppedPacket,
                LISTENING,
        );
    }
}

    if (fsm.getState() == LISTENING && isFreeToSend() && isMediumFree() && msg != initiateCTS) {
        if (currentTxFrame != nullptr) {
            handleWithFsm(moreMessagesToSend);
        }
        else if (!packetQueue.isEmpty() && currentTxFrame == nullptr) {
            EV << "Dequeuing and deleting current tx" << endl;
            currentTxFrame = packetQueue.dequeuePacket();
            handleWithFsm(moreMessagesToSend);
        }
    }
//    if (fsm.getState() == RECEIVING || fsm.getState() == WAIT_CTS || fsm.getState() == CW_CTS || fsm.getState() == AWAIT_TRANSMISSION) {

}

void LoRaMeshRouterRTSCTSv1::handlePacket(Packet *packet)
{
    bytesReceivedInInterval += packet->getByteLength();
    Packet *messageFrame = decapsulate(packet);
    auto chunk = messageFrame->peekAtFront<inet::Chunk>();
    EV << "handlePacket: " << packet->getName() << endl;

    if (auto msg = dynamic_cast<const BroadcastHeader*>(chunk.get())) {
        EV << "GOT BroadcastHeader" << endl;

        int messageId = msg->getMessageId();
        int source = msg->getSource();
        int missionId = msg->getMissionId();
        bool isMissionMsg = msg->getRetransmit();

        // we only care about this message if it is a newer Neighbor Message. If it is an older one then we do not care
        if (!isMissionMsg && !incompleteNeighbourPktList.isNewIdHigher(source, messageId)) {
            delete packet;
            return;
        }

        // we only care about this message if it is a newer Mission Message. If it is an older one then we do not care.
        // For continuous Header we care, because we are able to get fragments in different sequence and we only want
        // to get the ones which we still not have gotten
        if (isMissionMsg && !incompleteMissionPktList.isNewIdHigher(source, messageId)) {
            delete packet;
            return;
        }

        FragmentedPacket incompletePacket;
        incompletePacket.messageId = messageId;
        incompletePacket.missionId = missionId;
        incompletePacket.sourceNode = source;
        incompletePacket.size = msg->getSize();
        incompletePacket.lastHop = msg->getHop();
        incompletePacket.received = 0;
        incompletePacket.corrupted = false;
        incompletePacket.retransmit = msg->getRetransmit();

        if (isMissionMsg) {
            incompleteMissionPktList.addPacket(incompletePacket);
            incompleteMissionPktList.updatePacketId(source, missionId);
        }
        else {
            incompleteNeighbourPktList.addPacket(incompletePacket);
            incompleteNeighbourPktList.updatePacketId(source, messageId);
        }

        int sizeOfFragment = msg->getSize() > 255 - BROADCAST_FRAGMENT_META_SIZE ? 255 : msg->getSize() + BROADCAST_FRAGMENT_META_SIZE;
        scheduleAfter(0, initiateCTS);
        sizeOfFragment_CTSData = sizeOfFragment;
        sourceOfRTS_CTSData = msg->getHop();
        setRTSsource(msg->getHop());
    }
    else if (auto msg = dynamic_cast<const BroadcastContinuousHeader*>(chunk.get())) {
        EV << "GOT BroadcastContinuousHeader" << endl;

        // We can only have gotten this BroadcastContinuousHeader if we are not receiving anything else soon.
        // Otherwise we would have sent CTS and the node would have gotten it

        int messageId = msg->getMessageId();
        int source = msg->getSource();
        int missionId = msg->getMissionId();

        // TODO: need to check if we have already gotten the header for that message. It can be from a different hop: again, we can get
        // fragments from different nodes and not in order
        if (incompleteMissionPktList.isNewIdSame(source, missionId) || incompleteNeighbourPktList.isNewIdSame(source, messageId)) {
            scheduleAfter(0, initiateCTS);
            sizeOfFragment_CTSData = msg->getPayloadSizeOfNextFragment(); // TODO
            sourceOfRTS_CTSData = msg->getHopId();
            setRTSsource(msg->getHopId());
        }
    }
    else if (auto msg = dynamic_cast<const BroadcastFragment*>(chunk.get())) {
        EV << "GOT BroadcastFragment" << endl;
        effectiveBytesReceivedInInterval += packet->getByteLength() - BROADCAST_FRAGMENT_META_SIZE;

        int messageId = msg->getMessageId();
        int source = msg->getSource();
        int missionId = msg->getMissionId();
        bool isMissionMsg = missionId > 0;

        // We do check if we have started this packet with a header in the function
        Result result;
        if (isMissionMsg)
            result = incompleteMissionPktList.addToIncompletePacket(msg);
        else
            result = incompleteNeighbourPktList.addToIncompletePacket(msg);

        if (result.isComplete) {
            if (result.sendUp) {
                cMessage *readyMsg = new cMessage("Ready");
                sendUp(readyMsg);
                retransmitPacket(result.completePacket);
            }
            if (isMissionMsg)
                incompleteMissionPktList.removePacketById(missionId);
            else {
                incompleteNeighbourPktList.removePacketById(messageId);
                simtime_t time = timeOfLastTrajectory.calcAgeOfInformation(source, simTime());
                emit(timeOfLastTrajectorySignal, time);
                timeOfLastTrajectory.addTime(source, simTime());
            }
        }
    }
    else if (auto msg = dynamic_cast<const BroadcastCTS*>(chunk.get())) {
        // we are only here if we did not request the CTS. We need to wait out the message before we are able to send
        ASSERT(msg->getHopId() != nodeId);
        int size = msg->getSizeOfFragment();
        if (endOngoingMsg->isScheduled()) {
            cancelEvent(endOngoingMsg);
        }
        ASSERT(size > 0);
        scheduleAfter(predictOngoingMsgTime(size) + sifs, endOngoingMsg);
    }
    delete packet;
}

void LoRaMeshRouterRTSCTSv1::handleSelfMessage(cMessage *msg)
{
    EV << "handleSelfMessage" << endl;
    handleWithFsm(msg);
}

void LoRaMeshRouterRTSCTSv1::handleUpperPacket(Packet *packet)
{
    EV << "handleUpperPacket" << endl;
// add header to queue
    const auto &payload = packet->peekAtFront<LoRaRobotPacket>();
    bool isMission = payload->isMission();
    createBroadcastPacket(packet->getByteLength(), -1, -1, isMission);

    if (currentTxFrame == nullptr) {
        Packet *packetToSend = packetQueue.dequeuePacket();
        currentTxFrame = packetToSend;
    }
    EV << "Before handle with fsm" << endl;
    handleWithFsm(gotNewMessagToSend);
    EV << "After handle with fsm" << endl;
    delete packet;
    EV << "Before delete" << endl;
}

void LoRaMeshRouterRTSCTSv1::handleLowerPacket(Packet *msg)
{
    EV << "handleLowerPacket" << endl;
    if (fsm.getState() == RECEIVING || fsm.getState() == WAIT_CTS || fsm.getState() == CW_CTS || fsm.getState() == AWAIT_TRANSMISSION) {
        handleWithFsm(msg);
    }
    else {
        EV << "Received MSG while not in RECEIVING/WAIT_CTS state" << endl;
        delete msg;
    }
}

void LoRaMeshRouterRTSCTSv1::handleCanPullPacketChanged(cGate *gate)
{
    EV << "handleCanPullPacketChanged" << endl;
    Enter_Method("handleCanPullPacketChanged");
    Packet *packet = dequeuePacket();
    handleUpperMessage(packet);
}

void LoRaMeshRouterRTSCTSv1::handlePullPacketProcessed(Packet *packet, cGate *gate, bool successful)
{
    EV << "handlePullPacketProcessed" << endl;
    Enter_Method("handlePullPacketProcessed");
    throw cRuntimeError("Not supported callback");
}

void LoRaMeshRouterRTSCTSv1::receiveSignal(cComponent *source, simsignal_t signalID, intval_t value, cObject *details)
{
    EV << "receiveSignal" << endl;
    Enter_Method_Silent();
    if (signalID == IRadio::receptionStateChangedSignal) {
        IRadio::ReceptionState newRadioReceptionState = (IRadio::ReceptionState) value;
        handleWithFsm(mediumStateChange);
        receptionState = newRadioReceptionState;
    }
    else if (signalID == LoRaRadio::droppedPacket) {
        radio->setRadioMode(IRadio::RADIO_MODE_RECEIVER);
        handleWithFsm(droppedPacket);
    }
    else if (signalID == IRadio::transmissionStateChangedSignal) {
        IRadio::TransmissionState newRadioTransmissionState = (IRadio::TransmissionState) value;
        if (transmissionState == IRadio::TRANSMISSION_STATE_TRANSMITTING && newRadioTransmissionState == IRadio::TRANSMISSION_STATE_IDLE) {
            transmissionState = newRadioTransmissionState;
            handleWithFsm(endTransmission);
        }

        if (transmissionState == IRadio::TRANSMISSION_STATE_UNDEFINED && newRadioTransmissionState == IRadio::TRANSMISSION_STATE_IDLE) {
            transmissionState = newRadioTransmissionState;
            handleWithFsm(transmitSwitchDone);
        }
        transmissionState = newRadioTransmissionState;
    }
    EV << "endReceiveSignal" << endl;
}

// ================================================================================================
// ================================================================================================

// ================================================================================================
// Sending Functions
// ================================================================================================

void LoRaMeshRouterRTSCTSv1::sendDataFrame()
{
    EV << "sendDataFrame" << endl;
    auto frameToSend = getCurrentTransmission();
    EV << "sendDataFrame: " << frameToSend << endl;
    sendDown(frameToSend->dup());

    if (idToAddedTimeMap.find(frameToSend->getId()) != idToAddedTimeMap.end()) {
        SimTime previousTime = idToAddedTimeMap[frameToSend->getId()];
        SimTime delta = simTime() - previousTime;
        emit(timeInQueue, delta);
    }

    auto typeTag = frameToSend->getTag<MessageInfoTag>();
    int missionId = typeTag->getMissionId();
    if (typeTag->isNeighbourMsg())
        return;

    if (typeTag->isHeader()) {
        if (!missionIdRtsSentTracker.contains(missionId)) {
            // We are here if we want to the send RTS for the first time for MissionId
            emit(missionIdRtsSent, missionId);
            missionIdRtsSentTracker.add(missionId);
            return;
        }
    }
    else {
        // We are only here if we send first fragment of message
        if (!missionIdFragmentSentTracker.contains(missionId)) {
            {
                emit(missionIdFragmentSent, missionId);
                missionIdFragmentSentTracker.add(missionId);

            }
        }
    }
}

void LoRaMeshRouterRTSCTSv1::announceNodeId(int respond)
{
    EV << "announceNodeId" << endl;
    auto nodeAnnouncePacket = new Packet("NodeAnnouncePacket");
    auto nodeAnnouncePayload = makeShared<NodeAnnounce>();
    nodeAnnouncePayload->setChunkLength(B(NODE_ANNOUNCE_SIZE));
    nodeAnnouncePayload->setLastHop(nodeId);
    nodeAnnouncePayload->setNodeId(nodeId);
    nodeAnnouncePayload->setRespond(respond);

    nodeAnnouncePacket->insertAtBack(nodeAnnouncePayload);
    nodeAnnouncePacket->addTagIfAbsent<PacketProtocolTag>()->setProtocol(&Protocol::apskPhy);

    auto messageInfoTag = nodeAnnouncePacket->addTagIfAbsent<MessageInfoTag>();
    messageInfoTag->setIsNeighbourMsg(false);
    messageInfoTag->setWithRTS(false);
    messageInfoTag->setIsNodeAnnounce(true);

    auto fragmentEncap = encapsulate(nodeAnnouncePacket);
    packetQueue.enqueueNodeAnnounce(fragmentEncap);
}

void LoRaMeshRouterRTSCTSv1::retransmitPacket(FragmentedPacket fragmentedPacket)
{
    EV << "retransmitPacket" << endl;
// jedes feld muss gleich sein vom fragemntierten packet außer lastHop
    if (!fragmentedPacket.retransmit || fragmentedPacket.sourceNode == nodeId) {
        return;
    }
    emit(receivedMissionId, fragmentedPacket.missionId);
    createBroadcastPacket(fragmentedPacket.size, fragmentedPacket.missionId, fragmentedPacket.sourceNode, fragmentedPacket.retransmit);
}

// ================================================================================================
// ================================================================================================

// ================================================================================================
// RTS/CTS Mechanism
// ================================================================================================

// We do not have to do anything else with cts
void LoRaMeshRouterRTSCTSv1::handleCTS(Packet *pkt)
{
    EV << "handleCTS" << endl;
    cancelEvent(CTSWaitTimeout);
    delete pkt;
    EV << "END handleCTS" << endl;
}

// Put packet back to queue at front and currentTxFrame is the header again, to try once again. After 3 tries delete the packet
void LoRaMeshRouterRTSCTSv1::handleCTSTimeout()
{
    EV << "handleCTSTimeout" << endl;
    auto frameToSend = getCurrentTransmission();
    auto infoTag = frameToSend->getTagForUpdate<MessageInfoTag>();
    auto frag = frameToSend->peekAtFront<BroadcastFragment>();

    int newTries = infoTag->getTries() + 1;
    infoTag->setTries(newTries);
    if (newTries >= 3) {
        deleteCurrentTxFrame();
        if (!packetQueue.isEmpty()) {
            currentTxFrame = packetQueue.dequeuePacket();
        }
        EV << "PRE END handleCTSTimeout" << endl;
        return;
    }

    packetQueue.enqueuePacketAtPosition(frameToSend, 0);

    ASSERT(frag != nullptr);
    ASSERT(infoTag->getPayloadSize() != -1);
    if (infoTag->getHasRegularHeader()) {
        currentTxFrame = createHeader(frag->getMissionId(), frag->getSource(), infoTag->getPayloadSize(), !infoTag->isNeighbourMsg());
    }
    else {
        currentTxFrame = createContinuousHeader(frag->getMissionId(), frag->getSource(), infoTag->getPayloadSize(), !infoTag->isNeighbourMsg());
    }
    EV << "END handleCTSTimeout" << endl;
}

bool LoRaMeshRouterRTSCTSv1::withRTS()
{
    EV << "withRTS" << endl;
    auto frameToSend = getCurrentTransmission();
    auto infoTag = frameToSend->getTag<MessageInfoTag>();
    return infoTag->getWithRTS();
}

bool LoRaMeshRouterRTSCTSv1::isOurCTS(cMessage *msg)
{
    EV << "isOurCTS: " << msg << endl;
    auto pkt = dynamic_cast<Packet*>(msg);
    if (pkt != nullptr) {
        Packet *messageFrame = decapsulate(pkt);
        auto chunk = messageFrame->peekAtFront<inet::Chunk>();
        if (auto msg = dynamic_cast<const BroadcastCTS*>(chunk.get())) {
            EV << "We got CTS" << endl;
            if (msg->getHopId() == nodeId) {
                return true;
            }
        }
    }
    EV << "END isOurCTS" << endl;
    if (!msg->isSelfMessage()) {
        delete msg;
    }
    return false;
}

bool LoRaMeshRouterRTSCTSv1::isCTSForSameRTSSource(cMessage *msg)
{
    EV << "isCTSForSameRTSSource: " << msg << endl;

    auto pkt = dynamic_cast<Packet*>(msg);
    if (pkt != nullptr) {
        Packet *messageFrame = decapsulate(pkt);
        auto chunk = messageFrame->peekAtFront<inet::Chunk>();
        if (auto msg = dynamic_cast<const BroadcastCTS*>(chunk.get())) {
            if (msg->getHopId() == rtsSource) {
                EV << "We got CTS addressed to the same node that we want to send to. Stop our CW and listen now for the packet" << endl;
                return true;
            }
        }
    }
    EV << "END isCTSForSameRTSSource" << endl;
    return false;
}

void LoRaMeshRouterRTSCTSv1::sendRTS()
{
    EV << "sendRTS" << endl;
    auto header = getCurrentTransmission();
    auto typeTag = header->getTag<MessageInfoTag>();
    ASSERT(typeTag->isHeader());

    // TODO +ctsFS
    scheduleAfter(ctsFS * cwCTS + sifs + predictOngoingMsgTime(header->getByteLength()) + ctsFS, CTSWaitTimeout);
    sendDown(header->dup());
    delete header;

    ASSERT(!packetQueue.isEmpty());
    currentTxFrame = packetQueue.dequeuePacket();
}

void LoRaMeshRouterRTSCTSv1::sendCTS()
{
    EV << "sendCTS" << endl;
    auto ctsPacket = new Packet("BroadcastCTS");
    auto ctsPayload = makeShared<BroadcastCTS>();

    ASSERT(sizeOfFragment_CTSData > 0);
    ASSERT(source_CTSData > 0);
    ctsPayload->setChunkLength(B(BROADCAST_CTS));
    ctsPayload->setSizeOfFragment(sizeOfFragment_CTSData);
    ctsPayload->setHopId(sourceOfRTS_CTSData);

    ctsPacket->insertAtBack(ctsPayload);
    ctsPacket->addTagIfAbsent<PacketProtocolTag>()->setProtocol(&Protocol::apskPhy);
    ctsPacket->addTagIfAbsent<MessageInfoTag>()->setIsNeighbourMsg(false);
    encapsulate(ctsPacket);
    sendDown(ctsPacket->dup());
    delete ctsPacket;

    // After we send CTS, we need to receive message within ctsFS + sifs otherwise we assume there will be no message
    scheduleAfter(ctsFS + sifs, transmissionStartTimeout);
    scheduleAfter(ctsFS + sifs + predictOngoingMsgTime(sizeOfFragment_CTSData), transmissionEndTimeout);
    sizeOfFragment_CTSData = -1;
    sourceOfRTS_CTSData = -1;
}

void LoRaMeshRouterRTSCTSv1::clearRTSsource()
{
    EV << "clearRTSsource" << endl;
    rtsSource = -1;
}

void LoRaMeshRouterRTSCTSv1::setRTSsource(int rtsSourceId)
{
    EV << "setRTSsource" << endl;
    rtsSource = rtsSourceId;
}

bool LoRaMeshRouterRTSCTSv1::isPacketFromRTSSource(cMessage *msg)
{
    EV << "isPacketFromRTSSource" << endl;
    if (!isLowerMessage(msg)) {
        return false;
    }

    auto packet = dynamic_cast<Packet*>(msg->dup());
    EV << "Tags packet: " << packet->getTags() << endl;
    // TODO
    Packet *messageFrame = decapsulate(packet);
    auto chunk = messageFrame->peekAtFront<inet::Chunk>();

    EV << "messageFrame packet: " << messageFrame->getTags() << endl;

    if (auto fragment = dynamic_cast<const BroadcastFragment*>(chunk.get())) {
        EV << "Tags fragment: " << fragment->getNumTags() << endl;
        auto infoTag = packet->getTag<MessageInfoTag>();
        if (infoTag->getHopId() == rtsSource) {
            clearRTSsource();
            delete packet;
            return true;
        }
    }
    delete packet;
    delete msg;
    return false;
}

// ================================================================================================
// ================================================================================================

// ================================================================================================
// Packet Creation
// ================================================================================================

void LoRaMeshRouterRTSCTSv1::createBroadcastPacket(int payloadSize, int missionId, int source, bool retransmit)
{
    EV << "createBroadcastPacket" << endl;
    if (source == -1) {
        source = nodeId;
    }

    Packet *headerPaket = createHeader(missionId, source, payloadSize, retransmit);

    if (missionId == -1) {
        missionId = headerPaket->getId();
    }
    int messageId = headerPaket->getId();

    bool trackQueueTime = packetQueue.enqueuePacket(headerPaket);
    if (trackQueueTime) {
        // only track queue time for packets that were inserted at the back
        idToAddedTimeMap[headerPaket->getId()] = simTime();
    }

    int i = 0;
    while (payloadSize > 0) {

        if (i > 0) {
            Packet *continuousHeader = createContinuousHeader(missionId, source, payloadSize, retransmit);
            packetQueue.enqueuePacket(continuousHeader);
        }

        auto fragmentPacket = new Packet("BroadcastFragmentPkt");
        auto fragmentPayload = makeShared<BroadcastFragment>();

        int currentPayloadSize = 0;
        if (payloadSize + BROADCAST_FRAGMENT_META_SIZE > 255) {
            currentPayloadSize = 255 - BROADCAST_FRAGMENT_META_SIZE;
            fragmentPayload->setChunkLength(B(255));
            fragmentPayload->setPayloadSize(currentPayloadSize);
            payloadSize = payloadSize - currentPayloadSize;
        }
        else {
            currentPayloadSize = payloadSize;
            fragmentPayload->setChunkLength(B(currentPayloadSize + BROADCAST_FRAGMENT_META_SIZE));
            fragmentPayload->setPayloadSize(currentPayloadSize);
            payloadSize = 0;
        }

        fragmentPayload->setMissionId(missionId);
        fragmentPayload->setMessageId(messageId);
        fragmentPayload->setSource(source);
        fragmentPayload->setFragmentId(i++);
        fragmentPacket->insertAtBack(fragmentPayload);
        fragmentPacket->addTagIfAbsent<PacketProtocolTag>()->setProtocol(&Protocol::apskPhy);

        auto messageInfoTag = fragmentPacket->addTagIfAbsent<MessageInfoTag>();
        messageInfoTag->setIsNeighbourMsg(!retransmit);
        messageInfoTag->setMissionId(missionId);
        messageInfoTag->setIsHeader(false);
        messageInfoTag->setHopId(nodeId);
        messageInfoTag->setHasUsefulData(true);
        messageInfoTag->setPayloadSize(currentPayloadSize);

        encapsulate(fragmentPacket);

        packetQueue.enqueuePacket(fragmentPacket);
        if (trackQueueTime) {
            // only track queue time for packets that were inserted at the back
            idToAddedTimeMap[fragmentPacket->getId()] = simTime();
        }
    }
}

Packet* LoRaMeshRouterRTSCTSv1::createHeader(int missionId, int source, int payloadSize, bool retransmit)
{
    EV << "createHeader" << endl;

    Packet *headerPaket = new Packet("BroadcastHeaderPkt");

    if (missionId == -1) {
        missionId = headerPaket->getId();
    }

    auto headerPayload = makeShared<BroadcastHeader>();
    headerPayload->setChunkLength(B(BROADCAST_HEADER_SIZE));
    headerPayload->setSize(payloadSize);
    headerPayload->setMissionId(missionId);
    headerPayload->setMessageId(headerPaket->getId());
    headerPayload->setSource(source);
    headerPayload->setHop(nodeId);
    headerPayload->setRetransmit(retransmit);
    headerPaket->insertAtBack(headerPayload);
    headerPaket->addTagIfAbsent<PacketProtocolTag>()->setProtocol(&Protocol::apskPhy);

    auto messageInfoTag = headerPaket->addTagIfAbsent<MessageInfoTag>();
    messageInfoTag->setIsNeighbourMsg(!retransmit);
    messageInfoTag->setMissionId(missionId);
    messageInfoTag->setIsHeader(true);

    encapsulate(headerPaket);
    return headerPaket;
}

Packet* LoRaMeshRouterRTSCTSv1::createContinuousHeader(int missionId, int source, int payloadSize, bool retransmit)
{
    EV << "createContinuousHeader" << endl;
    Packet *headerPaket = new Packet("BroadcastContinuousHeader");

    if (missionId == -1) {
        missionId = headerPaket->getId();
    }

    auto headerPayload = makeShared<BroadcastContinuousHeader>();
    headerPayload->setChunkLength(B(BROADCAST_CONTINIOUS_HEADER));
    headerPayload->setPayloadSizeOfNextFragment(payloadSize + BROADCAST_FRAGMENT_META_SIZE);
    headerPayload->setHopId(nodeId);
    headerPayload->setSource(source);
    headerPayload->setMessageId(headerPaket->getId());
    headerPayload->setMissionId(missionId);
    headerPaket->insertAtBack(headerPayload);
    headerPaket->addTagIfAbsent<PacketProtocolTag>()->setProtocol(&Protocol::apskPhy);

    auto messageInfoTag = headerPaket->addTagIfAbsent<MessageInfoTag>();
    messageInfoTag->setIsNeighbourMsg(!retransmit);
    messageInfoTag->setMissionId(missionId);
    messageInfoTag->setIsHeader(true);

    encapsulate(headerPaket);
    return headerPaket;
}

Packet* LoRaMeshRouterRTSCTSv1::encapsulate(Packet *msg)
{
    EV << "encapsulate" << endl;
    auto frame = makeShared<LoRaMacFrame>();
    frame->setChunkLength(B(0));
    msg->setArrival(msg->getArrivalModuleId(), msg->getArrivalGateId());

    auto tag = msg->addTagIfAbsent<LoRaTag>();
    tag->setBandwidth(loRaRadio->loRaBW);
    tag->setCenterFrequency(loRaRadio->loRaCF);
    tag->setSpreadFactor(loRaRadio->loRaSF);
    tag->setCodeRendundance(loRaRadio->loRaCR);
    tag->setPower(mW(math::dBmW2mW(loRaRadio->loRaTP)));

    frame->setTransmitterAddress(address);
    frame->setLoRaTP(loRaRadio->loRaTP);
    frame->setLoRaCF(loRaRadio->loRaCF);
    frame->setLoRaSF(loRaRadio->loRaSF);
    frame->setLoRaBW(loRaRadio->loRaBW);
    frame->setLoRaCR(loRaRadio->loRaCR);
    frame->setSequenceNumber(0);
    frame->setReceiverAddress(MacAddress::BROADCAST_ADDRESS);

    frame->setLoRaUseHeader(tag->getUseHeader());

    msg->insertAtFront(frame);

    return msg;
}

Packet* LoRaMeshRouterRTSCTSv1::decapsulate(Packet *frame)
{
    EV << "decapsulate" << endl;
    auto loraHeader = frame->popAtFront<LoRaMacFrame>();
    frame->addTagIfAbsent<MacAddressInd>()->setSrcAddress(loraHeader->getTransmitterAddress());
    frame->addTagIfAbsent<MacAddressInd>()->setDestAddress(loraHeader->getReceiverAddress());
    frame->addTagIfAbsent<InterfaceInd>()->setInterfaceId(networkInterface->getInterfaceId());

    return frame;
}

// ================================================================================================
// ================================================================================================

// ================================================================================================
// Backoff Period
// ================================================================================================

void LoRaMeshRouterRTSCTSv1::invalidateBackoffPeriod()
{
    EV << "invalidateBackoffPeriod" << endl;
    backoffPeriod = -1;
}

bool LoRaMeshRouterRTSCTSv1::isInvalidBackoffPeriod()
{
    EV << "isInvalidBackoffPeriod" << endl;
    return backoffPeriod == -1;
}

void LoRaMeshRouterRTSCTSv1::generateBackoffPeriod()
{
    EV << "generateBackoffPeriod" << endl;
    int slots = intrand(cwBackoff);
    EV << "slots: " << slots << endl;
    backoffPeriod = slots * backoffFS;
}

void LoRaMeshRouterRTSCTSv1::scheduleBackoffTimer()
{
    EV << "scheduleBackoffTimer" << endl;
    if (isInvalidBackoffPeriod())
        generateBackoffPeriod();
    EV << "backoffPeriod: " << backoffPeriod << endl;
    scheduleAfter(backoffPeriod, endBackoff);
}

void LoRaMeshRouterRTSCTSv1::decreaseBackoffPeriod()
{
    EV << "decreaseBackoffPeriod" << endl;
    simtime_t elapsedBackoffTime = simTime() - endBackoff->getSendingTime();
    backoffPeriod -= ((int) (elapsedBackoffTime / backoffFS)) * backoffFS;
    ASSERT(backoffPeriod >= 0);
}

void LoRaMeshRouterRTSCTSv1::cancelBackoffTimer()
{
    EV << "cancelBackoffTimer" << endl;
    cancelEvent(endBackoff);
}

// ================================================================================================
// ================================================================================================

// ================================================================================================
// CTS CW Period
// ================================================================================================

void LoRaMeshRouterRTSCTSv1::invalidateCTSCWPeriod()
{
    EV << "invalidateCTSCWPeriod" << endl;
    ctsCWPeriod = -1;
}

bool LoRaMeshRouterRTSCTSv1::isInvalidCTSCWPeriod()
{
    EV << "isInvalidCTSCWPeriod" << endl;
    return ctsCWPeriod == -1;
}

void LoRaMeshRouterRTSCTSv1::generateCTSCWPeriod()
{
    EV << "generateCTSCWPeriod" << endl;
    int slots = intrand(cwCTS);
    EV << "slots: " << slots << endl;
    ctsCWPeriod = slots * ctsFS;
}

void LoRaMeshRouterRTSCTSv1::scheduleCTSCWTimer()
{
    EV << "scheduleCTSCWTimer" << endl;
    if (isInvalidCTSCWPeriod())
        generateCTSCWPeriod();
    EV << "ctsCWPeriod: " << ctsCWPeriod << endl;
    scheduleAfter(ctsCWPeriod, ctsCWTimeout);
}

void LoRaMeshRouterRTSCTSv1::cancelCTSCWTimer()
{
    EV << "cancelCTSCWTimer" << endl;
    cancelEvent(ctsCWTimeout);
}

// ================================================================================================
// ================================================================================================

// ================================================================================================
// Radio Utils
// ================================================================================================

bool LoRaMeshRouterRTSCTSv1::isMediumFree()
{
    EV << "isMediumFree" << endl;
    return radio->getReceptionState() == IRadio::RECEPTION_STATE_IDLE;
}

bool LoRaMeshRouterRTSCTSv1::isReceiving()
{
    EV << "isReceiving" << endl;
    return radio->getReceptionState() == IRadio::RECEPTION_STATE_RECEIVING;
}

void LoRaMeshRouterRTSCTSv1::turnOnReceiver()
{
// this makes receiverPart go to idle no matter what
    EV << "turnOnReceiver" << endl;
    LoRaRadio *loraRadio;
    loraRadio = check_and_cast<LoRaRadio*>(radio);
    loraRadio->setRadioMode(IRadio::RADIO_MODE_RECEIVER);
}
void LoRaMeshRouterRTSCTSv1::turnOnTransmitter()
{
// this makes receiverPart go to idle no matter what
    EV << "turnOnTransmitter" << endl;
    LoRaRadio *loraRadio;
    loraRadio = check_and_cast<LoRaRadio*>(radio);
    loraRadio->setRadioMode(IRadio::RADIO_MODE_TRANSMITTER);
}

void LoRaMeshRouterRTSCTSv1::turnOffReceiver()
{
    EV << "turnOffReceiver" << endl;
    LoRaRadio *loraRadio;
    loraRadio = check_and_cast<LoRaRadio*>(radio);
    loraRadio->setRadioMode(IRadio::RADIO_MODE_SLEEP);
}

// ================================================================================================
// ================================================================================================

// ================================================================================================
// Helper functions
// ================================================================================================

void LoRaMeshRouterRTSCTSv1::finishCurrentTransmission()
{
    EV << "finishCurrentTransmission" << endl;
    deleteCurrentTxFrame();
}

Packet* LoRaMeshRouterRTSCTSv1::getCurrentTransmission()
{
    EV << "getCurrentTransmission" << endl;
    ASSERT(currentTxFrame != nullptr);
    return currentTxFrame;
}

MacAddress LoRaMeshRouterRTSCTSv1::getAddress()
{
    return address;
}

// We do not have to wait for a ongoing transmission by a hidden node. Have to check before going into backoff period
// AND we are not waiting for the start of a message
bool LoRaMeshRouterRTSCTSv1::isFreeToSend()
{
    EV << "isFreeToSend" << endl;
    return !endOngoingMsg->isScheduled() && !transmissionStartTimeout->isScheduled();
}

double LoRaMeshRouterRTSCTSv1::predictOngoingMsgTime(int packetBytes)
{
    EV << "predictOngoingMsgTime" << endl;
    double sf = loRaRadio->loRaSF;
    double bw = loRaRadio->loRaBW.get();
    double cr = loRaRadio->loRaCR;
    simtime_t Tsym = (pow(2, sf)) / (bw / 1000);

    double preambleSymbNb = 12;
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
    EV << "sendDataFrame" << endl;
}

// ================================================================================================
// ================================================================================================

}// namespace inet
