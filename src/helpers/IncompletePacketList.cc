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

Result IncompletePacketList::addToIncompletePacket(const BroadcastFragment *packet)
{
    FragmentedPacket *incompletePacket = getById(packet->getMessageId());

    Result result;
    if (incompletePacket == nullptr) {
        result.isComplete = false;
        result.sendUp = false;
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

} // namespace rlora
