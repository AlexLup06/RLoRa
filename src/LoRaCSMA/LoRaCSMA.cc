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
#include "../LoRaApp/LoRaRobotPacket_m.h"
#include "../helpers/generalHelpers.h"

#include "../messages/MessageInfoTag_m.h"
#include "../messages/BroadcastLeaderFragment_m.h"
#include "../messages/BroadcastFragment_m.h"
#include "../messages/NodeAnnounce_m.h"

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

        nodeId = intuniform(0, 16777216); //16^6 -1
        packetQueue = CustomPacketQueue();

        throughputTimer = new cMessage("throughputTimer");
        nodeAnnounce = new cMessage("Node Announce");
        endBackoff = new cMessage("Backoff");
        endData = new cMessage("Data");
        mediumStateChange = new cMessage("MediumStateChange");
        transmitSwitchDone = new cMessage("transmitSwitchDone");
        receptionStated = new cMessage("receptionStated");

        throughputSignal = registerSignal("throughputBps");
        effectiveThroughputSignal = registerSignal("effectiveThroughputBps");
        timeInQueue = registerSignal("timeInQueue");
        missionIdFragmentSent = registerSignal("missionIdFragmentSent");
        receivedMissionId = registerSignal("receivedMissionId");
        timeOfLastTrajectorySignal = registerSignal("timeOfLastTrajectorySignal");

        scheduleAt(intuniform(0, 1000) / 1000.0, nodeAnnounce);
        scheduleAt(simTime() + measurementInterval, throughputTimer);

        txQueue = getQueue(gate(upperLayerInGateId));

        fsm.setName("LoRaCSMA State Machine");
    }
    else if (stage == INITSTAGE_LINK_LAYER) {
        turnOnReceiver();
        fsm.setState(LISTENING);
    }
}

void LoRaCSMA::finish()
{
    cancelAndDelete(receptionStated);
    cancelAndDelete(transmitSwitchDone);
    cancelAndDelete(mediumStateChange);
    cancelAndDelete(endBackoff);
    cancelAndDelete(endData);
    cancelAndDelete(throughputTimer);
    cancelAndDelete(nodeAnnounce);

    receptionStated = nullptr;
    transmitSwitchDone = nullptr;
    nodeAnnounce = nullptr;
    mediumStateChange = nullptr;
    throughputTimer = nullptr;
    endBackoff = nullptr;
    endData = nullptr;
    mediumStateChange = nullptr;

    while (!packetQueue.isEmpty()) {
        auto *pkt = packetQueue.dequeuePacket();
        delete pkt;
    }

    currentTxFrame = nullptr;

    cSimulation *sim = getSimulation();
    cFutureEventSet *fes = sim->getFES();
    for (int i = 0; i < fes->getLength(); ++i) {
        cEvent *event = fes->get(i);
        cMessage *msg = dynamic_cast<cMessage*>(event);
        if (msg != nullptr) {
            cModule *mod = msg->getArrivalModule();
            if (msg && msg->isScheduled() && msg->getOwner() == this) {
                cancelAndDelete(msg);
                msg = nullptr;
            }
        }
    }
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
    const auto &payload = packet->peekAtFront<LoRaRobotPacket>();
    bool isMission = payload->isMission();
    int missionId = -2;
    if (isMission) {
        missionId = -1;
    }
    createBroadcastPacket(packet->getByteLength(), missionId, -1, isMission);

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
}

void LoRaCSMA::receiveSignal(cComponent *source, simsignal_t signalID, intval_t value, cObject *details)
{
    Enter_Method("%s", cComponent::getSignalName(signalID));

    if (signalID == IRadio::receptionStateChangedSignal) {
        IRadio::ReceptionState newRadioReceptionState = (IRadio::ReceptionState) value;
        handleWithFsm(mediumStateChange);
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
    int slots = intrand(cw);
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
    auto frameToSend = getCurrentTransmission();
    if (frameToSend != nullptr) {
        sendDown(frameToSend->dup());
    }

    if (idToAddedTimeMap.find(frameToSend->getId()) != idToAddedTimeMap.end()) {
        SimTime previousTime = idToAddedTimeMap[frameToSend->getId()];
        SimTime delta = simTime() - previousTime;
        emit(timeInQueue, delta);
    }

    auto typeTag = frameToSend->getTag<MessageInfoTag>();
    if (!typeTag->isNeighbourMsg()) {
        emit(missionIdFragmentSent, typeTag->getMissionId());
    }
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

void LoRaCSMA::createBroadcastPacket(int payloadSize, int missionId, int source, bool retransmit)
{
    auto headerPaket = new Packet("BroadcastLeaderFragment");
    auto headerPayload = makeShared<BroadcastLeaderFragment>();
    int messageId = headerPaket->getId();
    int fullPayloadSize = payloadSize;

    if (missionId == -1) {
        missionId = headerPaket->getId();
    }

    if (source == -1) {
        source = nodeId;
    }
    int currentPayloadSize = 0;
    if (payloadSize + BROADCAST_LEADER_FRAGMENT_META_SIZE > 255) {
        currentPayloadSize = 255 - BROADCAST_LEADER_FRAGMENT_META_SIZE;
        headerPayload->setChunkLength(B(255));
        headerPayload->setPayloadSize(currentPayloadSize);
        payloadSize = payloadSize - currentPayloadSize;
    }
    else {
        currentPayloadSize = payloadSize;
        headerPayload->setChunkLength(B(currentPayloadSize + BROADCAST_LEADER_FRAGMENT_META_SIZE));
        headerPayload->setPayloadSize(currentPayloadSize);
        payloadSize = 0;
    }
    headerPayload->setSize(fullPayloadSize);
    headerPayload->setMessageId(messageId);
    headerPayload->setMissionId(missionId);
    headerPayload->setSource(source);
    headerPayload->setHop(nodeId);
    headerPayload->setRetransmit(retransmit);
    headerPaket->insertAtBack(headerPayload);
    headerPaket->addTagIfAbsent<PacketProtocolTag>()->setProtocol(&Protocol::apskPhy);

    auto messageInfoTag = headerPaket->addTagIfAbsent<MessageInfoTag>();
    messageInfoTag->setIsNeighbourMsg(!retransmit);
    messageInfoTag->setMissionId(missionId);
    messageInfoTag->setIsHeader(true);
    messageInfoTag->setHasUsefulData(true);
    messageInfoTag->setPayloadSize(currentPayloadSize);

    encapsulate(headerPaket);

    bool trackQueueTime = packetQueue.enqueuePacket(headerPaket);
    if (trackQueueTime) {
        idToAddedTimeMap[headerPaket->getId()] = simTime();
    }

    // INFO: das nullte packet ist das was im leader direkt mitgeschickt wird
    int i = 1;
    while (payloadSize > 0) {
        auto fragmentPacket = new Packet("BroadcastFragmentPkt");
        auto fragmentPayload = makeShared<BroadcastFragment>();

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

        fragmentPayload->setMessageId(messageId);
        fragmentPayload->setMissionId(missionId);
        fragmentPayload->setSource(source);
        fragmentPayload->setFragmentId(i++);
        fragmentPacket->insertAtBack(fragmentPayload);
        fragmentPacket->addTagIfAbsent<PacketProtocolTag>()->setProtocol(&Protocol::apskPhy);

        auto messageInfoTag = fragmentPacket->addTagIfAbsent<MessageInfoTag>();
        messageInfoTag->setIsNeighbourMsg(!retransmit);
        messageInfoTag->setMissionId(missionId);
        messageInfoTag->setIsHeader(false);
        messageInfoTag->setHasUsefulData(true);
        messageInfoTag->setPayloadSize(currentPayloadSize);

        encapsulate(fragmentPacket);
        packetQueue.enqueuePacket(fragmentPacket);
        if (trackQueueTime) {
            idToAddedTimeMap[fragmentPacket->getId()] = simTime();
        }
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
    nodeAnnouncePacket->addTagIfAbsent<MessageInfoTag>()->setIsNeighbourMsg(false);
    encapsulate(nodeAnnouncePacket);
    packetQueue.enqueueNodeAnnounce(nodeAnnouncePacket);
}

void LoRaCSMA::handlePacket(Packet *packet)
{
    bytesReceivedInInterval += packet->getByteLength();
    auto chunk = packet->peekAtFront<inet::Chunk>();

    if (auto msg = dynamic_cast<const BroadcastLeaderFragment*>(chunk.get())) {
        int messageId = msg->getMessageId();
        int source = msg->getSource();
        int missionId = msg->getMissionId();
        bool isMissionMsg = msg->getRetransmit();

        if (!isMissionMsg && !incompleteNeighbourPktList.isNewIdHigher(source, messageId)) {
            delete packet;
            return;
        }

        if (isMissionMsg && !incompleteMissionPktList.isNewIdHigher(source, missionId)) {
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

        // wir senden schon mit dem ersten packet daten
        BroadcastFragment *fragmentPayload = new BroadcastFragment();
        fragmentPayload->setChunkLength(msg->getChunkLength());
        fragmentPayload->setPayloadSize(msg->getPayloadSize());
        fragmentPayload->setMessageId(messageId);
        fragmentPayload->setMissionId(missionId);
        fragmentPayload->setSource(source);
        fragmentPayload->setFragmentId(0);

        effectiveBytesReceivedInInterval += msg->getPayloadSize();

        Result result;
        if (isMissionMsg)
            result = incompleteMissionPktList.addToIncompletePacket(fragmentPayload);
        else
            result = incompleteNeighbourPktList.addToIncompletePacket(fragmentPayload);
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
        delete fragmentPayload;
    }
    else if (auto msg = dynamic_cast<const BroadcastFragment*>(chunk.get())) {
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

        effectiveBytesReceivedInInterval += packet->getByteLength() - BROADCAST_FRAGMENT_META_SIZE;

        if (result.isComplete) {
            if (result.sendUp) {
                cMessage *readyMsg = new cMessage("Ready");
                sendUp(readyMsg);
                retransmitPacket(result.completePacket);
            }
            if (isMissionMsg)
                incompleteMissionPktList.removePacketById(missionId);
            else
                incompleteNeighbourPktList.removePacketById(messageId);
        }
    }
    delete packet;
}

void LoRaCSMA::retransmitPacket(FragmentedPacket fragmentedPacket)
{
    if (!fragmentedPacket.retransmit || fragmentedPacket.sourceNode == nodeId) {
        return;
    }
    emit(receivedMissionId, fragmentedPacket.missionId);
    createBroadcastPacket(fragmentedPacket.size, fragmentedPacket.missionId, fragmentedPacket.sourceNode, fragmentedPacket.retransmit);
}

void LoRaCSMA::turnOnReceiver()
{
    LoRaRadio *loraRadio;
    loraRadio = check_and_cast<LoRaRadio*>(radio);
    loraRadio->setRadioMode(IRadio::RADIO_MODE_RECEIVER);
}

} /* namespace rlora */
