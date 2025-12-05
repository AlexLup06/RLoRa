#include "PacketBase.h"

namespace rlora
{
    PacketBase::PacketBase() : incompleteMissionPktList(true), incompleteNeighbourPktList(false)
    {
        incompleteMissionPktList.setLogFragmentCallback(
            [this](int id)
            {
                this->logReceivedFragmentId(id);
            });

        incompleteNeighbourPktList.setLogFragmentCallback(
            [this](int id)
            {
                this->logReceivedFragmentId(id);
            });
    }

    void PacketBase::logReceivedFragmentId(int id)
    {
        emit(receivedFragmentId, id);
    }

    void PacketBase::finishPacketBase()
    {
        while (!packetQueue.isEmpty())
        {
            auto *pkt = packetQueue.dequeuePacket();
            delete pkt;
        }
    }

    Result PacketBase::addToIncompletePacket(const BroadcastFragment *pkt, bool isMission)
    {
        if (isMission)
            return incompleteMissionPktList.addToIncompletePacket(pkt);
        else
            return incompleteNeighbourPktList.addToIncompletePacket(pkt);
    }

    void PacketBase::addPacketToList(FragmentedPacket incompletePacket, bool isMissionMsg)
    {
        if (isMissionMsg)
        {
            incompleteMissionPktList.addPacket(incompletePacket);
            incompleteMissionPktList.updatePacketId(incompletePacket.sourceNode, incompletePacket.missionId);
        }
        else
        {
            incompleteNeighbourPktList.addPacket(incompletePacket);
            incompleteNeighbourPktList.updatePacketId(incompletePacket.sourceNode, incompletePacket.messageId);
        }
    }

    void PacketBase::removePacketById(int missionId, int messageId, bool isMission)
    {
        if (isMission)
            return incompleteMissionPktList.removePacketById(missionId);
        else
            return incompleteNeighbourPktList.removePacketById(messageId);
    }

    Packet *PacketBase::dequeueCustomPacket()
    {
        return packetQueue.dequeuePacket();
    }

    void PacketBase::encapsulate(Packet *msg)
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
    }

    void PacketBase::decapsulate(Packet *frame)
    {
        auto loraHeader = frame->popAtFront<LoRaMacFrame>();
        frame->addTagIfAbsent<MacAddressInd>()->setSrcAddress(loraHeader->getTransmitterAddress());
        frame->addTagIfAbsent<MacAddressInd>()->setDestAddress(loraHeader->getReceiverAddress());
        frame->addTagIfAbsent<InterfaceInd>()->setInterfaceId(networkInterface->getInterfaceId());
    }

    void PacketBase::createBroadcastPacket(int payloadSize, int missionId, int source, bool isMission)
    {
        auto headerPaket = new Packet("BroadcastLeaderFragment");
        auto headerPayload = makeShared<BroadcastLeaderFragment>();
        int messageId = headerPaket->getId();
        int fullPayloadSize = payloadSize;

        if (missionId == -1 && isMission)
        {
            missionId = headerPaket->getId();
        }

        if (source == -1)
        {
            source = nodeId;
        }

        int currentPayloadSize = 0;
        if (payloadSize + BROADCAST_LEADER_FRAGMENT_META_SIZE > MAXIMUM_PACKET_SIZE)
        {
            currentPayloadSize = MAXIMUM_PACKET_SIZE - BROADCAST_LEADER_FRAGMENT_META_SIZE;
            headerPayload->setChunkLength(B(MAXIMUM_PACKET_SIZE));
            headerPayload->setPayloadSize(currentPayloadSize);
            payloadSize = payloadSize - currentPayloadSize;
        }
        else
        {
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
        headerPayload->setIsMission(isMission);
        headerPaket->insertAtBack(headerPayload);
        headerPaket->addTagIfAbsent<PacketProtocolTag>()->setProtocol(&Protocol::apskPhy);

        auto messageInfoTag = headerPaket->addTagIfAbsent<MessageInfoTag>();
        messageInfoTag->setIsNeighbourMsg(!isMission);
        messageInfoTag->setMissionId(missionId);
        messageInfoTag->setIsHeader(true);
        messageInfoTag->setHasUsefulData(true);
        messageInfoTag->setPayloadSize(currentPayloadSize);
        messageInfoTag->setMessageId(messageId);

        encapsulate(headerPaket);
        packetQueue.enqueuePacket(headerPaket);

        // INFO: das nullte packet ist das was im leader direkt mitgeschickt wird
        int i = 1;
        while (payloadSize > 0)
        {
            auto fragmentPacket = new Packet("BroadcastFragmentPkt");
            auto fragmentPayload = makeShared<BroadcastFragment>();

            if (payloadSize + BROADCAST_FRAGMENT_META_SIZE > MAXIMUM_PACKET_SIZE)
            {
                currentPayloadSize = MAXIMUM_PACKET_SIZE - BROADCAST_FRAGMENT_META_SIZE;
                fragmentPayload->setChunkLength(B(MAXIMUM_PACKET_SIZE));
                fragmentPayload->setPayloadSize(currentPayloadSize);
                payloadSize = payloadSize - currentPayloadSize;
            }
            else
            {
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
            messageInfoTag->setMessageId(messageId);

            encapsulate(fragmentPacket);
            packetQueue.enqueuePacket(fragmentPacket);
        }
    }

    void PacketBase::createBroadcastPacketWithRTS(int payloadSize, int missionId, int source, bool isMission)
    {
        if (source == -1)
        {
            source = nodeId;
        }

        Packet *headerPaket = createHeader(missionId, source, payloadSize, isMission);

        if (missionId == -1)
        {
            missionId = headerPaket->getId();
        }
        int messageId = headerPaket->getId();

        auto waitTimeTag = headerPaket->addTagIfAbsent<WaitTimeTag>();
        waitTimeTag->setWaitTime(150);

        encapsulate(headerPaket);
        packetQueue.enqueuePacket(headerPaket);

        int i = 0;
        while (payloadSize > 0)
        {
            auto fragmentPacket = new Packet("BroadcastFragmentPkt");
            auto fragmentPayload = makeShared<BroadcastFragment>();

            int currentPayloadSize = 0;
            if (payloadSize + BROADCAST_FRAGMENT_META_SIZE > MAXIMUM_PACKET_SIZE)
            {
                currentPayloadSize = MAXIMUM_PACKET_SIZE - BROADCAST_FRAGMENT_META_SIZE;
                fragmentPayload->setChunkLength(B(MAXIMUM_PACKET_SIZE));
                fragmentPayload->setPayloadSize(currentPayloadSize);
                payloadSize = payloadSize - currentPayloadSize;
            }
            else
            {
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
            messageInfoTag->setIsNeighbourMsg(!isMission);
            messageInfoTag->setMissionId(missionId);
            messageInfoTag->setIsHeader(false);
            messageInfoTag->setHasUsefulData(true);
            messageInfoTag->setPayloadSize(currentPayloadSize);
            messageInfoTag->setMessageId(messageId);

            if (payloadSize == 0)
            {
                auto waitTimeTag = fragmentPacket->addTagIfAbsent<WaitTimeTag>();
                waitTimeTag->setWaitTime(50 + 270 + intuniform(0, 50));
            }
            else
            {
                auto waitTimeTag = fragmentPacket->addTagIfAbsent<WaitTimeTag>();
                waitTimeTag->setWaitTime(0);
            }

            encapsulate(fragmentPacket);
            packetQueue.enqueuePacket(fragmentPacket);
        }
    }

    Packet *PacketBase::createHeader(int missionId, int source, int payloadSize, bool isMission)
    {
        EV << "createHeader" << endl;

        Packet *headerPaket = new Packet("BroadcastRtsPkt");

        if (missionId == -1)
        {
            missionId = headerPaket->getId();
        }

        auto headerPayload = makeShared<BroadcastRts>();
        headerPayload->setChunkLength(B(BROADCAST_RTS_SIZE));
        headerPayload->setSize(payloadSize);
        headerPayload->setMissionId(missionId);
        headerPayload->setMessageId(headerPaket->getId());
        headerPayload->setSource(source);
        headerPayload->setHop(nodeId);
        headerPayload->setIsMission(isMission);
        headerPaket->insertAtBack(headerPayload);
        headerPaket->addTagIfAbsent<PacketProtocolTag>()->setProtocol(&Protocol::apskPhy);

        auto messageInfoTag = headerPaket->addTagIfAbsent<MessageInfoTag>();
        messageInfoTag->setIsNeighbourMsg(!isMission);
        messageInfoTag->setMissionId(missionId);
        messageInfoTag->setIsHeader(true);
        messageInfoTag->setMessageId(headerPaket->getId());

        return headerPaket;
    }

    Packet *PacketBase::createContinuousHeader(int missionId, int source, int payloadSize, bool isMission)
    {
        EV << "createContinuousHeader" << endl;
        Packet *headerPaket = new Packet("BroadcastContinuousRts");

        if (missionId == -1)
        {
            missionId = headerPaket->getId();
        }

        auto headerPayload = makeShared<BroadcastContinuousRts>();
        headerPayload->setChunkLength(B(BROADCAST_CONTINIOUS_RTS_SIZE));
        headerPayload->setPayloadSizeOfNextFragment(payloadSize + BROADCAST_FRAGMENT_META_SIZE);
        headerPayload->setHopId(nodeId);
        headerPayload->setMessageId(headerPaket->getId());
        headerPaket->insertAtBack(headerPayload);
        headerPaket->addTagIfAbsent<PacketProtocolTag>()->setProtocol(&Protocol::apskPhy);

        auto messageInfoTag = headerPaket->addTagIfAbsent<MessageInfoTag>();
        messageInfoTag->setIsNeighbourMsg(!isMission);
        messageInfoTag->setMissionId(missionId);
        messageInfoTag->setIsHeader(true);
        messageInfoTag->setWithRTS(isMission);
        messageInfoTag->setMessageId(headerPaket->getId());

        return headerPaket;
    }

    void PacketBase::createBroadcastPacketWithContinuousRTS(int payloadSize, int missionId, int source, bool isMission)
    {
        if (source == -1)
        {
            source = nodeId;
        }

        Packet *headerPaket = createHeader(missionId, source, payloadSize, isMission);

        if (missionId == -1)
        {
            missionId = headerPaket->getId();
        }
        int messageId = headerPaket->getId();

        encapsulate(headerPaket);
        packetQueue.enqueuePacket(headerPaket);

        int i = 0;
        while (payloadSize > 0)
        {
            bool hasRegularHeader = true;
            if (i > 0)
            {
                Packet *continuousHeader = createContinuousHeader(missionId, source, payloadSize, isMission);
                encapsulate(continuousHeader);
                packetQueue.enqueuePacket(continuousHeader);
                hasRegularHeader = false;
            }

            auto fragmentPacket = new Packet("BroadcastFragmentPkt");
            auto fragmentPayload = makeShared<BroadcastFragment>();

            int currentPayloadSize = 0;
            if (payloadSize + BROADCAST_FRAGMENT_META_SIZE > MAXIMUM_PACKET_SIZE)
            {
                currentPayloadSize = MAXIMUM_PACKET_SIZE - BROADCAST_FRAGMENT_META_SIZE;
                fragmentPayload->setChunkLength(B(MAXIMUM_PACKET_SIZE));
                fragmentPayload->setPayloadSize(currentPayloadSize);
                payloadSize = payloadSize - currentPayloadSize;
            }
            else
            {
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
            messageInfoTag->setIsNeighbourMsg(!isMission);
            messageInfoTag->setMissionId(missionId);
            messageInfoTag->setIsHeader(false);
            messageInfoTag->setHopId(nodeId);
            messageInfoTag->setHasUsefulData(true);
            messageInfoTag->setPayloadSize(currentPayloadSize);
            messageInfoTag->setWithRTS(isMission);
            messageInfoTag->setHasRegularHeader(hasRegularHeader);
            messageInfoTag->setMessageId(messageId);

            encapsulate(fragmentPacket);
            packetQueue.enqueuePacket(fragmentPacket);
        }
    }

    void PacketBase::createNeighbourPacket(int payloadSize, int source, bool isMission)
    {
        auto leaderpacket = new Packet("BroadcastLeaderFragment");
        auto leaderPayload = makeShared<BroadcastLeaderFragment>();
        int messageId = leaderpacket->getId();
        int fullPayloadSize = payloadSize;

        if (source == -1)
        {
            source = nodeId;
        }

        leaderPayload->setChunkLength(B(payloadSize + BROADCAST_LEADER_FRAGMENT_META_SIZE));
        leaderPayload->setPayloadSize(payloadSize);
        leaderPayload->setSize(fullPayloadSize);
        leaderPayload->setMessageId(messageId);
        leaderPayload->setMissionId(-1);
        leaderPayload->setSource(source);
        leaderPayload->setHop(nodeId);
        leaderPayload->setIsMission(isMission);
        leaderpacket->insertAtBack(leaderPayload);
        leaderpacket->addTagIfAbsent<PacketProtocolTag>()->setProtocol(&Protocol::apskPhy);

        auto messageInfoTag = leaderpacket->addTagIfAbsent<MessageInfoTag>();
        messageInfoTag->setIsNeighbourMsg(true);
        messageInfoTag->setMissionId(-1);
        messageInfoTag->setIsHeader(true);
        messageInfoTag->setHasUsefulData(true);
        messageInfoTag->setPayloadSize(payloadSize);
        messageInfoTag->setWithRTS(false);
        messageInfoTag->setMessageId(messageId);

        encapsulate(leaderpacket);

        packetQueue.enqueuePacket(leaderpacket);
    }

}
