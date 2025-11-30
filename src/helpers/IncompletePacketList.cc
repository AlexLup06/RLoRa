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

IncompletePacketList::IncompletePacketList(bool isMissionList) :
        isMissionList_(isMissionList)
{
}

IncompletePacketList::~IncompletePacketList()
{
}

FragmentedPacket* IncompletePacketList::getPacketById(int id)
{
    for (FragmentedPacket &packet : packets_) {
        if (packet.messageId == id && !isMissionList_) {
            return &packet;
        }

        if (packet.missionId == id && isMissionList_) {
            return &packet;
        }
    }
    return nullptr;
}

void IncompletePacketList::removePacketById(int id)
{
    packets_.erase(std::remove_if(packets_.begin(), packets_.end(), [id](const FragmentedPacket &packet) {
        return packet.messageId == id;
    }), packets_.end());
}

void IncompletePacketList::addPacket(const FragmentedPacket &packet)
{
    removePacketBySource(packet.sourceNode);
    if (packet.isMission)
        EV << "Creating packet with id: " << packet.missionId << endl;

    packets_.push_back(packet);
}

void IncompletePacketList::removePacketBySource(int source)
{
    auto it = std::remove_if(packets_.begin(), packets_.end(), [source](const FragmentedPacket &packet) {
        return packet.sourceNode == source;
    });
    if (it != packets_.end()) {
        packets_.erase(it, packets_.end());
    }
}

/*
 * We can get fragments in different sequence
 */
Result IncompletePacketList::addToIncompletePacket(const BroadcastFragment *packet)
{
    FragmentedPacket *incompletePacket;
    if (isMissionList_)
        incompletePacket = getPacketById(packet->getMissionId());
    else
        incompletePacket = getPacketById(packet->getMessageId());

    if (isMissionList_)
        EV << "Adding packet with id: " << packet->getMissionId() << endl;

    Result result;
    if (incompletePacket == nullptr) {
        EV << "Incomplete fragment does not exist" << endl;
        result.isComplete = false;
        result.sendUp = false;
        result.isRelevant = false;
        result.waitTime = 40 + predictSendTime(255);
        return result;
    }

    int fragmentId = packet->getFragmentId();
    if (incompletePacket->fragments[fragmentId]) { // we already got this fragment
        EV << "Already got this fragment" << endl;
        result.isComplete = false;
        result.sendUp = false;
        result.waitTime = -1;
        return result;
    }

    int totalBytesReceived = incompletePacket->received + packet->getPayloadSize();
    int bytesLeft = incompletePacket->size - totalBytesReceived;
    int waitTime = 20 + predictSendTime(bytesLeft > 255 ? 255 : bytesLeft);

    incompletePacket->received = totalBytesReceived;
    incompletePacket->fragments[fragmentId] = true;

    if (incompletePacket->received == incompletePacket->size) {
        if (!incompletePacket->corrupted) {
            result.isComplete = true;
            result.sendUp = true;
            result.waitTime = waitTime;
            result.isMission = incompletePacket->isMission;
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
    EV << "Wrong sizes" << endl;
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
    if (it == latestIds_.end() || newId > it->second) {
        latestIds_[sourceId] = newId;
    }
}

bool IncompletePacketList::isNewIdLower(int sourceId, int newId) const
{
    auto it = latestIds_.find(sourceId);
    if (it == latestIds_.end()) {
        return false; // No messageId yet, so it's considered "larger"
    }
    return newId < it->second;
}

bool IncompletePacketList::isNewIdSame(int sourceId, int newId) const
{
    auto it = latestIds_.find(sourceId);
    if (it == latestIds_.end()) {
        return false; // No messageId yet, so it's considered "larger"
    }
    return newId == it->second;
}

bool IncompletePacketList::isNewIdHigher(int sourceId, int newId) const
{
    auto it = latestIds_.find(sourceId);
    if (it == latestIds_.end()) {
        return true; // No messageId yet, so it's considered "larger"
    }
    return newId > it->second;
}

} // namespace rlora
