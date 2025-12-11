#include "IncompletePacketList.h"

namespace rlora
{

    IncompletePacketList::IncompletePacketList(bool isMissionList) : isMissionList_(isMissionList)
    {
    }

    IncompletePacketList::~IncompletePacketList()
    {
    }

    void IncompletePacketList::setLogFragmentCallback(LogFunc func)
    {
        logFragmentFunc_ = std::move(func);
    }

    FragmentedPacket *IncompletePacketList::getPacketById(int id)
    {
        for (FragmentedPacket &packet : packets_)
        {
            if (packet.messageId == id && !isMissionList_)
            {
                return &packet;
            }

            if (packet.missionId == id && isMissionList_)
            {
                return &packet;
            }
        }
        return nullptr;
    }

    void IncompletePacketList::removePacketById(int id)
    {
        packets_.erase(std::remove_if(packets_.begin(), packets_.end(), [id](const FragmentedPacket &packet)
                                      { return packet.messageId == id; }),
                       packets_.end());
    }

    void IncompletePacketList::addPacket(const FragmentedPacket &packet)
    {
        removePacketBySource(packet.sourceNode);
        packets_.push_back(packet);
    }

    void IncompletePacketList::removePacketBySource(int source)
    {
        auto it = std::remove_if(packets_.begin(), packets_.end(), [source](const FragmentedPacket &packet)
                                 { return packet.sourceNode == source; });
        if (it != packets_.end())
        {
            packets_.erase(it, packets_.end());
        }
    }

    Result IncompletePacketList::addToIncompletePacket(const BroadcastFragment *packet)
    {
        FragmentedPacket *incompletePacket;
        if (isMissionList_)
            incompletePacket = getPacketById(packet->getMissionId());
        else
            incompletePacket = getPacketById(packet->getMessageId());

        Result result;
        if (incompletePacket == nullptr)
        {
            EV << "Incomplete fragment does not exist" << endl;
            result.isComplete = false;
            result.sendUp = false;
            result.isRelevant = false;
            result.waitTime = 40 + predictSendTime(MAXIMUM_PACKET_SIZE);
            return result;
        }

        logFragmentFunc_(packet->getMessageId());

        int fragmentId = packet->getFragmentId();
        if (incompletePacket->fragments[fragmentId]) // we already got this fragment
        {
            result.isComplete = false;
            result.sendUp = false;
            result.waitTime = -1;
            return result;
        }

        int totalBytesReceived = incompletePacket->received + packet->getPayloadSize();
        int bytesLeft = incompletePacket->size - totalBytesReceived;
        int waitTime = 20 + predictSendTime(bytesLeft > MAXIMUM_PACKET_SIZE ? MAXIMUM_PACKET_SIZE : bytesLeft);

        incompletePacket->received = totalBytesReceived;
        incompletePacket->fragments[fragmentId] = true;

        if (incompletePacket->received == incompletePacket->size)
        {
            if (!incompletePacket->corrupted)
            {
                result.isComplete = true;
                result.sendUp = true;
                result.waitTime = waitTime;
                result.isMission = incompletePacket->isMission;
                result.completePacket = *incompletePacket;
                return result;
            }
            else
            {
                result.isComplete = true;
                result.sendUp = false;
                result.waitTime = waitTime;
                return result;
            }
        }
        result.isComplete = false;
        result.sendUp = false;
        result.waitTime = waitTime;
        return result;
    }

    void IncompletePacketList::updatePacketId(int sourceId, int newId)
    {
        if (newId < 0)
            return;
        auto it = latestIds_.find(sourceId);
        if (it == latestIds_.end() || newId > it->second)
        {
            latestIds_[sourceId] = newId;
        }
    }

    bool IncompletePacketList::isNewIdSame(int sourceId, int newId) const
    {
        auto it = latestIds_.find(sourceId);
        if (it == latestIds_.end())
        {
            return false; // No messageId yet, so it's considered "larger"
        }
        return newId == it->second;
    }

    bool IncompletePacketList::isNewIdHigher(int sourceId, int newId) const
    {
        auto it = latestIds_.find(sourceId);
        if (it == latestIds_.end())
        {
            return true; // No messageId yet, so it's considered "larger"
        }
        return newId > it->second;
    }

    bool IncompletePacketList::isNewIdLower(int sourceId, int newId) const
    {
        auto it = latestIds_.find(sourceId);
        if (it == latestIds_.end())
        {
            return false; // No messageId yet, so it's considered "larger"
        }
        return newId < it->second;
    }

}
