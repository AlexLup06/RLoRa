//
// Copyright (C) 2016 OpenSim Ltd.
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this program; if not, see <http://www.gnu.org/licenses/>.
//

#include "LoRaMeshRouter.h"

#include "inet/common/ModuleAccess.h"
#include "inet/linklayer/common/Ieee802Ctrl.h"
#include "inet/linklayer/common/UserPriority.h"
#include "inet/linklayer/csmaca/CsmaCaMac.h"
#include "inet/common/ProtocolTag_m.h"
#include "inet/linklayer/common/MacAddressTag_m.h"
#include "inet/linklayer/csmaca/CsmaCaMac.h"
#include "inet/linklayer/common/InterfaceTag_m.h"

#include "../LoRa/LoRaTagInfo_m.h"
#include "../helpers/generalHelpers.h"
#include "../helpers/MessageTypeTag_m.h"

#include "BroadcastAck_m.h"
#include "BroadcastFragment_m.h"
#include "BroadcastHeader_m.h"
#include "NodeAnnounce_m.h"

namespace rlora {

Define_Module(LoRaMeshRouter);

LoRaMeshRouter::~LoRaMeshRouter()
{
}

/****************************************************************
 * Initialization functions.
 */
void LoRaMeshRouter::initialize(int stage)
{
    MacProtocolBase::initialize(stage);
    if (stage == INITSTAGE_LOCAL) {
        const char *addressString = par("address");
        headerLength = par("headerLength");

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
        waitDelay = new cMessage("Wait Delay");
        nodeAnnounce = new cMessage("Node Announce");
        transmitSwitchDone = new cMessage("transmitSwitchDone");

        scheduleAt(intuniform(0, 1000) / 1000.0, nodeAnnounce);

    }
    else if (stage == INITSTAGE_LINK_LAYER) {
        turnOnReceiver();
        fsm.setState(LISTENING); // We are always in Listening state
    }
}

void LoRaMeshRouter::finish()
{
}

void LoRaMeshRouter::configureNetworkInterface()
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
void LoRaMeshRouter::handleSelfMessage(cMessage *msg)
{
    handleWithFsm(msg);
}

/**
 * For "serial" messages that we receive from the App (Host Application), we put a BroadcastHeader and
 * as many BroadcastFragments as needed in the packetQueue. If such a BroadcastPacket already exists, we just update that one
 * If there is no currentTxFrame - basically the next packet to send - we pull the next packet from the Queue and set it to currentTxFrame
 * If we are free to send handleWithFsm will send, otherwise handleWithFsm just does nothing
 */
void LoRaMeshRouter::handleUpperPacket(Packet *packet)
{
    // add header to queue
    bool isNeighbourMsg = intuniform(0, 100) < 10;
    createBroadcastPacket(packet->getByteLength(), -1, -1, -1, true, isNeighbourMsg);

    if (currentTxFrame == nullptr) {
        Packet *packetToSend = packetQueue.dequeuePacket();
        currentTxFrame = packetToSend;
    }
    handleWithFsm(packet);
}

void LoRaMeshRouter::handleLowerPacket(Packet *msg)
{
    if (fsm.getState() == RECEIVING) {
        handleWithFsm(msg);
    }
    else {
        EV << "Received MSG while not in RECEIVING state" << endl;
        delete msg;
    }
}

queueing::IPassivePacketSource* LoRaMeshRouter::getProvider(cGate *gate)
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
void LoRaMeshRouter::handleCanPullPacketChanged(cGate *gate)
{
    Enter_Method("handleCanPullPacketChanged");
    Packet *packet = dequeuePacket();
    handleUpperMessage(packet);
}

void LoRaMeshRouter::handlePullPacketProcessed(Packet *packet, cGate *gate, bool successful)
{
    Enter_Method("handlePullPacketProcessed");
    throw cRuntimeError("Not supported callback");
}

void LoRaMeshRouter::handleWithFsm(cMessage *msg)
{
    Ptr<LoRaMacFrame> frame = nullptr;
    auto pkt = dynamic_cast<Packet*>(msg);

    if (msg == nodeAnnounce) {
        announceNodeId(0);
        scheduleAfter(5, nodeAnnounce);
    }

    FSMA_Switch(fsm){

    FSMA_State(LISTENING)
    {
        FSMA_Enter(turnOnReceiver());
        FSMA_Event_Transition(Listening-Receiving,
                msg == mediumStateChange && isReceiving(),
                RECEIVING,
        );

        FSMA_Event_Transition(Listening-Transmitting_1,
                currentTxFrame!=nullptr && !waitDelay->isScheduled() && !isReceiving(),
                TRANSMITING,
        );

        FSMA_Event_Transition(Listening-Transmitting_1,
                msg == waitDelay && currentTxFrame != nullptr,
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
                msg == endTransmission /* && Check if NO more fragments left to send from the message */,
                LISTENING,
                finishCurrentTransmission();
//                numSent++;
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

    if (fsm.getState() == LISTENING && receptionState == IRadio::RECEPTION_STATE_IDLE && !waitDelay->isScheduled()) {
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
void LoRaMeshRouter::handlePacket(Packet *packet)
{

    Packet *messageFrame = decapsulate(packet);
    auto chunk = messageFrame->peekAtFront<inet::Chunk>();

    if (auto msg = dynamic_cast<const NodeAnnounce*>(chunk.get())) {
        EV << "Received NodeAnnounce" << endl;
        // einfach kopirt aus Julians implementierung
        if (nodeId != msg->getNodeId()) {

            if (msg->getRespond() == 1) {
                // Unknown node! Block Sending for a time window, to allow other Nodes to respond.
                senderWaitDelay(0 + intuniform(0, 450));
                announceNodeId(0);
            }
        }

    }
    else if (auto msg = dynamic_cast<const BroadcastHeader*>(chunk.get())) {
        EV << "Received BroadcastHeader" << endl;
        int messageId = msg->getMessageId();
        int source = msg->getSource();
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

        FragmentedPacket incompletePacket;
        incompletePacket.messageId = messageId;
        incompletePacket.sourceNode = source;
        incompletePacket.size = msg->getSize();
        incompletePacket.lastHop = msg->getHop();
        incompletePacket.lastFragment = 0;
        incompletePacket.received = 0;
        incompletePacket.corrupted = false;
        incompletePacket.retransmit = msg->getRetransmit();

        incompletePacketList.add(incompletePacket);
        latestMessageIdMap.updateMessageId(source, messageId);

        int size = msg->getSize();
        int waitTime = 20 + predictSendTime(size);
        senderWaitDelay(waitTime);

    }
    else if (auto msg = dynamic_cast<const BroadcastFragment*>(chunk.get())) {
        EV << "Received BroadcastFragment" << endl;
        Result result = incompletePacketList.addToIncompletePacket(msg);

        if (result.waitTime >= 0) {
            senderWaitDelay(result.waitTime);
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
    else if (auto msg = dynamic_cast<const BroadcastAck*>(chunk.get())) {
        EV << "Received BroadcastAck" << endl;
        // we are by design not sending any acks so we dont receive any acks

    }
    else {
        throw cRuntimeError("Received Unknown message");

    }
    delete packet;
}

void LoRaMeshRouter::receiveSignal(cComponent *source, simsignal_t signalID, intval_t value, cObject *details)
{
    Enter_Method_Silent();
    if (signalID == IRadio::receptionStateChangedSignal) {
        IRadio::ReceptionState newRadioReceptionState = (IRadio::ReceptionState) value;
        if (receptionState == IRadio::RECEPTION_STATE_RECEIVING) {
        }
        receptionState = newRadioReceptionState;
        handleWithFsm(mediumStateChange);
    }
    else if (signalID == LoRaRadio::droppedPacket) {
        radio->setRadioMode(IRadio::RADIO_MODE_RECEIVER);
        handleWithFsm(droppedPacket);
    }
    else if (signalID == IRadio::transmissionStateChangedSignal) {
        IRadio::TransmissionState newRadioTransmissionState = (IRadio::TransmissionState) value;
        if (transmissionState == IRadio::TRANSMISSION_STATE_TRANSMITTING && newRadioTransmissionState == IRadio::TRANSMISSION_STATE_IDLE) {
            handleWithFsm(endTransmission);
        }

        if (transmissionState == IRadio::TRANSMISSION_STATE_UNDEFINED && newRadioTransmissionState == IRadio::TRANSMISSION_STATE_IDLE) {
            handleWithFsm(transmitSwitchDone);
        }
        transmissionState = newRadioTransmissionState;
    }
}

Packet* LoRaMeshRouter::encapsulate(Packet *msg)
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

Packet* LoRaMeshRouter::decapsulate(Packet *frame)
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
void LoRaMeshRouter::sendDataFrame()
{
    auto frameToSend = getCurrentTransmission();
    if (frameToSend != nullptr) {
        int waitTime = waitTimeMap[frameToSend->getId()];

        senderWaitDelay(waitTime);
        sendDown(frameToSend);
    }
}

/****************************************************************
 * Helper functions.
 */
void LoRaMeshRouter::finishCurrentTransmission()
{
    if (currentTxFrame != nullptr) {
        currentTxFrame = nullptr;
    }
}

Packet* LoRaMeshRouter::getCurrentTransmission()
{
    ASSERT(currentTxFrame != nullptr);
    return currentTxFrame;
}

void LoRaMeshRouter::retransmitPacket(FragmentedPacket fragmentedPacket)
{
    // jedes feld muss gleich sein vom fragemntierten packet außer lastHop
    if (!fragmentedPacket.retransmit) {
        return;
    }
    createBroadcastPacket(fragmentedPacket.size, fragmentedPacket.messageId, nodeId, fragmentedPacket.sourceNode, fragmentedPacket.retransmit, false);
}

void LoRaMeshRouter::createBroadcastPacket(int packetSize, int messageId, int hopId, int source, bool retransmit, bool isNeighbourMsg)
{
    // TODO: Prüfe den Typ des packets und update die packetQueue mir einem aktuellen Packet falls nötig

    auto headerPaket = new Packet("BroadcastHeaderPkt");
    auto headerPayload = makeShared<BroadcastHeader>();

    if (messageId == -1) {
        int messageId = headerPaket->getId();
    }
    if (hopId == -1) {
        int hopId = nodeId;
    }
    if (source == -1) {
        int source = nodeId;
    }

    headerPayload->setChunkLength(B(BROADCAST_HEADER_SIZE));
    headerPayload->setSize(packetSize);
    headerPayload->setMessageId(messageId);
    headerPayload->setSource(source);
    headerPayload->setHop(hopId);
    headerPayload->setRetransmit(retransmit);
    headerPaket->insertAtBack(headerPayload);
    headerPaket->addTagIfAbsent<PacketProtocolTag>()->setProtocol(&Protocol::apskPhy);
    headerPaket->addTagIfAbsent<MessageTypeTag>()->setIsNeighbourMsg(isNeighbourMsg);
    auto headerEncap = encapsulate(headerPaket);
    packetQueue.enqueuePacket(headerEncap, true);
    waitTimeMap[headerEncap->getId()] = 150;

    int i = 0;
    while (packetSize > 0) {
        auto fragmentPacket = new Packet("BroadcastFragmentPkt");
        auto fragmentPayload = makeShared<BroadcastFragment>();

        if (packetSize + BROADCAST_FRAGMENT_META_SIZE > 255) {
            fragmentPayload->setChunkLength(B(255));
            fragmentPayload->setSize(255 - BROADCAST_FRAGMENT_META_SIZE);
            packetSize = packetSize - (255 - BROADCAST_FRAGMENT_META_SIZE);
        }
        else {
            fragmentPayload->setChunkLength(B(packetSize + BROADCAST_FRAGMENT_META_SIZE));
            fragmentPayload->setSize(packetSize);
            packetSize = 0;
        }

        fragmentPayload->setMessageId(messageId);
        fragmentPayload->setSource(source);
        fragmentPayload->setFragment(i++);
        fragmentPacket->insertAtBack(fragmentPayload);
        fragmentPacket->addTagIfAbsent<PacketProtocolTag>()->setProtocol(&Protocol::apskPhy);
        fragmentPacket->addTagIfAbsent<MessageTypeTag>()->setIsNeighbourMsg(isNeighbourMsg);

        auto fragmentEncap = encapsulate(fragmentPacket);
        packetQueue.enqueuePacket(fragmentEncap);

        if (packetSize == 0) {
            waitTimeMap[fragmentEncap->getId()] = 50 + 270 + intuniform(0, 50);
        }
    }
}

void LoRaMeshRouter::announceNodeId(int respond)
{
    auto nodeAnnouncePacket = new Packet("NodeAnnouncePacket");
    auto nodeAnnouncePayload = makeShared<NodeAnnounce>();
    nodeAnnouncePayload->setChunkLength(B(NODE_ANNOUNCE_SIZE));
    nodeAnnouncePayload->setLastHop(nodeId);
    nodeAnnouncePayload->setNodeId(nodeId);
    nodeAnnouncePayload->setRespond(respond);

    nodeAnnouncePacket->insertAtBack(nodeAnnouncePayload);
    nodeAnnouncePacket->addTagIfAbsent<PacketProtocolTag>()->setProtocol(&Protocol::apskPhy);
    nodeAnnouncePacket->addTagIfAbsent<MessageTypeTag>()->setIsNeighbourMsg(false);
    auto fragmentEncap = encapsulate(nodeAnnouncePacket);
    // todo: paclet in front
    packetQueue.enqueuePacket(fragmentEncap);
}

bool LoRaMeshRouter::isReceiving()
{
    return radio->getReceptionState() == IRadio::RECEPTION_STATE_RECEIVING;
}

void LoRaMeshRouter::turnOnReceiver()
{

    LoRaRadio *loraRadio;
    loraRadio = check_and_cast<LoRaRadio*>(radio);
    loraRadio->setRadioMode(IRadio::RADIO_MODE_RECEIVER);
}

void LoRaMeshRouter::senderWaitDelay(int waitTime)
{
    simtime_t newScheduleTime = simTime() + SimTime(waitTime, SIMTIME_MS); // assume waitTime is in milliseconds

    if (waitDelay->isScheduled()) {
        simtime_t currentScheduledTime = waitDelay->getArrivalTime();
        if (newScheduleTime > currentScheduledTime) {
            cancelEvent(waitDelay);
            scheduleAt(newScheduleTime, waitDelay);
        }
    }
    else {
        scheduleAt(newScheduleTime, waitDelay);
    }
}

void LoRaMeshRouter::turnOffReceiver()
{
    LoRaRadio *loraRadio;
    loraRadio = check_and_cast<LoRaRadio*>(radio);
    loraRadio->setRadioMode(IRadio::RADIO_MODE_SLEEP);
}

MacAddress LoRaMeshRouter::getAddress()
{
    return address;
}

} // namespace inet
