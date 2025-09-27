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

#include "LoRaAloha.h"

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
#include "../messages/BroadcastLeaderFragment_m.h"

namespace rlora {

Define_Module(LoRaAloha);

LoRaAloha::~LoRaAloha()
{
}

/****************************************************************
 * Initialization functions.
 */
void LoRaAloha::initialize(int stage)
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

        mediumStateChange = new cMessage("MediumStateChange");
        droppedPacket = new cMessage("Dropped Packet");
        endTransmission = new cMessage("End Transmission");
        nodeAnnounce = new cMessage("Node Announce");
        transmitSwitchDone = new cMessage("transmitSwitchDone");
        receptionStated = new cMessage("receptionStated");
        throughputTimer = new cMessage("throughputTimer");

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

void LoRaAloha::finish()
{
    cancelAndDelete(receptionStated);
    cancelAndDelete(transmitSwitchDone);
    cancelAndDelete(nodeAnnounce);
    cancelAndDelete(endTransmission);
    cancelAndDelete(mediumStateChange);
    cancelAndDelete(droppedPacket);
    cancelAndDelete(throughputTimer);

    receptionStated = nullptr;
    transmitSwitchDone = nullptr;
    nodeAnnounce = nullptr;
    endTransmission = nullptr;
    droppedPacket = nullptr;
    mediumStateChange = nullptr;
    throughputTimer = nullptr;

    while (!packetQueue.isEmpty()) {
        auto *pkt = packetQueue.dequeuePacket();
        delete pkt;
    }

    currentTxFrame = nullptr;
}

void LoRaAloha::configureNetworkInterface()
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

/****************************************************************
 * Message handling functions.
 */
void LoRaAloha::handleSelfMessage(cMessage *msg)
{
    handleWithFsm(msg);
}

void LoRaAloha::handleUpperPacket(Packet *packet)
{
    // add header to queue
    const auto &payload = packet->peekAtFront<LoRaRobotPacket>();
    bool isMission = payload->isMission();
    int missionId = -2;
    if (isMission) {
        missionId = -1;
    }
    createBroadcastPacket(packet->getByteLength(), missionId, -1, -1, isMission);

    if (currentTxFrame == nullptr) {
        Packet *packetToSend = packetQueue.dequeuePacket();
        currentTxFrame = packetToSend;
    }
    handleWithFsm(packet);
    delete packet;
}

void LoRaAloha::handleLowerPacket(Packet *msg)
{
    if (fsm.getState() == RECEIVING) {
        handleWithFsm(msg);
    }
    else {
        EV << "Received MSG while not in RECEIVING state" << endl;
        delete msg;
    }
}

queueing::IPassivePacketSource* LoRaAloha::getProvider(cGate *gate)
{
    return (gate->getId() == upperLayerInGateId) ? txQueue.get() : nullptr;
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
void LoRaAloha::handleCanPullPacketChanged(cGate *gate)
{
    Enter_Method("handleCanPullPacketChanged");
    Packet *packet = dequeuePacket();
    handleUpperMessage(packet);
}

void LoRaAloha::handlePullPacketProcessed(Packet *packet, cGate *gate, bool successful)
{
    Enter_Method("handlePullPacketProcessed");
    throw cRuntimeError("Not supported callback");
}

void LoRaAloha::handleWithFsm(cMessage *msg)
{
    Ptr<LoRaMacFrame> frame = nullptr;
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
        FSMA_Enter(turnOnReceiver());
        FSMA_Event_Transition(Listening-Receiving,
                msg == mediumStateChange,
                LISTENING,
        );
    }

    FSMA_State(LISTENING)
    {
        FSMA_Event_Transition(Listening-Receiving,
                isReceiving(),
                RECEIVING,
        );

        FSMA_Event_Transition(Listening-Transmitting_1,
                currentTxFrame!=nullptr && !isReceiving(),
                TRANSMITING,
        );
    }

    FSMA_State(TRANSMITING)
    {
        FSMA_Enter(radio->setRadioMode(IRadio::RADIO_MODE_TRANSMITTER));
        FSMA_Event_Transition(Transmit-Listening,
                msg == transmitSwitchDone,
                TRANSMITING,
                sendDataFrame();
        );
        FSMA_Event_Transition(Transmit-Listening,
                msg == endTransmission,
                SWITCHING,
                finishCurrentTransmission();
        );
    }

    FSMA_State(RECEIVING)
    {
        FSMA_Event_Transition(Listening-Receiving,
                isLowerMessage(msg),
                LISTENING,
                handlePacket(pkt);
        );
        FSMA_Event_Transition(Receive-BelowSensitivity,
                msg == droppedPacket,
                LISTENING,
        );
    }
}

    if (fsm.getState() == LISTENING && receptionState == IRadio::RECEPTION_STATE_IDLE) {
        if (currentTxFrame != nullptr) {
            handleWithFsm(currentTxFrame);
        }
        else if (!packetQueue.isEmpty()) {
            currentTxFrame = packetQueue.dequeuePacket();
            handleWithFsm(currentTxFrame);
        }
    }
}

/**
 * Wir können vier verschiedene Packete erhalten haben:
 *
 *  1. AnnounceNode
 *  2. Broadcast_Header
 *  3. Broadcast_Fragment
 *  4. Broadcast_Ack
 *
 *  Je nachdem was wir erhalten haben machen wir etwas anderes
 */
void LoRaAloha::handlePacket(Packet *packet)
{
    bytesReceivedInInterval += packet->getByteLength();
    Packet *messageFrame = decapsulate(packet);
    auto chunk = messageFrame->peekAtFront<inet::Chunk>();

    if (auto msg = dynamic_cast<const NodeAnnounce*>(chunk.get())) {
        EV << "Received NodeAnnounce" << endl;
        // einfach kopirt aus Julians implementierung
        if (nodeId != msg->getNodeId()) {

            if (msg->getRespond() == 1) {
                // Unknown node! Block Sending for a time window, to allow other Nodes to respond.
                announceNodeId(0);
            }
        }

    }
    else if (auto msg = dynamic_cast<const BroadcastLeaderFragment*>(chunk.get())) {
        EV << "Received BroadcastLeaderFragment" << endl;

        effectiveBytesReceivedInInterval += packet->getByteLength() - BROADCAST_LEADER_FRAGMENT_META_SIZE;

        int messageId = msg->getMessageId();
        int source = msg->getSource();
        int missionId = msg->getMissionId();
        bool isMissionMsg = msg->getRetransmit();

        if (incompletePacketList.getById(messageId) != nullptr) {
            // Paket schon erhalten. Vorbeugen von Duplikaten. Sollte nicht passieren eigentlich
            delete packet;
            return;
        }

        if (!latestMessageIdMap.isNewMessageIdLarger(source, messageId)) {
            // packet schon erhalten. Wir gerade von einem anderen Knoten nochmal wiederholt
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
        latestMessageIdMap.updateMessageId(source, messageId);

        if (isMissionMsg) {
            latestMissionIdFromSourceMap.updateMissionId(source, missionId);
        }

        // wir senden schon mit dem ersten packet daten
        BroadcastFragment *fragmentPayload = new BroadcastFragment();
        fragmentPayload->setChunkLength(msg->getChunkLength());
        fragmentPayload->setPayloadSize(msg->getPayloadSize());
        fragmentPayload->setMessageId(messageId);
        fragmentPayload->setMissionId(missionId);
        fragmentPayload->setSource(source);
        fragmentPayload->setFragment(0);

        Result result = incompletePacketList.addToIncompletePacket(fragmentPayload);

        if (result.isComplete) {
            if (result.sendUp) {
                cMessage *readyMsg = new cMessage("Ready");
                sendUp(readyMsg);
                retransmitPacket(result.completePacket);
            }
            incompletePacketList.removeById(msg->getMessageId());
        }
        delete fragmentPayload;

    }
    else if (auto msg = dynamic_cast<const BroadcastFragment*>(chunk.get())) {
        EV << "Received BroadcastFragment" << endl;
        Result result = incompletePacketList.addToIncompletePacket(msg);

        effectiveBytesReceivedInInterval += packet->getByteLength() - BROADCAST_FRAGMENT_META_SIZE;

        if (result.isComplete) {
            if (result.sendUp) {
                cMessage *readyMsg = new cMessage("Ready");
                sendUp(readyMsg);
                retransmitPacket(result.completePacket);
            }
            incompletePacketList.removeById(msg->getMessageId());
        }
    }
    else {
        throw cRuntimeError("Received Unknown message");

    }
    delete packet;
}

void LoRaAloha::receiveSignal(cComponent *source, simsignal_t signalID, intval_t value, cObject *details)
{
    Enter_Method_Silent();
    if (signalID == IRadio::receptionStateChangedSignal) {
        IRadio::ReceptionState newRadioReceptionState = (IRadio::ReceptionState) value;
        // just switching from transmitter to receiver
        if (receptionState == IRadio::RECEPTION_STATE_UNDEFINED && newRadioReceptionState == IRadio::RECEPTION_STATE_IDLE) {
            receptionState = newRadioReceptionState;
            handleWithFsm(mediumStateChange);
        }
        if (receptionState == IRadio::RECEPTION_STATE_IDLE && newRadioReceptionState == IRadio::RECEPTION_STATE_RECEIVING) {
            receptionState = newRadioReceptionState;
            handleWithFsm(receptionStated);
        }
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

Packet* LoRaAloha::encapsulate(Packet *msg)
{
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

Packet* LoRaAloha::decapsulate(Packet *frame)
{
    auto loraHeader = frame->popAtFront<LoRaMacFrame>();
    frame->addTagIfAbsent<MacAddressInd>()->setSrcAddress(loraHeader->getTransmitterAddress());
    frame->addTagIfAbsent<MacAddressInd>()->setDestAddress(loraHeader->getReceiverAddress());
    frame->addTagIfAbsent<InterfaceInd>()->setInterfaceId(networkInterface->getInterfaceId());

    return frame;
}

/****************************************************************
 * Frame sender functions. send to Radio
 */
void LoRaAloha::sendDataFrame()
{
    auto frameToSend = getCurrentTransmission();
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

    frameToSend = nullptr;
    delete frameToSend;
}

/****************************************************************
 * Helper functions.
 */
void LoRaAloha::finishCurrentTransmission()
{
    if (currentTxFrame != nullptr) {
        currentTxFrame = nullptr;
    }
}

Packet* LoRaAloha::getCurrentTransmission()
{
    ASSERT(currentTxFrame != nullptr);
    return currentTxFrame;
}

void LoRaAloha::retransmitPacket(FragmentedPacket fragmentedPacket)
{
    // jedes feld muss gleich sein vom fragemntierten packet außer lastHop
    if (!fragmentedPacket.retransmit || fragmentedPacket.sourceNode == nodeId) {
        return;
    }
    emit(receivedMissionId, fragmentedPacket.missionId);
    createBroadcastPacket(fragmentedPacket.size, fragmentedPacket.missionId, nodeId, fragmentedPacket.sourceNode, fragmentedPacket.retransmit);
}

void LoRaAloha::createBroadcastPacket(int packetSize, int missionId, int hopId, int source, bool retransmit)
{
    auto headerPaket = new Packet("BroadcastLeaderFragment");
    auto headerPayload = makeShared<BroadcastLeaderFragment>();
    int messageId = headerPaket->getId();

    if (missionId == -1) {
        missionId = headerPaket->getId();
    }
    if (hopId == -1) {
        hopId = nodeId;
    }
    if (source == -1) {
        source = nodeId;
    }
    headerPayload->setSize(packetSize);
    if (packetSize + BROADCAST_LEADER_FRAGMENT_META_SIZE > 255) {
        headerPayload->setChunkLength(B(255));
        headerPayload->setPayloadSize(255 - BROADCAST_LEADER_FRAGMENT_META_SIZE);
        packetSize = packetSize - (255 - BROADCAST_LEADER_FRAGMENT_META_SIZE);
    }
    else {
        headerPayload->setChunkLength(B(packetSize + BROADCAST_LEADER_FRAGMENT_META_SIZE));
        headerPayload->setPayloadSize(packetSize);
        packetSize = 0;
    }
    headerPayload->setMessageId(messageId);
    headerPayload->setMissionId(missionId);
    headerPayload->setSource(source);
    headerPayload->setHop(hopId);
    headerPayload->setRetransmit(retransmit);
    headerPaket->insertAtBack(headerPayload);
    headerPaket->addTagIfAbsent<PacketProtocolTag>()->setProtocol(&Protocol::apskPhy);

    auto messageInfoTag = headerPaket->addTagIfAbsent<MessageInfoTag>();
    messageInfoTag->setIsNeighbourMsg(!retransmit);
    messageInfoTag->setMissionId(missionId);
    messageInfoTag->setIsHeader(true);
    messageInfoTag->setHasUsefulData(true);

    encapsulate(headerPaket);

    bool trackQueueTime = packetQueue.enqueuePacket(headerPaket);
    if (trackQueueTime) {
        idToAddedTimeMap[headerPaket->getId()] = simTime();
    }

    // INFO: das nullte packet ist das was im leader direkt mitgeschickt wird
    int i = 1;
    while (packetSize > 0) {
        auto fragmentPacket = new Packet("BroadcastFragmentPkt");
        auto fragmentPayload = makeShared<BroadcastFragment>();

        if (packetSize + BROADCAST_FRAGMENT_META_SIZE > 255) {
            fragmentPayload->setChunkLength(B(255));
            fragmentPayload->setPayloadSize(255 - BROADCAST_FRAGMENT_META_SIZE);
            packetSize = packetSize - (255 - BROADCAST_FRAGMENT_META_SIZE);
        }
        else {
            fragmentPayload->setChunkLength(B(packetSize + BROADCAST_FRAGMENT_META_SIZE));
            fragmentPayload->setPayloadSize(packetSize);
            packetSize = 0;
        }

        fragmentPayload->setMessageId(messageId);
        fragmentPayload->setMissionId(missionId);
        fragmentPayload->setSource(source);
        fragmentPayload->setFragment(i++);
        fragmentPacket->insertAtBack(fragmentPayload);
        fragmentPacket->addTagIfAbsent<PacketProtocolTag>()->setProtocol(&Protocol::apskPhy);

        auto messageInfoTag = fragmentPacket->addTagIfAbsent<MessageInfoTag>();
        messageInfoTag->setIsNeighbourMsg(!retransmit);
        messageInfoTag->setMissionId(missionId);
        messageInfoTag->setIsHeader(false);
        messageInfoTag->setHasUsefulData(true);

        encapsulate(fragmentPacket);
        packetQueue.enqueuePacket(fragmentPacket);
        if (trackQueueTime) {
            idToAddedTimeMap[fragmentPacket->getId()] = simTime();
        }
    }
}

void LoRaAloha::announceNodeId(int respond)
{
    auto nodeAnnouncePacket = new Packet("NodeAnnouncePacket");
    auto nodeAnnouncePayload = makeShared<NodeAnnounce>();
    nodeAnnouncePayload->setChunkLength(B(NODE_ANNOUNCE_SIZE));
    nodeAnnouncePayload->setLastHop(nodeId);
    nodeAnnouncePayload->setNodeId(nodeId);
    nodeAnnouncePayload->setRespond(respond);

    nodeAnnouncePacket->insertAtBack(nodeAnnouncePayload);
    nodeAnnouncePacket->addTagIfAbsent<PacketProtocolTag>()->setProtocol(&Protocol::apskPhy);
    nodeAnnouncePacket->addTagIfAbsent<MessageInfoTag>()->setIsNeighbourMsg(false);
    auto fragmentEncap = encapsulate(nodeAnnouncePacket);
    packetQueue.enqueueNodeAnnounce(fragmentEncap);
}

bool LoRaAloha::isReceiving()
{
    return radio->getReceptionState() == IRadio::RECEPTION_STATE_RECEIVING;
}

void LoRaAloha::turnOnReceiver()
{

    LoRaRadio *loraRadio;
    loraRadio = check_and_cast<LoRaRadio*>(radio);
    loraRadio->setRadioMode(IRadio::RADIO_MODE_RECEIVER);
}

void LoRaAloha::turnOffReceiver()
{
    LoRaRadio *loraRadio;
    loraRadio = check_and_cast<LoRaRadio*>(radio);
    loraRadio->setRadioMode(IRadio::RADIO_MODE_SLEEP);
}

MacAddress LoRaAloha::getAddress()
{
    return address;
}

} // namespace inet
