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

#include "Aloha.h"

namespace rlora {

Define_Module(Aloha);

Aloha::Aloha() :
        incompleteMissionPktList(true), incompleteNeighbourPktList(false)
{
}

Aloha::~Aloha()
{
}

/****************************************************************
 * Initialization functions.
 */
void Aloha::initialize(int stage)
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
        moreMessagesToSend = new cMessage("moreMessagesToSend");

        throughputSignal = registerSignal("throughputBps");
        effectiveThroughputSignal = registerSignal("effectiveThroughputBps");
        timeInQueue = registerSignal("timeInQueue");
        missionIdFragmentSent = registerSignal("missionIdFragmentSent");
        receivedMissionId = registerSignal("receivedMissionId");
        timeOfLastTrajectorySignal = registerSignal("timeOfLastTrajectorySignal");

        scheduleAt(simTime() + measurementInterval, throughputTimer);
        scheduleAt(intuniform(500, 1000) / 1000.0, nodeAnnounce);
    }
    else if (stage == INITSTAGE_LINK_LAYER) {
        turnOnReceiver();
        fsm.setState(LISTENING); // We are always in Listening state
    }
}

void Aloha::finish()
{
    cancelAndDelete(receptionStated);
    cancelAndDelete(transmitSwitchDone);
    cancelAndDelete(nodeAnnounce);
    cancelAndDelete(endTransmission);
    cancelAndDelete(mediumStateChange);
    cancelAndDelete(droppedPacket);
    cancelAndDelete(throughputTimer);
    cancelAndDelete(moreMessagesToSend);

    moreMessagesToSend = nullptr;
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

void Aloha::configureNetworkInterface()
{
    networkInterface->setDatarate(bitrate);
    networkInterface->setMacAddress(address);

    networkInterface->setMtu(std::numeric_limits<int>::quiet_NaN());
    networkInterface->setMulticast(true);
    networkInterface->setBroadcast(true);
    networkInterface->setPointToPoint(false);
}

queueing::IPassivePacketSource* Aloha::getProvider(cGate *gate)
{
    return (gate->getId() == upperLayerInGateId) ? txQueue.get() : nullptr;
}

/****************************************************************
 * Message handling functions.
 */
void Aloha::handleSelfMessage(cMessage *msg)
{
    handleWithFsm(msg);
}

void Aloha::handleUpperPacket(Packet *packet)
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

void Aloha::handleLowerPacket(Packet *msg)
{
    if (fsm.getState() == RECEIVING) {
        handleWithFsm(msg);
    }
    else {
        EV << "Received MSG while not in RECEIVING state" << endl;
        delete msg;
    }
}

void Aloha::handleCanPullPacketChanged(cGate *gate)
{
    Enter_Method("handleCanPullPacketChanged");
    Packet *packet = dequeuePacket();
    handleUpperMessage(packet);
}

void Aloha::handlePullPacketProcessed(Packet *packet, cGate *gate, bool successful)
{
    Enter_Method("handlePullPacketProcessed");
    throw cRuntimeError("Not supported callback");
}

void Aloha::handleWithFsm(cMessage *msg)
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
                currentTxFrame!=nullptr && ! isReceiving(),
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
    if (!packetQueue.isEmpty() && currentTxFrame == nullptr) {
        currentTxFrame = packetQueue.dequeuePacket();
        handleWithFsm(moreMessagesToSend);
    }
}

void Aloha::handlePacket(Packet *packet)
{
    bytesReceivedInInterval += packet->getByteLength();
    Packet *messageFrame = decapsulate(packet);
    auto chunk = messageFrame->peekAtFront<inet::Chunk>();

    if (auto msg = dynamic_cast<const BroadcastLeaderFragment*>(chunk.get())) {
        effectiveBytesReceivedInInterval += packet->getByteLength() - BROADCAST_LEADER_FRAGMENT_META_SIZE;

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

void Aloha::receiveSignal(cComponent *source, simsignal_t signalID, intval_t value, cObject *details)
{
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

Packet* Aloha::encapsulate(Packet *msg)
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

Packet* Aloha::decapsulate(Packet *frame)
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
void Aloha::sendDataFrame()
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
        emit(missionIdFragmentSent, typeTag->getMissionId());
    }
}

/****************************************************************
 * Helper functions.
 */
void Aloha::finishCurrentTransmission()
{
    deleteCurrentTxFrame();
}

Packet* Aloha::getCurrentTransmission()
{
    ASSERT(currentTxFrame != nullptr);
    return currentTxFrame;
}

void Aloha::retransmitPacket(FragmentedPacket fragmentedPacket)
{
    // jedes feld muss gleich sein vom fragemntierten packet außer lastHop
    if (!fragmentedPacket.retransmit || fragmentedPacket.sourceNode == nodeId) {
        return;
    }

    EV << "Retransmit Packet!" << endl;
    emit(receivedMissionId, fragmentedPacket.missionId);
    createBroadcastPacket(fragmentedPacket.size, fragmentedPacket.missionId, fragmentedPacket.sourceNode, fragmentedPacket.retransmit);
}

void Aloha::createBroadcastPacket(int payloadSize, int missionId, int source, bool isMission)
{
    auto headerPaket = new Packet("BroadcastLeaderFragment");
    auto headerPayload = makeShared<BroadcastLeaderFragment>();
    int messageId = headerPaket->getId();
    int fullPayloadSize = payloadSize;

    if (missionId == -1 && isMission) {
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
    headerPayload->setRetransmit(isMission);
    headerPaket->insertAtBack(headerPayload);
    headerPaket->addTagIfAbsent<PacketProtocolTag>()->setProtocol(&Protocol::apskPhy);

    auto messageInfoTag = headerPaket->addTagIfAbsent<MessageInfoTag>();
    messageInfoTag->setIsNeighbourMsg(!isMission);
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
        messageInfoTag->setIsNeighbourMsg(!isMission);
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

void Aloha::announceNodeId(int respond)
{
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
    messageInfoTag->setIsNodeAnnounce(true);

    encapsulate(nodeAnnouncePacket);
    packetQueue.enqueueNodeAnnounce(nodeAnnouncePacket);
}

bool Aloha::isReceiving()
{
    return radio->getReceptionState() == IRadio::RECEPTION_STATE_RECEIVING;
}

void Aloha::turnOnReceiver()
{

    LoRaRadio *loraRadio;
    loraRadio = check_and_cast<LoRaRadio*>(radio);
    loraRadio->setRadioMode(IRadio::RADIO_MODE_RECEIVER);
}

void Aloha::turnOffReceiver()
{
    LoRaRadio *loraRadio;
    loraRadio = check_and_cast<LoRaRadio*>(radio);
    loraRadio->setRadioMode(IRadio::RADIO_MODE_SLEEP);
}

MacAddress Aloha::getAddress()
{
    return address;
}

} // namespace inet
