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

namespace rlora {

using namespace inet::physicallayer;
using namespace inet;

Define_Module(LoRaCSMA);

LoRaCSMA::~LoRaCSMA()
{
    if (endSifs != nullptr)
        delete static_cast<Packet*>(endSifs->getContextPointer());
    cancelAndDelete(endSifs);
    cancelAndDelete(endDifs);
    cancelAndDelete(endBackoff);
    cancelAndDelete(endAckTimeout);
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
//        radio = check_and_cast<IRadio*>(radioModule);
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

        // }

        // initialize self messages
        endSifs = new cMessage("SIFS");
        endDifs = new cMessage("DIFS");
        endBackoff = new cMessage("Backoff");
        endAckTimeout = new cMessage("AckTimeout");
        endData = new cMessage("Data");
        mediumStateChange = new cMessage("MediumStateChange");

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
        // subscribe for the information of the carrier sense
        radio.reference(this, "radioModule", true);
//        cModule *radioModule = check_and_cast<cModule*>(radio.get());
//        radioModule->subscribe(IRadio::receptionStateChangedSignal, this);
//        radioModule->subscribe(IRadio::transmissionStateChangedSignal, this);
        radio->setRadioMode(IRadio::RADIO_MODE_RECEIVER);
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
    EV << "received self message: " << msg << endl;
    handleWithFsm(msg);
}

void LoRaCSMA::handleUpperPacket(Packet *packet)
{
//    auto destAddress = packet->getTag<MacAddressReq>()->getDestAddress();
//    ASSERT(!destAddress.isUnspecified());
//    EV << "frame " << packet << " received from higher layer" << endl;
//    packet->addTagIfAbsent<PacketProtocolTag>()->setProtocol(&Protocol::apskPhy);

//    encapsulate(packet);
//    if (currentTxFrame != nullptr)
//        throw cRuntimeError("Model error: incomplete transmission exists");
//    currentTxFrame = packet;
//    handleWithFsm(currentTxFrame);

    bool isNeighbourMsg = intuniform(0, 100) < 10;
    createBroadcastPacket(packet->getByteLength(), -1, -1, -1, true, isNeighbourMsg);

    if (currentTxFrame == nullptr) {
        Packet *packetToSend = packetQueue.dequeuePacket();
        currentTxFrame = packetToSend;
    }
    handleWithFsm(packet);
}

void LoRaCSMA::handleLowerPacket(Packet *packet)
{
    EV << "received message from lower layer: " << packet << endl;
    handleWithFsm(packet);
}

void LoRaCSMA::handleWithFsm(cMessage *msg)
{
    EV << "is upper layer:" << (msg->getArrivalGateId() == upperLayerInGateId) << endl;
    EV << "radio reception state:" << radio->getReceptionState() << endl;
    Packet *frame = dynamic_cast<Packet*>(msg);

    if (msg == nodeAnnounce) {
        announceNodeId(0);
        scheduleAfter(5, nodeAnnounce);
    }

    FSMA_Switch(fsm){
    FSMA_State(IDLE)
    {
        FSMA_Event_Transition(Defer-Transmit,
                isUpperMessage(msg) && !isMediumFree(),
                DEFER,
                EV<<"not free"<<endl;
        );
        FSMA_Event_Transition(Start-Backoff,
                isUpperMessage(msg) && isMediumFree() && !useAck,
                BACKOFF,
                EV<<"going to waitdifs"<<endl;
        );
        FSMA_Event_Transition(Start-Difs,
                isUpperMessage(msg) && isMediumFree() && useAck,
                WAITDIFS,
                EV<<"going to waitdifs"<<endl;
        );
        FSMA_Event_Transition(Start-Receive,
                msg == mediumStateChange && isReceiving(),
                RECEIVE,
        );
    }
    FSMA_State(DEFER)
    {
        FSMA_Event_Transition(Start-Backoff,
                msg == mediumStateChange && isMediumFree() && !useAck,
                BACKOFF,
        );
        FSMA_Event_Transition(Start-Difs,
                msg == mediumStateChange && isMediumFree() && useAck,
                WAITDIFS,
        );
        FSMA_Event_Transition(Start-Receive,
                msg == mediumStateChange && isReceiving(),
                RECEIVE,
        );
    }
    FSMA_State(WAITDIFS)
    {
        FSMA_Enter(scheduleDifsTimer();EV<<"scheduled difs timer"<<endl;);
        FSMA_Event_Transition(Start-Backoff,
                msg == endDifs,
                BACKOFF,
                EV<<"going to backoff"<<endl;
        );
        FSMA_Event_Transition(Start-Receive,
                msg == mediumStateChange && isReceiving(),
                RECEIVE,
                cancelDifsTimer();
        );
        FSMA_Event_Transition(Defer-Difs,
                msg == mediumStateChange && !isMediumFree(),
                DEFER,
                cancelDifsTimer();
        );
    }
    FSMA_State(BACKOFF)
    {
        FSMA_Enter(scheduleBackoffTimer();EV<<"schedule backoff timer"<<endl;);
        FSMA_Event_Transition(Start-Transmit,
                msg == endBackoff,
                TRANSMIT,
                invalidateBackoffPeriod();
                EV<<"going to transmit"<<endl;
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
        FSMA_Enter(EV<<"sending"<<endl;sendDataFrame(getCurrentTransmission()));
        FSMA_Event_Transition(Transmit-Broadcast,
                msg == endData && isBroadcast(getCurrentTransmission()),
                IDLE,
                finishCurrentTransmission();
                numSentBroadcast++;
        );
        FSMA_Event_Transition(Transmit-Unicast-No-Ack,
                msg == endData && !useAck && !isBroadcast(getCurrentTransmission()),
                IDLE,
                finishCurrentTransmission();
                numSent++;
        );
        FSMA_Event_Transition(Transmit-Unicast-Use-Ack,
                msg == endData && useAck && !isBroadcast(getCurrentTransmission()),
                WAITACK,
        );
    }
    FSMA_State(WAITACK)
    {
        FSMA_Enter(scheduleAckTimeout(getCurrentTransmission()));
        FSMA_Event_Transition(Receive-Ack,
                isLowerMessage(msg) && isFcsOk(frame) && isForUs(frame) && isAck(frame),
                IDLE,
                if (retryCounter == 0) numSentWithoutRetry++;
                numSent++;
                cancelAckTimer();
                finishCurrentTransmission();
        );
        FSMA_Event_Transition(Give-Up-Transmission,
                msg == endAckTimeout && retryCounter == retryLimit,
                IDLE,
                giveUpCurrentTransmission();
        );
        FSMA_Event_Transition(Retry-Transmission,
                msg == endAckTimeout,
                IDLE,
                retryCurrentTransmission();
        );
    }
    FSMA_State(RECEIVE)
    {
        FSMA_Event_Transition(Receive-Bit-Error,
                isLowerMessage(msg) && !isFcsOk(frame),
                IDLE,
                numCollision++;
                emitPacketDropSignal(frame, INCORRECTLY_RECEIVED);
        );
        FSMA_Event_Transition(Receive-Unexpected-Ack,
                isLowerMessage(msg) && isAck(frame),
                IDLE,
        );
        FSMA_Event_Transition(Receive-Broadcast,
                isLowerMessage(msg) && isBroadcast(frame),
                IDLE,
                decapsulate(frame);
                handlePacket(frame);
                numReceivedBroadcast++;
        );
        FSMA_Event_Transition(Receive-Unicast-No-Ack,
                isLowerMessage(msg) && isForUs(frame) && !useAck,
                IDLE,
                decapsulate(frame);
                handlePacket(frame);
                numReceived++;
        );
        FSMA_Event_Transition(Receive-Unicast-Use-Ack,
                isLowerMessage(msg) && isForUs(frame) && useAck,
                WAITSIFS,
                auto frameCopy = frame->dup();
                decapsulate(frameCopy);
                handlePacket(frameCopy);
                numReceived++;
        );
        FSMA_Event_Transition(Receive-Unicast-Not-For-Us,
                isLowerMessage(msg) && !isForUs(frame),
                IDLE,
                emitPacketDropSignal(frame, NOT_ADDRESSED_TO_US, retryLimit);
        );
    }
    FSMA_State(WAITSIFS)
    {
        FSMA_Enter(scheduleSifsTimer(frame));
        FSMA_Event_Transition(Transmit-Ack,
                msg == endSifs,
                IDLE,
                sendAckFrame();
        );
    }
}
    if (fsm.getState() == IDLE) {
        if (isReceiving())
            handleWithFsm(mediumStateChange);
//        else if (currentTxFrame != nullptr)
//            handleWithFsm(currentTxFrame);
//        else if (!txQueue->isEmpty()) {
//            processUpperPacket();
//        }
        else if (currentTxFrame != nullptr) {
            handleWithFsm(currentTxFrame);
        }
        else if (!packetQueue.isEmpty()) {
            currentTxFrame = packetQueue.dequeuePacket();
            handleWithFsm(currentTxFrame);
        }
    }
    if (isLowerMessage(msg) && frame->getOwner() == this && endSifs->getContextPointer() != frame)
        delete frame;
    getDisplayString().setTagArg("t", 0, fsm.getStateName());
}

void LoRaCSMA::receiveSignal(cComponent *source, simsignal_t signalID, intval_t value, cObject *details)
{
    Enter_Method("%s", cComponent::getSignalName(signalID));

    if (signalID == IRadio::receptionStateChangedSignal)
        handleWithFsm(mediumStateChange);
    else if (signalID == IRadio::transmissionStateChangedSignal) {
        IRadio::TransmissionState newRadioTransmissionState = static_cast<IRadio::TransmissionState>(value);
        if (transmissionState == IRadio::TRANSMISSION_STATE_TRANSMITTING && newRadioTransmissionState == IRadio::TRANSMISSION_STATE_IDLE) {
            handleWithFsm(endData);
            radio->setRadioMode(IRadio::RADIO_MODE_RECEIVER);
        }
        transmissionState = newRadioTransmissionState;
    }
}

void LoRaCSMA::encapsulate(Packet *msg)
{
    auto macTrailer = makeShared<CsmaCaMacTrailer>();
    macTrailer->setFcsMode(fcsMode);
    if (fcsMode == FCS_COMPUTED)
        macTrailer->setFcs(computeFcs(msg->peekAllAsBytes()));
    msg->insertAtBack(macTrailer);

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
void LoRaCSMA::scheduleSifsTimer(Packet *frame)
{
    EV << "scheduling SIFS timer\n";
    endSifs->setContextPointer(frame);
    scheduleAfter(sifsTime, endSifs);
}

void LoRaCSMA::scheduleDifsTimer()
{
    EV << "scheduling DIFS timer\n";
    scheduleAfter(difsTime, endDifs);
}

void LoRaCSMA::cancelDifsTimer()
{
    EV << "canceling DIFS timer\n";
    cancelEvent(endDifs);
}

void LoRaCSMA::scheduleAckTimeout(Packet *frameToSend)
{
    EV << "scheduling ACK timeout\n";
    scheduleAfter(ackTimeout, endAckTimeout);
}

void LoRaCSMA::cancelAckTimer()
{
    EV << "canceling ACK timer\n";
    cancelEvent(endAckTimeout);
}

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
    EV << "generating backoff slot number for retry: " << retryCounter << endl;
    int cw;
    // wir sind zwar immer multicast aber wir berechnen normales contention window
//    if (getCurrentTransmission()->peekAtFront<CsmaCaMacHeader>()->getReceiverAddress().isMulticast())
//        cw = cwMulticast;
//    else
    cw = std::min(cwMax, (cwMin + 1) * (1 << retryCounter) - 1);
    int slots = intrand(cw + 1);
    EV << "generated backoff slot number: " << slots << " , cw: " << cw << endl;
    backoffPeriod = slots * slotTime;
    ASSERT(backoffPeriod >= 0);
    EV << "backoff period set to " << backoffPeriod << endl;
}

void LoRaCSMA::decreaseBackoffPeriod()
{
    simtime_t elapsedBackoffTime = simTime() - endBackoff->getSendingTime();
    backoffPeriod -= ((int) (elapsedBackoffTime / slotTime)) * slotTime;
    ASSERT(backoffPeriod >= 0);
    EV << "backoff period decreased to " << backoffPeriod << endl;
}

void LoRaCSMA::scheduleBackoffTimer()
{
    EV << "scheduling backoff timer\n";
    if (isInvalidBackoffPeriod())
        generateBackoffPeriod();
    scheduleAfter(backoffPeriod, endBackoff);
}

void LoRaCSMA::cancelBackoffTimer()
{
    EV << "canceling backoff timer\n";
    cancelEvent(endBackoff);
}

/****************************************************************
 * Frame sender functions.
 */
void LoRaCSMA::sendDataFrame(Packet *frameToSend)
{
    EV << "sending Data frame " << frameToSend->getName() << endl;
    radio->setRadioMode(IRadio::RADIO_MODE_TRANSMITTER);
    sendDown(frameToSend->dup());
}

void LoRaCSMA::sendAckFrame()
{
    EV << "sending Ack frame\n";
    auto frameToAck = static_cast<Packet*>(endSifs->getContextPointer());
    endSifs->setContextPointer(nullptr);
    auto macHeader = makeShared<CsmaCaMacAckHeader>();
    macHeader->setChunkLength(ackLength);
    macHeader->setHeaderLengthField(B(ackLength).get());
    macHeader->setReceiverAddress(MacAddress::BROADCAST_ADDRESS);
    auto frame = new Packet("CsmaAck");
    frame->insertAtFront(macHeader);
    auto macTrailer = makeShared<CsmaCaMacTrailer>();
    macTrailer->setFcsMode(fcsMode);
    if (fcsMode == FCS_COMPUTED)
        macTrailer->setFcs(computeFcs(frame->peekAllAsBytes()));
    frame->insertAtBack(macTrailer);
    auto macAddressInd = frame->addTag<MacAddressInd>();
    macAddressInd->setSrcAddress(macHeader->getTransmitterAddress());
    macAddressInd->setDestAddress(macHeader->getReceiverAddress());
    frame->addTag<PacketProtocolTag>()->setProtocol(&Protocol::csmaCaMac);
    radio->setRadioMode(IRadio::RADIO_MODE_TRANSMITTER);
    sendDown(frame);
    delete frameToAck;
}

/****************************************************************
 * Helper functions.
 */
void LoRaCSMA::finishCurrentTransmission()
{
    deleteCurrentTxFrame();
    resetTransmissionVariables();
}

void LoRaCSMA::giveUpCurrentTransmission()
{
    auto packet = getCurrentTransmission();
    emitPacketDropSignal(packet, RETRY_LIMIT_REACHED, retryLimit);
    emit(linkBrokenSignal, packet);
    deleteCurrentTxFrame();
    resetTransmissionVariables();
    numGivenUp++;
}

void LoRaCSMA::retryCurrentTransmission()
{
    ASSERT(retryCounter < retryLimit);
    retryCounter++;
    numRetry++;
    generateBackoffPeriod();
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

bool LoRaCSMA::isAck(Packet *frame)
{
    return false;
    const auto &macHeader = frame->peekAtFront<CsmaCaMacHeader>();
    return macHeader->getType() == CSMA_ACK;
}

bool LoRaCSMA::isBroadcast(Packet *frame)
{
    return true;
    const auto &macHeader = frame->peekAtFront<CsmaCaMacHeader>();
    return macHeader->getReceiverAddress().isBroadcast();
}

bool LoRaCSMA::isForUs(Packet *frame)
{
    return false;
    const auto &macHeader = frame->peekAtFront<CsmaCaMacHeader>();
    return macHeader->getReceiverAddress() == networkInterface->getMacAddress();
}

bool LoRaCSMA::isFcsOk(Packet *frame)
{
    if (frame->hasBitError() || !frame->peekData()->isCorrect())
        return false;
    else {
        const auto &trailer = frame->peekAtBack<CsmaCaMacTrailer>(B(4));
        switch (trailer->getFcsMode()) {
            case FCS_DECLARED_INCORRECT:
                return false;
            case FCS_DECLARED_CORRECT:
                return true;
            case FCS_COMPUTED: {
                const auto &fcsBytes = frame->peekDataAt<BytesChunk>(B(0), frame->getDataLength() - trailer->getChunkLength());
                auto bufferLength = B(fcsBytes->getChunkLength()).get();
                auto buffer = new uint8_t[bufferLength];
                fcsBytes->copyToBuffer(buffer, bufferLength);
                auto computedFcs = ethernetCRC(buffer, bufferLength);
                delete[] buffer;
                return computedFcs == trailer->getFcs();
            }
            default:
                throw cRuntimeError("Unknown FCS mode");
        }
    }
}

uint32_t LoRaCSMA::computeFcs(const Ptr<const BytesChunk> &bytes)
{
    auto bufferLength = B(bytes->getChunkLength()).get();
    auto buffer = new uint8_t[bufferLength];
    bytes->copyToBuffer(buffer, bufferLength);
    auto computedFcs = ethernetCRC(buffer, bufferLength);
    delete[] buffer;
    return computedFcs;
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
//    Enter_Method("handleCanPullPacketChanged");
//    if (fsm.getState() == IDLE && !txQueue->isEmpty()) {
//        processUpperPacket();
//    }
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

// todo: adjust for csma
void LoRaCSMA::createBroadcastPacket(int packetSize, int messageId, int hopId, int source, bool retransmit, bool isNeighbourMsg)
{
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
    auto fragmentEncap = encapsulate(nodeAnnouncePacket);
    // todo: packet in front
    packetQueue.enqueuePacket(fragmentEncap);
}

void LoRaCSMA::handlePacket(Packet *packet)
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

} /* namespace rlora */
