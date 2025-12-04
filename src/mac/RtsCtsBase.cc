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

    void RtsCtsBase::handleCTSTimeout(bool withRetry)
    {
        regularBackoff->increaseCw();
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
            return;
        }
        packetQueue.enqueuePacketAtPosition(frameToSend, 0);

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

    bool RtsCtsBase::isRTS(Packet *packet)
    {
        if (packet != nullptr)
        {
            auto chunk = packet->peekAtFront<inet::Chunk>();
            if (auto msg = dynamic_cast<const BroadcastRts *>(chunk.get()))
            {
                return true;
            }
        }
        return false;
    }

    void RtsCtsBase::handleUnhandeledRTS()
    {
        double maxTransmissionTime = predictOngoingMsgTime(MAXIMUM_PACKET_SIZE);
        double maxCtsCWTime = cwCTS * ctsFS.dbl();
        double scheduleTime = maxTransmissionTime + maxCtsCWTime + sifs.dbl();

        scheduleOrExtend(this, endOngoingMsg, scheduleTime);
    }

    bool RtsCtsBase::isOurCTS(Packet *packet)
    {
        if (packet != nullptr)
        {
            auto chunk = packet->peekAtFront<inet::Chunk>();
            if (auto msg = dynamic_cast<const BroadcastCTS *>(chunk.get()))
            {
                if (msg->getHopId() == nodeId)
                {
                    regularBackoff->resetCw();
                    return true;
                }
            }
        }
        return false;
    }

    bool RtsCtsBase::isCTSForSameRTSSource(Packet *packet)
    {
        if (packet != nullptr)
        {
            auto chunk = packet->peekAtFront<inet::Chunk>();
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
        auto infoTag = header->getTag<MessageInfoTag>();
        ASSERT(infoTag->isHeader());
        emit(missionIdRtsSent, infoTag->getMissionId());

        scheduleAfter(
            ctsFS * (cwCTS - 1) + sifs + 0.003 +
                predictOngoingMsgTime(header->getByteLength()) +
                predictOngoingMsgTime(BROADCAST_CTS_SIZE),
            CTSWaitTimeout);
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
        ctsPayload->setChunkLength(B(BROADCAST_CTS_SIZE));
        ctsPayload->setSizeOfFragment(sizeOfFragment_CTSData);
        ctsPayload->setHopId(sourceOfRTS_CTSData);
        ctsPayload->setSlot(ctsBackoff->chosenSlot);

        ctsPacket->insertAtBack(ctsPayload);
        ctsPacket->addTagIfAbsent<PacketProtocolTag>()->setProtocol(&Protocol::apskPhy);
        ctsPacket->addTagIfAbsent<MessageInfoTag>()->setIsNeighbourMsg(false);
        encapsulate(ctsPacket);
        sendDown(ctsPacket->dup());

        DataLogger::getInstance()->logTransmission();
        DataLogger::getInstance()->logBytesSent(ctsPacket->getByteLength());
        delete ctsPacket;

        if (withRemainder)
        {
            scheduleAfter(ctsFS + sifs + (ctsBackoff->remainder) * ctsFS, transmissionStartTimeout);
            scheduleAfter(ctsFS + sifs + (ctsBackoff->remainder) * ctsFS + predictOngoingMsgTime(sizeOfFragment_CTSData), transmissionEndTimeout);
        }
        else
        {
            scheduleAfter(ctsFS + sifs, transmissionStartTimeout);
            scheduleAfter(ctsFS + sifs + predictOngoingMsgTime(sizeOfFragment_CTSData), transmissionEndTimeout);
        }

        sizeOfFragment_CTSData = -1;
        sourceOfRTS_CTSData = -1;
    }

    bool RtsCtsBase::isStrayCTS(Packet *packet)
    {
        if (packet != nullptr)
        {
            auto chunk = packet->peekAtFront<inet::Chunk>();
            if (auto cts = dynamic_cast<const BroadcastCTS *>(chunk.get()))
            {
                if (cts->getHopId() != nodeId)
                {
                    return true;
                }
            }
        }
        return false;
    }

    void RtsCtsBase::handleStrayCTS(Packet *packet, bool withRemainder)
    {
        auto chunk = packet->peekAtFront<inet::Chunk>();
        auto cts = dynamic_cast<const BroadcastCTS *>(chunk.get());

        double scheduleTime = predictOngoingMsgTime(cts->getSizeOfFragment()) + sifs.dbl();
        if (withRemainder)
        {
            double remainingCtsCwDuration = (cwCTS - cts->getSlot() - 1) * ctsFS.dbl();
            scheduleTime += remainingCtsCwDuration;
        }
        scheduleOrExtend(this, endOngoingMsg, scheduleTime);
    }

    void RtsCtsBase::clearRTSsource()
    {
        rtsSource = -1;
    }

    void RtsCtsBase::setRTSsource(int rtsSourceId)
    {
        rtsSource = rtsSourceId;
    }

    bool RtsCtsBase::isPacketFromRTSSource(Packet *packet)
    {
        if (packet == nullptr)
        {
            return false;
        }

        auto chunk = packet->peekAtFront<inet::Chunk>();
        if (auto fragment = dynamic_cast<const BroadcastFragment *>(chunk.get()))
        {
            auto infoTag = packet->getTag<MessageInfoTag>();
            if (infoTag->getHopId() == rtsSource)
            {
                clearRTSsource();
                return true;
            }
        }
        return false;
    }

    bool RtsCtsBase::isNotOurDataPacket(Packet *packet)
    {
        if (packet == nullptr)
        {
            return false;
        }

        auto chunk = packet->peekAtFront<inet::Chunk>();
        if (auto fragment = dynamic_cast<const BroadcastFragment *>(chunk.get()))
        {
            auto infoTag = packet->getTag<MessageInfoTag>();
            if (infoTag->getHopId() != rtsSource)
            {
                clearRTSsource();
                return true;
            }
        }
        else if (auto fragment = dynamic_cast<const BroadcastLeaderFragment *>(chunk.get()))
        {
            return true;
        }
        return false;
    }

    bool RtsCtsBase::isDataPacket(Packet *packet)
    {
        if (packet == nullptr)
        {
            return false;
        }

        auto chunk = packet->peekAtFront<inet::Chunk>();
        if (auto fragment = dynamic_cast<const BroadcastFragment *>(chunk.get()))
        {
            return true;
        }
        else if (auto fragment = dynamic_cast<const BroadcastLeaderFragment *>(chunk.get()))
        {
            return true;
        }
        return false;
    }

    //============================================================
    // handle packets
    //============================================================

    void RtsCtsBase::handleCTS(const BroadcastCTS *cts)
    {
        ASSERT(cts->getHopId() != nodeId);
        int size = cts->getSizeOfFragment();
        ASSERT(size > 0);
        scheduleOrExtend(this, endOngoingMsg, predictOngoingMsgTime(size) + sifs.dbl());
    }

    void RtsCtsBase::handleFragment(const BroadcastFragment *fragment, Ptr<const MessageInfoTag> infoTag)
    {
        int missionId = fragment->getMissionId();
        bool isMissionMsg = !infoTag->isNeighbourMsg();

        Result result = addToIncompletePacket(fragment, isMissionMsg);
        retransmitPacket(result);
    }
}