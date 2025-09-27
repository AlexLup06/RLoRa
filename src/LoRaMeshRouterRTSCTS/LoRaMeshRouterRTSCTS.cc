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

#include "LoRaMeshRouterRTSCTS.h"

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
#include "../helpers/MessageInfoTag_m.h"

#include "../messages/BroadcastFragment_m.h"
#include "../messages/NodeAnnounce_m.h"
#include "../messages/BroadcastCTS_m.h"
#include "../messages/BroadcastHeader_m.h"
#include "../messages/BroadcastContinuousHeader_m.h"

namespace rlora {

Define_Module(LoRaMeshRouterRTSCTS);

LoRaMeshRouterRTSCTS::~LoRaMeshRouterRTSCTS()
{
}

/****************************************************************
 * Initialization functions.
 */
void LoRaMeshRouterRTSCTS::initialize(int stage)
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

        slotTime = par("slotTime");

        mediumStateChange = new cMessage("mediumStateChange");
        listeningStarted = new cMessage("listeningStarted");
        droppedPacket = new cMessage("Dropped Packet");
        endTransmission = new cMessage("End Transmission");
        transmitSwitchDone = new cMessage("transmitSwitchDone");
        nodeAnnounce = new cMessage("Node Announce");
        receptionStarted = new cMessage("receptionStarted");
        endCTSWait = new cMessage("endCTSWait");
        receivedCTS = new cMessage("receivedCTS");
        throughputTimer = new cMessage("throughputTimer");
        endOngoingMsg = new cMessage("endOngoingMsg");
        initiateCTS = new cMessage("initiateCTS");
        endBackoff = new cMessage("endBackoff");
        transmissionStartTimeout = new cMessage("transmissionStartTimeout");
        gotNewMessagToSend = new cMessage("gotNewMessagToSend");
        ctsCWTimeout = new cMessage("ctsCWTimeout");

        throughputSignal = registerSignal("throughputBps");
        effectiveThroughputSignal = registerSignal("effectiveThroughputBps");
        timeInQueue = registerSignal("timeInQueue");
        sentMissionId = registerSignal("sentMissionId");
        receivedMissionId = registerSignal("receivedMissionId");

        scheduleAt(simTime() + measurementInterval, throughputTimer);
        scheduleAt(intuniform(0, 1000) / 1000.0, nodeAnnounce);
    }
    else if (stage == INITSTAGE_LINK_LAYER) {
        turnOnReceiver();
        fsm.setState(LISTENING); // We are always in Listening state
    }
}

void LoRaMeshRouterRTSCTS::finish()
{
    cancelAndDelete(receptionStarted);
    cancelAndDelete(transmitSwitchDone);
    cancelAndDelete(nodeAnnounce);
    cancelAndDelete(endTransmission);
    cancelAndDelete(listeningStarted);
    cancelAndDelete(droppedPacket);
    cancelAndDelete(throughputTimer);

    receptionStarted = nullptr;
    transmitSwitchDone = nullptr;
    nodeAnnounce = nullptr;
    endTransmission = nullptr;
    droppedPacket = nullptr;
    listeningStarted = nullptr;
    throughputTimer = nullptr;

    while (!packetQueue.isEmpty()) {
        auto *pkt = packetQueue.dequeuePacket();
        delete pkt;
    }

    currentTxFrame = nullptr;
}

void LoRaMeshRouterRTSCTS::configureNetworkInterface()
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

queueing::IPassivePacketSource* LoRaMeshRouterRTSCTS::getProvider(cGate *gate)
{
    return (gate->getId() == upperLayerInGateId) ? txQueue.get() : nullptr;
}

// ================================================================================================
// Handle Messages and Signals
// ================================================================================================

void LoRaMeshRouterRTSCTS::handleWithFsm(cMessage *msg)
{
    EV << "Handlewigthfsms: " << msg << endl;
    EV << "CurrentTxFrame: " << currentTxFrame << endl;
    EV << "QUEUE: " << packetQueue.toString() << endl;

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
        FSMA_Enter(turnOnReceiver();EV<<"We are in SWITCHING"<<endl;);
        FSMA_Event_Transition(able-to-listen,
                msg == mediumStateChange,
                LISTENING,
        );
    }

    FSMA_State(LISTENING)
    {
        FSMA_Enter(EV<<"We are in LISTENING"<<endl;);
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
                SEND_CTS,
        );
    }
    FSMA_State(DEFER)
    {
        FSMA_Enter(EV<<"We are in DEFER"<<endl;);
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
    }
    FSMA_State(BACKOFF)
    {
        FSMA_Enter(scheduleBackoffTimer();EV<<"We are in BACKOFF"<<endl;);
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
    }
    FSMA_State(SEND_RTS)
    {
        FSMA_Enter(turnOnTransmitter();EV<<"We are in SEND_RTS"<<endl;);
        FSMA_Event_Transition(transmitter-is-ready-to-send,
                msg == transmitSwitchDone,
                SEND_RTS,
                sendRTS();
        );
        FSMA_Event_Transition(rts-was-sent-now-wait-cts,
                msg == endTransmission,
                WAIT_CTS,
        );
        // TODO: we got some other RTS. What doe we do. I think abort our request and handle the RTS as we would regularly
        FSMA_Event_Transition(rts-was-sent-now-wait-cts,
                isRTS(msg),
                LISTENING,
                handlePacket(pkt);
        );
    }
    FSMA_State(WAIT_CTS)
    {
        FSMA_Enter(turnOnReceiver();EV<<"We are in WAIT_CTS"<<endl;);
        FSMA_Event_Transition(we-didnt-get-cts-go-back-to-listening,
                msg == endCTSWait && !currentlyReceivingOurCTS(),
                LISTENING,
                handleCTSTimeout();
        );
        FSMA_Event_Transition(Listening-Receiving,
                isOurCTS(msg),
                TRANSMITING,
                handleCTS(pkt);
        );
    }
    FSMA_State(TRANSMITING)
    {
        FSMA_Enter(turnOnTransmitter();EV<<"We are in TRANSMITING"<<endl;);
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
        FSMA_Enter(scheduleCTSCWTimer();EV<<"We are in CW_CTS"<<endl;);
        FSMA_Event_Transition(got-cts-sent-to-same-source-as-we-want-to-send-to,
                isCTSForSameRTSSource(msg),
                LISTENING,
                invalidateCTSCWPeriod();
        );
        FSMA_Event_Transition(had-to-wait-cw-to-send-cts,
                msg == ctsCWTimeout,
                SEND_CTS,
                invalidateCTSCWPeriod();
        );
        FSMA_Event_Transition(had-to-wait-cw-to-send-cts,
                isPacketFromSource(msg),
                LISTENING,
                invalidateCTSCWPeriod();
                handlePacket(pkt);
        );
    }
    FSMA_State(SEND_CTS)
    {
        FSMA_Enter(turnOnTransmitter();EV<<"We are in SEND_CTS"<<endl;);
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
        FSMA_Enter(turnOnReceiver();EV<<"We are in AWAIT_TRANSMISSION"<<endl;);
        // TODO: do we then even care about the message if some other CTS made it and the source sends the fragment.
        //       If we get lucky we pick it up. But if not then too bad
        FSMA_Event_Transition(source-didnt-get-cts-just-go-back-to-regular-listening,
                // TODO
                msg == transmissionStartTimeout && !isReceivingPacketFromRTS(),
                LISTENING,
        );
        FSMA_Event_Transition(transmitter-on-now-send-cts,
                isPacketFromSource(msg),
                LISTENING,
                handlePacket(pkt);
        );
    }
    FSMA_State(RECEIVING)
    {
        FSMA_Enter(EV<<"We are in RECEIVING"<<endl;);
        FSMA_Event_Transition(received-message-handle-keep-listening,
                isLowerMessage(msg),
                LISTENING,
                handlePacket(pkt);
        );
        FSMA_Event_Transition(receive-below-sensitivity,
                msg == droppedPacket,
                LISTENING,
        );
    }
}

    if (fsm.getState() == LISTENING && isFreeToSend()) {
        if (currentTxFrame != nullptr) {
            handleWithFsm(currentTxFrame);
        }
        else if (!packetQueue.isEmpty()) {
            EV << "Dequeuing and deleting current tx" << endl;
            currentTxFrame = packetQueue.dequeuePacket();
            handleWithFsm(currentTxFrame);
        }
    }
}
bool LoRaMeshRouterRTSCTS::isPacketFromSource(cMessage *msg)
{
    if (!isLowerMessage(msg)) {
        return false;
    }

    auto packet = dynamic_cast<Packet*>(msg);
    Packet *messageFrame = decapsulate(packet);
    auto chunk = messageFrame->peekAtFront<inet::Chunk>();

    if (auto msg = dynamic_cast<const BroadcastFragment*>(chunk.get())) {
    // TODO: check if its from same source
        return true;
    }
    return false;
}

bool LoRaMeshRouterRTSCTS::currentlyReceivingOurCTS(cMessage *msg){
    if (!isLowerMessage(msg)) {
        return false;
    }

    auto packet = dynamic_cast<Packet*>(msg);
    Packet *messageFrame = decapsulate(packet);
    auto chunk = messageFrame->peekAtFront<inet::Chunk>();

    if (auto msg = dynamic_cast<const BroadcastCTS*>(chunk.get())) {
        // TODO: check if meant for us
        return true;
    }
    return false;
}



void LoRaMeshRouterRTSCTS::handlePacket(Packet *packet)
{
    bytesReceivedInInterval += packet->getByteLength();
    Packet *messageFrame = decapsulate(packet);
    auto chunk = messageFrame->peekAtFront<inet::Chunk>();

    if (auto msg = dynamic_cast<const BroadcastHeader*>(chunk.get())) {
        EV << "Received BroadcastHeader" << endl;

        int messageId = msg->getMessageId();
        int source = msg->getSource();
        int missionId = msg->getMissionId();
        bool isMissionMsg = msg->getRetransmit();

        if (incompletePacketList.getById(messageId) != nullptr) {
            // Paket schon erhalten. Vorbeugen von Duplikaten. Sollte nicht passieren eigentlich
            delete packet;
            return;
        }

        if (!latestMissionIdFromSourceMap.isNewMissionIdLarger(source, missionId)) {
            // Mission schon erhalten
            delete packet;
            return;
        }

        FragmentedPacket incompletePacket;
        incompletePacket.messageId = messageId;
        incompletePacket.missionId = missionId;
        incompletePacket.sourceNode = source;
        incompletePacket.size = msg->getSize();
        incompletePacket.lastHop = msg->getHop();
        incompletePacket.lastFragment = 0;
        incompletePacket.received = 0;
        incompletePacket.corrupted = false;
        incompletePacket.retransmit = msg->getRetransmit();

        incompletePacketList.add(incompletePacket);

        if (isMissionMsg) {
            latestMissionIdFromSourceMap.updateMissionId(source, missionId);
        }

        scheduleAfter(0, initiateCTS);
        size_CTSData = msg->getSize();
        source_CTSData = msg->getSource();
    }
    else if (auto msg = dynamic_cast<const BroadcastContinuousHeader*>(chunk.get())) {
        EV << "Received BroadcastContinuousHeader" << endl;
        // We can only have gotten this if we are not receiving anything else soon. Otherwise we would have sent CTS and the node would have gotten it

        // The messageIds are only the same from the same sender. So if we got BroadcastContinuousHeader and the messageId is not
        // there then we have not gotten the BroadcastHeader which starts the actual beginning of incomplete packet
        if (incompletePacketList.isFromSameHop(msg->getMessageId())) {
            scheduleAfter(0, initiateCTS);
            size_CTSData = msg->getSize();
            source_CTSData = msg->getHopId();
        }
    }
    else if (auto msg = dynamic_cast<const BroadcastFragment*>(chunk.get())) {
        EV << "Received BroadcastFragment" << endl;

        // If we did not get BroadcastHeader from the node we got the Fragment from then we ignore this packet
        Result result = incompletePacketList.addToIncompletePacket(msg);

        // only count bytes if we got the Fragement from the same node from which we got the initial header
        if (result.isRelevant) {
            effectiveBytesReceivedInInterval += packet->getByteLength() - BROADCAST_FRAGMENT_META_SIZE;
        }

        if (result.isComplete) {
            if (result.sendUp) {
                cMessage *readyMsg = new cMessage("Ready");
                sendUp(readyMsg);
                retransmitPacket(result.completePacket);
            }
            incompletePacketList.removeById(msg->getMessageId());
        }
    }
    else if (auto msg = dynamic_cast<const BroadcastCTS*>(chunk.get())) {
        EV << "Received BroadcastCTS" << endl;
        int size = msg->getSize();
        if (endOngoingMsg->isScheduled()) {
            cancelEvent(endOngoingMsg);
        }
        ASSERT(size > 0);
        scheduleAfter(predictOngoingMsgTime(packet->getByteLength()) + sifs, endOngoingMsg);
    }
    delete packet;
}

void LoRaMeshRouterRTSCTS::handleSelfMessage(cMessage *msg)
{
    handleWithFsm(msg);
}

void LoRaMeshRouterRTSCTS::handleUpperPacket(Packet *packet)
{
    EV << "handleUpperPacket" << endl;
// add header to queue
    const auto &payload = packet->peekAtFront<LoRaRobotPacket>();
    bool retransmit = payload->isMission();
    createBroadcastPacket(packet->getByteLength(), -1, -1, retransmit);

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

void LoRaMeshRouterRTSCTS::handleLowerPacket(Packet *msg)
{
    EV << "handleLowerPacket" << endl;
    if (fsm.getState() == RECEIVING || fsm.getState() == WAIT_CTS) {
        handleWithFsm(msg);
    }
    else {
        EV << "Received MSG while not in RECEIVING/WAIT_CTS state" << endl;
        delete msg;
    }
}

/**
 * Notifies about a change in the possibility of pulling some packet from
 * the passive packet source at the given gate.
 *
 * This method is called, for example, when a new packet is inserted into
 * a queue. It allows the sink to pull a new packet from the queue.
 *
 * The gate parameter must be a valid gate of this module.
 */
void LoRaMeshRouterRTSCTS::handleCanPullPacketChanged(cGate *gate)
{
    EV << "handleCanPullPacketChanged" << endl;
    Enter_Method("handleCanPullPacketChanged");
    Packet *packet = dequeuePacket();
    handleUpperMessage(packet);
}

void LoRaMeshRouterRTSCTS::handlePullPacketProcessed(Packet *packet, cGate *gate, bool successful)
{
    EV << "handlePullPacketProcessed" << endl;
    Enter_Method("handlePullPacketProcessed");
    throw cRuntimeError("Not supported callback");
}

void LoRaMeshRouterRTSCTS::receiveSignal(cComponent *source, simsignal_t signalID, intval_t value, cObject *details)
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
}

// ================================================================================================
// ================================================================================================

// ================================================================================================
// Sending Functions
// ================================================================================================

void LoRaMeshRouterRTSCTS::sendDataFrame()
{
    auto frameToSend = getCurrentTransmission();
    EV << "sendDataFrame: " << frameToSend << endl;
    sendDown(frameToSend->dup());

    if (idToAddedTimeMap.find(frameToSend->getId()) != idToAddedTimeMap.end()) {
        SimTime previousTime = idToAddedTimeMap[frameToSend->getId()];
        SimTime delta = simTime() - previousTime;
        emit(timeInQueue, delta);
    }

    auto typeTag = frameToSend->getTag<MessageInfoTag>();
    if (!typeTag->isNeighbourMsg()) {
        emit(sentMissionId, typeTag->getMissionId());
    }
}

void LoRaMeshRouterRTSCTS::announceNodeId(int respond)
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

    auto fragmentEncap = encapsulate(nodeAnnouncePacket);
    packetQueue.enqueueNodeAnnounce(fragmentEncap);
}

void LoRaMeshRouterRTSCTS::retransmitPacket(FragmentedPacket fragmentedPacket)
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
void LoRaMeshRouterRTSCTS::handleCTS(Packet *pkt)
{
    EV << "handleCTS" << endl;
    cancelEvent(endCTSWait);
    delete pkt;
    EV << "END handleCTS" << endl;
}

// Put packet back to queue at front and currentTxFrame is the header again, to try once again. After 3 tries delete the packet
void LoRaMeshRouterRTSCTS::handleCTSTimeout()
{
    EV << "handleCTSTimeout" << endl;
    auto frameToSend = getCurrentTransmission();
    EV << "Got frame: " << frameToSend << endl;
    auto infoTag = frameToSend->getTagForUpdate<MessageInfoTag>();
    EV << "tried accessing" << endl;
    auto frag = frameToSend->peekAtFront<BroadcastFragment>();

    int newTries = infoTag->getTries() + 1;
    infoTag->setTries(newTries);
    EV << "vor try abfrage" << endl;
    if (newTries >= 3) {
        deleteCurrentTxFrame();
        if (!packetQueue.isEmpty()) {
            currentTxFrame = packetQueue.dequeuePacket();
        }
        EV << "PRE END handleCTSTimeout" << endl;
        return;
    }
    EV << "nach try abfrage" << endl;

    packetQueue.enqueuePacketAtPosition(frameToSend, 0);

    ASSERT(frag != nullptr);
    ASSERT(infoTag->getPayloadSize() != -1);
    if (infoTag->getHasRegularHeader()) {
        EV << "CurrentTxFrame is header: " << packetQueue.toString() << endl;
        currentTxFrame = createHeader(frag->getMissionId(), frag->getSource(), infoTag->getPayloadSize(), infoTag->isNeighbourMsg());
    }
    else {
        EV << "CurrentTxFrame is continuous header: " << packetQueue.toString() << endl;
        currentTxFrame = createContinuousHeader(frag->getMissionId(), infoTag->getPayloadSize(), infoTag->isNeighbourMsg());
    }
    EV << "END handleCTSTimeout" << endl;
}

bool LoRaMeshRouterRTSCTS::withRTS()
{
    EV << "withRTS" << endl;
    auto frameToSend = getCurrentTransmission();
    auto infoTag = frameToSend->getTag<MessageInfoTag>();
    return infoTag->getWithRTS();
}

bool LoRaMeshRouterRTSCTS::isOurCTS(cMessage *msg)
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
    return false;
}

bool LoRaMeshRouterRTSCTS::isCTSForSameRTSSource(cMessage *msg)
{
// TODO: actually implement it
// TODO: we also have to check if we actually need this message if it is a missionId

    EV << "isCTSForSameRTSSource: " << msg << endl;
    auto pkt = dynamic_cast<Packet*>(msg);
    if (pkt != nullptr) {
        Packet *messageFrame = decapsulate(pkt);
        auto chunk = messageFrame->peekAtFront<inet::Chunk>();
        if (auto msg = dynamic_cast<const BroadcastCTS*>(chunk.get())) {
            EV << "We got CTS" << endl;
            return true;
        }
    }
    EV << "END isCTSForSameRTSSource" << endl;
    return false;
}

// the RTS is just the currentTxFrame which is just the header.
// Though make sure that it is really a header and there is also another packet afterwards. ASSERT
void LoRaMeshRouterRTSCTS::sendRTS()
{
    EV << "sendRTS" << endl;
    auto frameToSend = getCurrentTransmission();
    auto typeTag = frameToSend->getTag<MessageInfoTag>();
    ASSERT(typeTag->isHeader());

    scheduleAfter(0.1, endCTSWait);
    sendDown(frameToSend);
    ASSERT(!packetQueue.isEmpty());
    currentTxFrame = packetQueue.dequeuePacket();
}

void LoRaMeshRouterRTSCTS::sendCTS()
{
    EV << "sendCTS" << endl;
    auto ctsPacket = new Packet("BroadcastCTS");
    auto ctsPayload = makeShared<BroadcastCTS>();

    ASSERT(size_CTSData > 0);
    ASSERT(source_CTSData > 0);
    ctsPayload->setChunkLength(B(BROADCAST_CTS));
    ctsPayload->setSize(size_CTSData);
    ctsPayload->setHopId(source_CTSData);

    ctsPacket->insertAtBack(ctsPayload);
    ctsPacket->addTagIfAbsent<PacketProtocolTag>()->setProtocol(&Protocol::apskPhy);
    ctsPacket->addTagIfAbsent<MessageInfoTag>()->setIsNeighbourMsg(false);
    encapsulate(ctsPacket);
    sendDown(ctsPacket);

// After we send CTS, we need to receive message within 10 ms otherwise we assume there will be no message
    scheduleAfter(ctsFS + sifs, transmissionStartTimeout);
    size_CTSData = -1;
    source_CTSData = -1;
}

// ================================================================================================
// ================================================================================================

// ================================================================================================
// Packet Creation
// ================================================================================================

void LoRaMeshRouterRTSCTS::createBroadcastPacket(int payloadSize, int missionId, int source, bool retransmit)
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
            Packet *continuousHeader = createContinuousHeader(missionId, payloadSize, retransmit);
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
        fragmentPayload->setFragment(i++);
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

Packet* LoRaMeshRouterRTSCTS::createHeader(int missionId, int source, int payloadSize, bool retransmit)
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

Packet* LoRaMeshRouterRTSCTS::createContinuousHeader(int missionId, int payloadSize, bool retransmit)
{
    EV << "createContinuousHeader" << endl;
    Packet *headerPaket = new Packet("BroadcastContinuousHeader");

    if (missionId == -1) {
        missionId = headerPaket->getId();
    }

    auto headerPayload = makeShared<BroadcastContinuousHeader>();
    headerPayload->setChunkLength(B(BROADCAST_CONTINIOUS_HEADER));
    headerPayload->setSize(payloadSize);
    headerPayload->setHopId(nodeId);
    headerPayload->setMessageId(headerPaket->getId());
    headerPaket->insertAtBack(headerPayload);
    headerPaket->addTagIfAbsent<PacketProtocolTag>()->setProtocol(&Protocol::apskPhy);

    auto messageInfoTag = headerPaket->addTagIfAbsent<MessageInfoTag>();
    messageInfoTag->setIsNeighbourMsg(!retransmit);
    messageInfoTag->setMissionId(missionId);
    messageInfoTag->setIsHeader(true);

    encapsulate(headerPaket);
    return headerPaket;
}

Packet* LoRaMeshRouterRTSCTS::encapsulate(Packet *msg)
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

Packet* LoRaMeshRouterRTSCTS::decapsulate(Packet *frame)
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

void LoRaMeshRouterRTSCTS::invalidateBackoffPeriod()
{
    backoffPeriod = -1;
}

bool LoRaMeshRouterRTSCTS::isInvalidBackoffPeriod()
{
    return backoffPeriod == -1;
}

void LoRaMeshRouterRTSCTS::generateBackoffPeriod()
{
    int slots = intrand(cwBackoff);
    backoffPeriod = slots * backoffFS;
}

void LoRaMeshRouterRTSCTS::scheduleBackoffTimer()
{
    if (isInvalidBackoffPeriod())
        generateBackoffPeriod();
    scheduleAfter(backoffPeriod, endBackoff);
}

void LoRaMeshRouterRTSCTS::decreaseBackoffPeriod()
{
    simtime_t elapsedBackoffTime = simTime() - endBackoff->getSendingTime();
    backoffPeriod -= ((int) (elapsedBackoffTime / backoffFS)) * backoffFS;
    ASSERT(backoffPeriod >= 0);
}

void LoRaMeshRouterRTSCTS::cancelBackoffTimer()
{
    cancelEvent(endBackoff);
}

// ================================================================================================
// ================================================================================================

// ================================================================================================
// CTS CW Period
// ================================================================================================

void LoRaMeshRouterRTSCTS::invalidateCTSCWPeriod()
{
    ctsCWPeriod = -1;
}

bool LoRaMeshRouterRTSCTS::isInvalidCTSCWPeriod()
{
    return ctsCWPeriod == -1;
}

void LoRaMeshRouterRTSCTS::generateCTSCWPeriod()
{
    int slots = intrand(cwCTS);
    ctsCWPeriod = slots * ctsFS;
}

void LoRaMeshRouterRTSCTS::scheduleCTSCWTimer()
{
    if (isInvalidCWPeriod())
        generateCWPeriod();
    scheduleAfter(ctsCWPeriod, endBackoff);
}

void LoRaMeshRouterRTSCTS::cancelCTSCWTimer()
{
    cancelEvent(ctsCWTimeout);
}

// ================================================================================================
// ================================================================================================

// ================================================================================================
// Radio Utils
// ================================================================================================

bool LoRaMeshRouterRTSCTS::isMediumFree()
{
    return radio->getReceptionState() == IRadio::RECEPTION_STATE_IDLE;
}

bool LoRaMeshRouterRTSCTS::isReceiving()
{
    return radio->getReceptionState() == IRadio::RECEPTION_STATE_RECEIVING;
}

void LoRaMeshRouterRTSCTS::turnOnReceiver()
{
// this makes receiverPart go to idle no matter what
    LoRaRadio *loraRadio;
    loraRadio = check_and_cast<LoRaRadio*>(radio);
    loraRadio->setRadioMode(IRadio::RADIO_MODE_RECEIVER);
}
void LoRaMeshRouterRTSCTS::turnOnTransmitter()
{
// this makes receiverPart go to idle no matter what
    LoRaRadio *loraRadio;
    loraRadio = check_and_cast<LoRaRadio*>(radio);
    loraRadio->setRadioMode(IRadio::RADIO_MODE_TRANSMITTER);
}

void LoRaMeshRouterRTSCTS::turnOffReceiver()
{
    LoRaRadio *loraRadio;
    loraRadio = check_and_cast<LoRaRadio*>(radio);
    loraRadio->setRadioMode(IRadio::RADIO_MODE_SLEEP);
}

// ================================================================================================
// ================================================================================================

// ================================================================================================
// Helper functions
// ================================================================================================

void LoRaMeshRouterRTSCTS::finishCurrentTransmission()
{
    EV << "finishCurrentTransmission" << endl;
    deleteCurrentTxFrame();
}

Packet* LoRaMeshRouterRTSCTS::getCurrentTransmission()
{
    EV << "getCurrentTransmission" << endl;
    ASSERT(currentTxFrame != nullptr);
    return currentTxFrame;
}

MacAddress LoRaMeshRouterRTSCTS::getAddress()
{
    return address;
}

// We do not have to wait for a ongoing transmission by a hidden node. Have to check before going into backoff period
// AND we are not waiting for the start of a message
bool LoRaMeshRouterRTSCTS::isFreeToSend()
{
    return !endOngoingMsg->isScheduled() && !transmissionStartTimeout->isScheduled();
}

double LoRaMeshRouterRTSCTS::predictOngoingMsgTime(int packetBytes)
{
    double sf = loRaRadio->loRaSF;
    double bw = loRaRadio->loRaBW.get();
    double cr = loRaRadio->loRaCR;
    simtime_t Tsym = (pow(2, sf)) / (bw / 1000);

    int preambleSymbNb = 12;
    int headerSymbNb = 8;
    int payloadSymbNb = std::ceil((8 * packetBytes - 4 * sf + 28 + 16 - 20 * 0) / (4 * (sf - 2 * 0))) * (cr + 4);
    if (payloadSymbNb < 8)
        payloadSymbNb = 8;

    simtime_t Tpreamble = (preambleSymbNb + 4.25) * Tsym / 1000;
    simtime_t Theader = headerSymbNb * Tsym / 1000;
    ;
    simtime_t Tpayload = payloadSymbNb * Tsym / 1000;

    const simtime_t duration = Tpreamble + Theader + Tpayload;
    return duration.dbl();
}

// ================================================================================================
// ================================================================================================

}// namespace inet
