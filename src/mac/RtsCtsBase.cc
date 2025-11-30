#include "RtsCtsBase.h"

namespace rlora
{
    void RtsCtsBase::initializeProtocol()
    {
        CTSWaitTimeout = new cMessage("CTSWaitTimeout");
        receivedCTS = new cMessage("receivedCTS");
        endOngoingMsg = new cMessage("endOngoingMsg");
        initiateCTS = new cMessage("initiateCTS");
        transmissionStartTimeout = new cMessage("transmissionStartTimeout");
        transmissionEndTimeout = new cMessage("transmissionEndTimeout");
        shortWait = new cMessage("shortWait");

        ctsCWTimeout = new cMessage("ctsCWTimeout");
        endBackoff = new cMessage("endBackoff");
        ctsBackoff = new BackoffHandler(this, ctsCWTimeout, ctsFS, cwCTS);
        regularBackoff = new BackoffHandler(this, endBackoff, backoffFS, cwBackoff);

        initializeRtsCtsProtocol();
    }

    void RtsCtsBase::finishProtocol()
    {
        cancelAndDelete(CTSWaitTimeout);
        cancelAndDelete(receivedCTS);
        cancelAndDelete(endOngoingMsg);
        cancelAndDelete(initiateCTS);
        cancelAndDelete(endBackoff);
        cancelAndDelete(transmissionStartTimeout);
        cancelAndDelete(ctsCWTimeout);
        cancelAndDelete(transmissionEndTimeout);
        cancelAndDelete(shortWait);

        CTSWaitTimeout = nullptr;
        receivedCTS = nullptr;
        endOngoingMsg = nullptr;
        initiateCTS = nullptr;
        endBackoff = nullptr;
        transmissionStartTimeout = nullptr;
        ctsCWTimeout = nullptr;
        transmissionEndTimeout = nullptr;
        shortWait = nullptr;
    }

    bool RtsCtsBase::isFreeToSend()
    {
        return !endOngoingMsg->isScheduled() && !transmissionStartTimeout->isScheduled();
    }

    void RtsCtsBase::handleCTS(Packet *pkt)
    {
        cancelEvent(CTSWaitTimeout);
        delete pkt;
    }

    void RtsCtsBase::handleCTSTimeout(bool withRetry)
    {
        if (!withRetry)
        {
            if (!packetQueue.isEmpty())
            {
                delete currentTxFrame;
                currentTxFrame = packetQueue.dequeuePacket();
            }
            return;
        }

        auto frameToSend = getCurrentTransmission();
        auto infoTag = frameToSend->getTagForUpdate<MessageInfoTag>();
        auto frag = frameToSend->peekAtFront<BroadcastFragment>();

        int newTries = infoTag->getTries() + 1;
        infoTag->setTries(newTries);
        if (newTries >= 3)
        {
            deleteCurrentTxFrame();
            if (!packetQueue.isEmpty())
            {
                currentTxFrame = packetQueue.dequeuePacket();
            }
            cwBackoff = 8; // TODO need to change inside backoffhandler
            return;
        }
        packetQueue.enqueuePacketAtPosition(frameToSend, 0);

        cwBackoff = cwBackoff * std::pow(2, newTries); // TODO just times 2 and also need to change inside backoffhandler

        ASSERT(frag != nullptr);
        ASSERT(infoTag->getPayloadSize() != -1);
        if (infoTag->getHasRegularHeader())
        {
            currentTxFrame = createHeader(frag->getMissionId(), frag->getSource(), infoTag->getPayloadSize(), !infoTag->isNeighbourMsg());
            encapsulate(currentTxFrame);
        }
        else
        {
            currentTxFrame = createContinuousHeader(frag->getMissionId(), frag->getSource(), infoTag->getPayloadSize(), !infoTag->isNeighbourMsg());
            encapsulate(currentTxFrame);
        }
    }

    bool RtsCtsBase::withRTS()
    {
        auto frameToSend = getCurrentTransmission();
        auto infoTag = frameToSend->getTag<MessageInfoTag>();
        return infoTag->getWithRTS();
    }

    bool RtsCtsBase::isOurCTS(cMessage *msg)
    {
        auto pkt = dynamic_cast<Packet *>(msg);
        if (pkt != nullptr)
        {
            decapsulate(pkt);
            auto chunk = pkt->peekAtFront<inet::Chunk>();
            if (auto msg = dynamic_cast<const BroadcastCTS *>(chunk.get()))
            {
                if (msg->getHopId() == nodeId)
                {
                    return true;
                }
            }
        }
        if (!msg->isSelfMessage())
        {
            delete msg;
        }
        return false;
    }

    bool RtsCtsBase::isCTSForSameRTSSource(cMessage *msg)
    {
        auto pkt = dynamic_cast<Packet *>(msg);
        if (pkt != nullptr)
        {
            decapsulate(pkt);
            auto chunk = pkt->peekAtFront<inet::Chunk>();
            if (auto msg = dynamic_cast<const BroadcastCTS *>(chunk.get()))
            {
                if (msg->getHopId() == rtsSource)
                {
                    return true;
                }
            }
        }
        return false;
    }

    void RtsCtsBase::sendRTS()
    {
        auto header = getCurrentTransmission();
        auto typeTag = header->getTag<MessageInfoTag>();
        ASSERT(typeTag->isHeader());

        scheduleAfter(ctsFS * cwCTS + sifs + predictOngoingMsgTime(header->getByteLength()), CTSWaitTimeout);
        sendDown(header->dup());
        DataLogger::getInstance()->logTransmission();
        DataLogger::getInstance()->logBytesSent(header->getByteLength());
        delete header;

        ASSERT(!packetQueue.isEmpty());
        currentTxFrame = packetQueue.dequeuePacket();
    }

    void RtsCtsBase::sendCTS(bool withRemainder)
    {
        auto ctsPacket = new Packet("BroadcastCTS");
        auto ctsPayload = makeShared<BroadcastCTS>();

        ASSERT(sourceOfRTS_CTSData > 0 && sizeOfFragment_CTSData > 0);
        ctsPayload->setChunkLength(B(BROADCAST_CTS));
        ctsPayload->setSizeOfFragment(sizeOfFragment_CTSData);
        ctsPayload->setHopId(sourceOfRTS_CTSData);

        ctsPacket->insertAtBack(ctsPayload);
        ctsPacket->addTagIfAbsent<PacketProtocolTag>()->setProtocol(&Protocol::apskPhy);
        ctsPacket->addTagIfAbsent<MessageInfoTag>()->setIsNeighbourMsg(false);
        encapsulate(ctsPacket);
        sendDown(ctsPacket->dup());

        DataLogger::getInstance()->logTransmission();
        DataLogger::getInstance()->logBytesSent(ctsPacket->getByteLength());
        delete ctsPacket;

        // After we send CTS, we need to receive message within ctsFS + sifs otherwise we assume there will be no message
        if (withRemainder)
        {
            scheduleAfter(ctsFS + sifs + ctsBackoff->remainder * ctsFS, transmissionStartTimeout);
            scheduleAfter(ctsFS + sifs + ctsBackoff->remainder * ctsFS + predictOngoingMsgTime(sizeOfFragment_CTSData), transmissionEndTimeout);
        }
        else
        {
            scheduleAfter(ctsFS + sifs, transmissionStartTimeout);
            scheduleAfter(ctsFS + sifs + predictOngoingMsgTime(sizeOfFragment_CTSData), transmissionEndTimeout);
        }

        sizeOfFragment_CTSData = -1;
        sourceOfRTS_CTSData = -1;
    }

    void RtsCtsBase::clearRTSsource()
    {
        rtsSource = -1;
    }

    void RtsCtsBase::setRTSsource(int rtsSourceId)
    {
        rtsSource = rtsSourceId;
    }

    bool RtsCtsBase::isPacketFromRTSSource(cMessage *msg)
    {
        if (!isLowerMessage(msg))
        {
            return false;
        }

        auto packet = dynamic_cast<Packet *>(msg->dup());
        decapsulate(packet);
        auto chunk = packet->peekAtFront<inet::Chunk>();

        if (auto fragment = dynamic_cast<const BroadcastFragment *>(chunk.get()))
        {
            auto infoTag = packet->getTag<MessageInfoTag>();
            if (infoTag->getHopId() == rtsSource)
            {
                clearRTSsource();
                delete packet;
                return true;
            }
        }
        delete packet;
        delete msg;
        return false;
    }
}