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

#include "IncompletePacketList.h"
#include <algorithm>
#include <cstring>
#include "generalHelpers.h"

namespace rlora {

IncompletePacketList::IncompletePacketList()
{
}

IncompletePacketList::~IncompletePacketList()
{
}

void IncompletePacketList::add(const FragmentedPacket &packet)
{
    // falls wir eine neue Nachricht von einem Knoten bekommen und die alte
    // Nachricht nicht vollständig empfangen wurde, dann wurde das letzte Fragment verloren
    removeBySource(packet.sourceNode);
    packets_.push_back(packet);
}

FragmentedPacket* IncompletePacketList::getById(int messageId)
{
    for (FragmentedPacket &packet : packets_) {
        if (packet.messageId == messageId) {
            return &packet;
        }
    }
    return nullptr;
}

void IncompletePacketList::removeById(int messageId)
{
    packets_.erase(std::remove_if(packets_.begin(), packets_.end(), [messageId](const FragmentedPacket &packet) {
        return packet.messageId == messageId;
    }), packets_.end());
}

void IncompletePacketList::removeBySource(int source)
{
    auto it = std::remove_if(packets_.begin(), packets_.end(), [source](const FragmentedPacket &packet) {
        return packet.sourceNode == source;
    });
    if (it != packets_.end()) {
        packets_.erase(it, packets_.end());
    }
}

// The messageIds are only the same from the same sender
bool IncompletePacketList::isFromSameHop(int messageId)
{
    FragmentedPacket *incompletePacket = getById(messageId);
    if (incompletePacket == nullptr) {
        return false;
    }
    return true;
}

Result IncompletePacketList::addToIncompletePacket(const BroadcastFragment *packet)
{
    FragmentedPacket *incompletePacket = getById(packet->getMessageId());

    Result result;
    if (incompletePacket == nullptr) {
        result.isComplete = false;
        result.sendUp = false;
        result.isRelevant = false;
        result.waitTime = 40 + predictSendTime(255);
        return result;
    }

    if (packet->getFragment() <= incompletePacket->lastFragment && packet->getFragment() > 0) {
        result.isComplete = false;
        result.sendUp = false;
        result.waitTime = -1;
        return result;
    }

    if (packet->getFragment() - incompletePacket->lastFragment > 1) {
        // We lost a Packet! Doesn't matter, fill out bytes and continue

        int lostFragments = packet->getFragment() - incompletePacket->lastFragment - 1;

        incompletePacket->received += (255 - BROADCAST_FRAGMENT_META_SIZE) * lostFragments;
        incompletePacket->lastFragment += lostFragments;
        incompletePacket->corrupted = true;
    }

    int totalBytesReceived = incompletePacket->received + packet->getPayloadSize();
    int bytesLeft = incompletePacket->size - totalBytesReceived;
    int waitTime = 20 + predictSendTime(bytesLeft > 255 ? 255 : bytesLeft);

    incompletePacket->received = totalBytesReceived;
    incompletePacket->lastFragment = packet->getFragment();

    if (incompletePacket->received == incompletePacket->size) {

        if (!incompletePacket->corrupted) {
            result.isComplete = true;
            result.sendUp = true;
            result.waitTime = waitTime;
            result.isMission = incompletePacket->retransmit;
            result.completePacket = *incompletePacket;
            return result;
        }
        else {
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

void IncompletePacketList::updateMessageId(int sourceId, int newMessageId)
{
    auto it = latestMessageIds_.find(sourceId);
    if (it == latestMessageIds_.end() || newMessageId > it->second) {
        latestMessageIds_[sourceId] = newMessageId;
    }
}

void IncompletePacketList::updateMissionId(int sourceId, int newMissionId)
{
    auto it = latestMissionIds_.find(sourceId);
    if (it == latestMissionIds_.end() || newMissionId > it->second) {
        latestMissionIds_[sourceId] = newMissionId;
    }
}

bool IncompletePacketList::isNewMissionIdLower(int sourceId, int newMissionId) const
{
    auto it = latestMissionIds_.find(nodeId);
    if (it == latestMissionIds_.end()) {
        return false; // No messageId yet, so it's considered "larger"
    }
    return newMissionId < it->second;

}
bool IncompletePacketList::isNewMissionIdSame(int sourceId, int newMissionId) const
{
    auto it = latestMissionIds_.find(nodeId);
    if (it == latestMissionIds_.end()) {
        return false; // No messageId yet, so it's considered "larger"
    }
    return newMissionId == it->second;
}
bool IncompletePacketList::isNewMissionIdHigher(int sourceId, int newMissionId) const
{
    auto it = latestMissionIds_.find(nodeId);
    if (it == latestMissionIds_.end()) {
        return true; // No messageId yet, so it's considered "larger"
    }
    return newMissionId > it->second;
}

bool IncompletePacketList::isNewMessageIdLower(int sourceId, int newMessageId) const
{
    auto it = latestMessageIds_.find(nodeId);
    if (it == latestMessageIds_.end()) {
        return false; // No messageId yet, so it's considered "larger"
    }
    return newMessageId < it->second;
}
bool IncompletePacketList::isNewMessageIdSame(int sourceId, int newMessageId) const
{
    auto it = latestMessageIds_.find(nodeId);
    if (it == latestMessageIds_.end()) {
        return false; // No messageId yet, so it's considered "larger"
    }
    return newMessageId == it->second;
}
bool IncompletePacketList::isNewMessageIdHigher(int sourceId, int newMessageId) const
{
    auto it = latestMessageIds_.find(nodeId);
    if (it == latestMessageIds_.end()) {
        return true; // No messageId yet, so it's considered "larger"
    }
    return newMessageId > it->second;
}

} // namespace rlora
