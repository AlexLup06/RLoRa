/*
 * LoRaCSMA.cc
 *
 *  Created on: 24 Mar 2025
 *      Author: alexanderlupatsiy
 */

#include "LoRaCSMA.h"

#include "inet/common/ModuleAccess.h"
#include "inet/common/ProtocolGroup.h"
#include "inet/common/ProtocolTag_m.h"
#include "inet/common/checksum/EthernetCRC.h"
#include "inet/common/packet/chunk/ByteCountChunk.h"
#include "inet/linklayer/common/InterfaceTag_m.h"
#include "inet/linklayer/common/MacAddressTag_m.h"
#include "inet/linklayer/common/UserPriority.h"
#include "inet/linklayer/common/UserPriorityTag_m.h"

#include "../LoRa/LoRaTagInfo_m.h"
#include "../helpers/generalHelpers.h"
#include "../helpers/MessageTypeTag_m.h"

#include "./BroadcastLeaderFragment_m.h"
#include "../LoRaMeshRouter/BroadcastFragment_m.h"
#include "../LoRaMeshRouter/NodeAnnounce_m.h"

namespace rlora {

using namespace inet::physicallayer;
using namespace inet;

Define_Module(LoRaCSMA);

LoRaCSMA::~LoRaCSMA()
{
    cancelAndDelete(endBackoff);
    cancelAndDelete(endData);
    cancelAndDelete(mediumStateChange);
}

/****************************************************************
 * Initialization functions.
 */
void LoRaCSMA::initialize(int stage)
{
    MacProtocolBase::initialize(stage);
    if (stage == INITSTAGE_LOCAL) {
        EV << "Initializing stage 0\n";
        fcsMode = parseFcsMode(par("fcsMode"));
        useAck = par("useAck");
        bitrate = par("bitrate");
        headerLength = B(par("headerLength"));
        if (headerLength > B(255))
            throw cRuntimeError("The specified headerLength is too large");
        if (headerLength < makeShared<CsmaCaMacDataHeader>()->getChunkLength())
            throw cRuntimeError("The specified headerLength is too short");
        ackLength = B(par("ackLength"));
        if (ackLength > B(255))
            throw cRuntimeError("The specified ackLength is too large");
        if (ackLength < makeShared<CsmaCaMacAckHeader>()->getChunkLength())
            throw cRuntimeError("The specified ackLength is too short");
        ackTimeout = par("ackTimeout");
        slotTime = par("slotTime");
        sifsTime = par("sifsTime");
        difsTime = par("difsTime");
        cwMin = par("cwMin");
        cwMax = par("cwMax");
        cwMulticast = par("cwMulticast");
        retryLimit = par("retryLimit");

        // meine aenderungen {
        cModule *radioModule = getModuleFromPar<cModule>(par("radioModule"), this);
        radioModule->subscribe(IRadio::receptionStateChangedSignal, this);
        radioModule->subscribe(IRadio::transmissionStateChangedSignal, this);
        radioModule->subscribe(LoRaRadio::droppedPacket, this);
        radio = check_and_cast<IRadio*>(radioModule);
        loRaRadio = check_and_cast<LoRaRadio*>(radioModule);

        const char *addressString = par("address");
        if (!strcmp(addressString, "auto")) {
            address = MacAddress::generateAutoAddress();
            par("address").setStringValue(address.str().c_str());
        }
        else {
            address.setAddress(addressString);
        }
        nodeAnnounce = new cMessage("Node Announce");
        nodeId = intuniform(0, 16777216); //16^6 -1
//        scheduleAt(intuniform(0, 1000) / 1000.0, nodeAnnounce);
        packetQueue = CustomPacketQueue();
        // }

        // initialize self messages
        endBackoff = new cMessage("Backoff");
        endData = new cMessage("Data");
        mediumStateChange = new cMessage("MediumStateChange");
        transmitSwitchDone = new cMessage("transmitSwitchDone");
        receptionStated = new cMessage("receptionStated");

        // set up internal queue
        txQueue = getQueue(gate(upperLayerInGateId));

        // state variables
        fsm.setName("LoRaCSMA State Machine");
        backoffPeriod = -1;
        retryCounter = 0;

        // statistics
        numRetry = 0;
        numSentWithoutRetry = 0;
        numGivenUp = 0;
        numCollision = 0;
        numSent = 0;
        numReceived = 0;
        numSentBroadcast = 0;
        numReceivedBroadcast = 0;

        // initialize watches
        WATCH(fsm);
        WATCH(backoffPeriod);
        WATCH(retryCounter);
        WATCH(numRetry);
        WATCH(numSentWithoutRetry);
        WATCH(numGivenUp);
        WATCH(numCollision);
        WATCH(numSent);
        WATCH(numReceived);
        WATCH(numSentBroadcast);
        WATCH(numReceivedBroadcast);
    }
    else if (stage == INITSTAGE_LINK_LAYER) {
        turnOnReceiver();
        fsm.setState(LISTENING);
    }
}

void LoRaCSMA::finish()
{
    recordScalar("numRetry", numRetry);
    recordScalar("numSentWithoutRetry", numSentWithoutRetry);
    recordScalar("numGivenUp", numGivenUp);
    recordScalar("numCollision", numCollision);
    recordScalar("numSent", numSent);
    recordScalar("numReceived", numReceived);
    recordScalar("numSentBroadcast", numSentBroadcast);
    recordScalar("numReceivedBroadcast", numReceivedBroadcast);
}

void LoRaCSMA::configureNetworkInterface()
{
    MacAddress address = parseMacAddressParameter(par("address"));

    // data rate
    networkInterface->setDatarate(bitrate);

    // generate a link-layer address to be used as interface token for IPv6
    networkInterface->setMacAddress(address);
    networkInterface->setInterfaceToken(address.formInterfaceIdentifier());

    // capabilities
    networkInterface->setMtu(par("mtu"));
    networkInterface->setMulticast(true);
    networkInterface->setBroadcast(true);
    networkInterface->setPointToPoint(false);
}

/****************************************************************
 * Message handling functions.
 */
void LoRaCSMA::handleSelfMessage(cMessage *msg)
{
    handleWithFsm(msg);
}

void LoRaCSMA::handleUpperPacket(Packet *packet)
{
    bool retransmit = intuniform(0, 100) < 100;
    createBroadcastPacket(packet->getByteLength(), -1, -1, -1, retransmit);

    if (currentTxFrame == nullptr) {
        Packet *packetToSend = packetQueue.dequeuePacket();
        currentTxFrame = packetToSend;
    }
    handleWithFsm(packet);
    delete packet;
}

void LoRaCSMA::handleLowerPacket(Packet *packet)
{
    handleWithFsm(packet);
}

void LoRaCSMA::handleWithFsm(cMessage *msg)
{
    Packet *frame = dynamic_cast<Packet*>(msg);

    if (msg == nodeAnnounce) {
        announceNodeId(0);
        scheduleAfter(5, nodeAnnounce);
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
        FSMA_Event_Transition(Defer-Transmit,
                currentTxFrame != nullptr && !isMediumFree(),
                DEFER,
        );
        FSMA_Event_Transition(Start-Backoff,
                currentTxFrame != nullptr && isMediumFree(),
                BACKOFF,
        );
        FSMA_Event_Transition(Start-Receive,
                msg == mediumStateChange && isReceiving(),
                RECEIVE,
        );
    }
    FSMA_State(DEFER)
    {
        FSMA_Event_Transition(Start-Backoff,
                msg == mediumStateChange && isMediumFree(),
                BACKOFF,
        );
        FSMA_Event_Transition(Start-Receive,
                msg == mediumStateChange && isReceiving(),
                RECEIVE,
        );
    }
    FSMA_State(BACKOFF)
    {
        FSMA_Enter(scheduleBackoffTimer());
        FSMA_Event_Transition(Start-Transmit,
                msg == endBackoff,
                TRANSMIT,
                invalidateBackoffPeriod();
        );
        FSMA_Event_Transition(Start-Receive,
                msg == mediumStateChange && isReceiving(),
                RECEIVE,
                cancelBackoffTimer();
                decreaseBackoffPeriod();
        );
        FSMA_Event_Transition(Defer-Backoff,
                msg == mediumStateChange && !isMediumFree(),
                DEFER,
                cancelBackoffTimer();
                decreaseBackoffPeriod();
        );
    }
    FSMA_State(TRANSMIT)
    {
        FSMA_Enter(radio->setRadioMode(IRadio::RADIO_MODE_TRANSMITTER));
        FSMA_Event_Transition(Transmit-Listening,
                msg == transmitSwitchDone,
                TRANSMIT,
                sendDataFrame();
        );
        FSMA_Event_Transition(Transmit-Listening,
                msg == endData,
                SWITCHING,
                finishCurrentTransmission();
        );
    }
    FSMA_State(RECEIVE)
    {
        FSMA_Event_Transition(Receive-Broadcast,
                isLowerMessage(msg),
                LISTENING,
                decapsulate(frame);
                handlePacket(frame);
                numReceivedBroadcast++;
        );
    }
}
    if (fsm.getState() == LISTENING) {
        if (isReceiving())
            handleWithFsm(mediumStateChange);
        else if (currentTxFrame != nullptr && isMediumFree()) {
            handleWithFsm(currentTxFrame);
        }
        else if (!packetQueue.isEmpty() && isMediumFree()) {
            currentTxFrame = packetQueue.dequeuePacket();
            handleWithFsm(currentTxFrame);
        }
    }
//    if (isLowerMessage(msg) && frame->getOwner() == this && endSifs->getContextPointer() != frame)
//        delete frame;
    getDisplayString().setTagArg("t", 0, fsm.getStateName());
}

void LoRaCSMA::receiveSignal(cComponent *source, simsignal_t signalID, intval_t value, cObject *details)
{
    Enter_Method("%s", cComponent::getSignalName(signalID));

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
    else if (signalID == IRadio::transmissionStateChangedSignal) {
        IRadio::TransmissionState newRadioTransmissionState = (IRadio::TransmissionState) value;
        if (transmissionState == IRadio::TRANSMISSION_STATE_TRANSMITTING && newRadioTransmissionState == IRadio::TRANSMISSION_STATE_IDLE) {
            transmissionState = newRadioTransmissionState;
            handleWithFsm(endData);
        }

        if (transmissionState == IRadio::TRANSMISSION_STATE_UNDEFINED && newRadioTransmissionState == IRadio::TRANSMISSION_STATE_IDLE) {
            transmissionState = newRadioTransmissionState;
            handleWithFsm(transmitSwitchDone);
        }
        transmissionState = newRadioTransmissionState;
    }
}

void LoRaCSMA::encapsulate(Packet *msg)
{
    auto macFrame = makeShared<LoRaMacFrame>();
    macFrame->setChunkLength(B(0));
    msg->setArrival(msg->getArrivalModuleId(), msg->getArrivalGateId());

    auto tag = msg->addTagIfAbsent<LoRaTag>();
    tag->setBandwidth(loRaRadio->loRaBW);
    tag->setCenterFrequency(loRaRadio->loRaCF);
    tag->setSpreadFactor(loRaRadio->loRaSF);
    tag->setCodeRendundance(loRaRadio->loRaCR);
    tag->setPower(mW(math::dBmW2mW(loRaRadio->loRaTP)));

    macFrame->setTransmitterAddress(address);
    macFrame->setLoRaTP(loRaRadio->loRaTP);
    macFrame->setLoRaCF(loRaRadio->loRaCF);
    macFrame->setLoRaSF(loRaRadio->loRaSF);
    macFrame->setLoRaBW(loRaRadio->loRaBW);
    macFrame->setLoRaCR(loRaRadio->loRaCR);
    macFrame->setSequenceNumber(0);
    macFrame->setReceiverAddress(MacAddress::BROADCAST_ADDRESS);

    macFrame->setLoRaUseHeader(tag->getUseHeader());

    msg->insertAtFront(macFrame);
}

void LoRaCSMA::decapsulate(Packet *frame)
{
    auto loraHeader = frame->popAtFront<LoRaMacFrame>();
    frame->addTagIfAbsent<MacAddressInd>()->setSrcAddress(loraHeader->getTransmitterAddress());
    frame->addTagIfAbsent<MacAddressInd>()->setDestAddress(loraHeader->getReceiverAddress());
    frame->addTagIfAbsent<InterfaceInd>()->setInterfaceId(networkInterface->getInterfaceId());
}

/****************************************************************
 * Timer functions.
 */

void LoRaCSMA::invalidateBackoffPeriod()
{
    backoffPeriod = -1;
}

bool LoRaCSMA::isInvalidBackoffPeriod()
{
    return backoffPeriod == -1;
}

void LoRaCSMA::generateBackoffPeriod()
{
    ASSERT(0 <= retryCounter && retryCounter <= retryLimit);
    int cw;
    // wir sind zwar immer multicast aber wir berechnen normales contention window
//    if (getCurrentTransmission()->peekAtFront<CsmaCaMacHeader>()->getReceiverAddress().isMulticast())
//        cw = cwMulticast;
//    else
    cw = std::min(cwMax, (cwMin + 1) * (1 << retryCounter) - 1);
    int slots = intrand(cw + 1);
    backoffPeriod = slots * slotTime;
    ASSERT(backoffPeriod >= 0);
}

void LoRaCSMA::decreaseBackoffPeriod()
{
    simtime_t elapsedBackoffTime = simTime() - endBackoff->getSendingTime();
    backoffPeriod -= ((int) (elapsedBackoffTime / slotTime)) * slotTime;
    ASSERT(backoffPeriod >= 0);
}

void LoRaCSMA::scheduleBackoffTimer()
{
    if (isInvalidBackoffPeriod())
        generateBackoffPeriod();
    scheduleAfter(backoffPeriod, endBackoff);
}

void LoRaCSMA::cancelBackoffTimer()
{
    cancelEvent(endBackoff);
}

/****************************************************************
 * Frame sender functions.
 */
void LoRaCSMA::sendDataFrame()
{
//    radio->setRadioMode(IRadio::RADIO_MODE_TRANSMITTER);
//    sendDown(frameToSend->dup());
    auto frameToSend = getCurrentTransmission();
    if (frameToSend != nullptr) {
        sendDown(frameToSend->dup());
    }
    frameToSend = nullptr;
    delete frameToSend;
}

/****************************************************************
 * Helper functions.
 */
void LoRaCSMA::finishCurrentTransmission()
{
    deleteCurrentTxFrame();
    resetTransmissionVariables();
}

Packet* LoRaCSMA::getCurrentTransmission()
{
    ASSERT(currentTxFrame != nullptr);
    return currentTxFrame;
}

void LoRaCSMA::resetTransmissionVariables()
{
    backoffPeriod = -1;
    retryCounter = 0;
}

void LoRaCSMA::emitPacketDropSignal(Packet *frame, PacketDropReason reason, int limit)
{
    PacketDropDetails details;
    details.setReason(reason);
    details.setLimit(limit);
    emit(packetDroppedSignal, frame, &details);
}

bool LoRaCSMA::isMediumFree()
{
    return radio->getReceptionState() == IRadio::RECEPTION_STATE_IDLE;
}

bool LoRaCSMA::isReceiving()
{
    return radio->getReceptionState() == IRadio::RECEPTION_STATE_RECEIVING;
}

void LoRaCSMA::handleStopOperation(LifecycleOperation *operation)
{
    MacProtocolBase::handleStopOperation(operation);
    resetTransmissionVariables();
}

void LoRaCSMA::handleCrashOperation(LifecycleOperation *operation)
{
    MacProtocolBase::handleCrashOperation(operation);
    resetTransmissionVariables();
}

void LoRaCSMA::processUpperPacket()
{
    Packet *packet = dequeuePacket();
    handleUpperPacket(packet);
}

queueing::IPassivePacketSource* LoRaCSMA::getProvider(cGate *gate)
{
    return (gate->getId() == upperLayerInGateId) ? txQueue.get() : nullptr;
}

void LoRaCSMA::handleCanPullPacketChanged(cGate *gate)
{
    Enter_Method("handleCanPullPacketChanged");
    Packet *packet = dequeuePacket();
    handleUpperMessage(packet);
}

void LoRaCSMA::handlePullPacketProcessed(Packet *packet, cGate *gate, bool successful)
{
    Enter_Method("handlePullPacketProcessed");
    throw cRuntimeError("Not supported callback");
}

MacAddress LoRaCSMA::getAddress()
{
    return address;
}

void LoRaCSMA::createBroadcastPacket(int packetSize, int messageId, int hopId, int source, bool retransmit)
{
    auto headerPaket = new Packet("BroadcastLeaderFragment");
    auto headerPayload = makeShared<BroadcastLeaderFragment>();

    if (messageId == -1) {
        messageId = headerPaket->getId();
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
    headerPayload->setSource(source);
    headerPayload->setHop(hopId);
    headerPayload->setRetransmit(retransmit);
    headerPaket->insertAtBack(headerPayload);
    headerPaket->addTagIfAbsent<PacketProtocolTag>()->setProtocol(&Protocol::apskPhy);
    auto messageTypeTag = headerPaket->addTagIfAbsent<MessageTypeTag>();
    messageTypeTag->setIsNeighbourMsg(!retransmit);
    messageTypeTag->setIsHeader(false);

    encapsulate(headerPaket);

    packetQueue.enqueuePacket(headerPaket, true);

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
        fragmentPayload->setSource(source);
        fragmentPayload->setFragment(i++);
        fragmentPacket->insertAtBack(fragmentPayload);
        fragmentPacket->addTagIfAbsent<PacketProtocolTag>()->setProtocol(&Protocol::apskPhy);
        auto messageTypeTag = fragmentPacket->addTagIfAbsent<MessageTypeTag>();
        messageTypeTag->setIsNeighbourMsg(!retransmit);
        messageTypeTag->setIsHeader(false);

        encapsulate(fragmentPacket);
        packetQueue.enqueuePacket(fragmentPacket);

    }
}

void LoRaCSMA::announceNodeId(int respond)
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
    encapsulate(nodeAnnouncePacket);
    packetQueue.enqueuePacketAtPosition(nodeAnnouncePacket, 0);
}

void LoRaCSMA::handlePacket(Packet *packet)
{
    auto chunk = packet->peekAtFront<inet::Chunk>();
    if (auto msg = dynamic_cast<const NodeAnnounce*>(chunk.get())) {
        EV << "Received NodeAnnounce" << endl;
        // einfach kopiert aus Julians implementierung
        if (nodeId != msg->getNodeId()) {

            if (msg->getRespond() == 1) {
                announceNodeId(0);
            }
        }

    }
    else if (auto msg = dynamic_cast<const BroadcastLeaderFragment*>(chunk.get())) {
        EV << "Received BroadcastLeaderFragment" << endl;
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

        // wir senden schon mit dem ersten packet daten
        BroadcastFragment *fragmentPayload = new BroadcastFragment();
        fragmentPayload->setChunkLength(msg->getChunkLength());
        fragmentPayload->setPayloadSize(msg->getPayloadSize());
        fragmentPayload->setMessageId(messageId);
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
        throw cRuntimeError("Received Unknown message: ");

    }
    delete packet;
}

void LoRaCSMA::retransmitPacket(FragmentedPacket fragmentedPacket)
{
    if (!fragmentedPacket.retransmit || fragmentedPacket.sourceNode == nodeId) {
        return;
    }
    // jedes feld muss gleich sein vom fragemntierten packet außer lastHop
    createBroadcastPacket(fragmentedPacket.size, fragmentedPacket.messageId, nodeId, fragmentedPacket.sourceNode, fragmentedPacket.retransmit);
}

void LoRaCSMA::turnOnReceiver()
{
    LoRaRadio *loraRadio;
    loraRadio = check_and_cast<LoRaRadio*>(radio);
    loraRadio->setRadioMode(IRadio::RADIO_MODE_RECEIVER);
}

} /* namespace rlora */
