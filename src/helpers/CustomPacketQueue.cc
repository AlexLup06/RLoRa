/*
 * CustomPacketQueue.cpp
 *
 *  Created on: 5 Mar 2025
 *      Author: alexanderlupatsiy
 */

#include "CustomPacketQueue.h"
#include "../messages/MessageInfoTag_m.h"

namespace rlora {

CustomPacketQueue::CustomPacketQueue()
{
}

CustomPacketQueue::~CustomPacketQueue()
{
    while (!packetQueue.empty()) {
        delete packetQueue.front();

        packetQueue.pop_front();
    }
}

string CustomPacketQueue::toString() const
{
    ostringstream oss;
    EV << "CustomPacketQueue [size=" << packetQueue.size() << "]" << endl;

    int index = 0;
    for (auto pkt : packetQueue) {
        EV << "  [" << index++ << "] " << pkt << endl;
    }

    return oss.str();
}

bool CustomPacketQueue::enqueuePacket(Packet *pkt)
{
    auto typeTag = pkt->getTag<MessageInfoTag>();
    EV << "CustomPacketQueue::enqueuePacket" << endl;

    if (!typeTag->isNeighbourMsg()) {
        EV << "This is Mission - Just adding to back" << endl;
        packetQueue.push_back(pkt);
        return true;
    }

    if (typeTag->isHeader()) {
        // Remove all NeighbourMsg packets and find the position of the first one
        int firstNeighbourPos = -1;
        int index = 0;

        for (auto it = packetQueue.begin(); it != packetQueue.end();) {
            auto tag = (*it)->getTag<MessageInfoTag>();

            // if the header was already sent we dont want to override the rest of the packet
            if (tag->isNeighbourMsg() && firstNeighbourPos == -1 && !tag->isHeader()) {
                EV << "Header already sent, we dont want to override rest - just need to append to back" << endl;
                ++it;
                ++index;
                continue;
            }

            if (tag->isNeighbourMsg()) {
                if (firstNeighbourPos == -1) {
                    firstNeighbourPos = index;
                }
                delete *it;
                it = packetQueue.erase(it); // erase returns the next valid iterator
                EV << "Erase neighbour packet" << endl;
            }
            else {
                EV << "Just go to Next" << endl;
                ++it;
                ++index;
            }
        }

        // If no NeighbourMsgs were found, just push_back
        if (firstNeighbourPos == -1) {
            EV << "No neighbour MSG - adding to back" << endl;
            packetQueue.push_back(pkt);
            return true;
        }
        else {
            EV << "removed all unwanted and now enqueuing at postion" << endl;
            enqueuePacketAtPosition(pkt, firstNeighbourPos);
        }
    }
    else {
        // Insert pkt after the last NeighbourMsg
        auto it = packetQueue.begin();
        auto lastNeighbourIt = packetQueue.end();

        bool allowedToinsert = false;

        for (; it != packetQueue.end(); ++it) {
            auto tag = (*it)->getTag<MessageInfoTag>();

            if (tag->isNeighbourMsg() && tag->isHeader()) {
                allowedToinsert = true;
            }

            if (tag->isNeighbourMsg() && allowedToinsert) {
                lastNeighbourIt = it;
            }
        }

        if (lastNeighbourIt != packetQueue.end()) {
            ++lastNeighbourIt; // move after the last NeighbourMsg
            packetQueue.insert(lastNeighbourIt, pkt);
        }
        else {
            packetQueue.push_front(pkt); // no NeighbourMsgs, put at the beginning
        }
    }
    return false;
}

void CustomPacketQueue::enqueuePacketAtPosition(Packet *pkt, int pos)
{
    auto it = packetQueue.begin();
    advance(it, pos);
    packetQueue.insert(it, pkt);
}

// enqueue NodeAnnounce Packet always in the front. If there is already a NodeAnnounce Packet there, then do nothing. If the first
// packet is not a header, then enqueue in front of the next header. Otherwise we will disrupt package transmission
void CustomPacketQueue::enqueueNodeAnnounce(Packet *pkt)
{
    // 1. Check if a NodeAnnounce already exists
    for (auto existingPkt : packetQueue) {
        auto existingTag = existingPkt->getTag<MessageInfoTag>();
        if (existingTag->isNodeAnnounce()) {
            EV << "NodeAnnounce already in queue -> dropping new one" << endl;
            delete pkt; // or handle accordingly
            return;
        }
    }

    // 2. If queue is empty -> just push to front
    if (packetQueue.empty()) {
        EV << "Queue empty -> pushing NodeAnnounce to front" << endl;
        packetQueue.push_front(pkt);
        return;
    }

    auto it = packetQueue.begin();
    auto firstTag = (*it)->getTag<MessageInfoTag>();

    // 3. If first packet is header -> insert at front
    if (firstTag->isHeader()) {
        EV << "First packet is header -> inserting NodeAnnounce at front" << endl;
        packetQueue.push_front(pkt);
        return;
    }

    // 4. First packet is not header -> find the next header
    auto insertPos = packetQueue.end();
    for (auto it2 = packetQueue.begin(); it2 != packetQueue.end(); ++it2) {
        auto tag2 = (*it2)->getTag<MessageInfoTag>();
        if (tag2->isHeader()) {
            insertPos = it2;
            break;
        }
    }

    if (insertPos != packetQueue.end()) {
        EV << "Inserting NodeAnnounce before next header" << endl;
        packetQueue.insert(insertPos, pkt);
    } else {
        EV << "No header found -> pushing NodeAnnounce to back" << endl;
        packetQueue.push_back(pkt);
    }
}

Packet* CustomPacketQueue::dequeuePacket()
{
    if (!packetQueue.empty()) {
        auto pkt = packetQueue.front();
        packetQueue.pop_front();
        return pkt;
    }
    return nullptr;
}

void CustomPacketQueue::removePacketAtPosition(int pos)
{
    if (pos < 0 || pos >= packetQueue.size()) {
        return;
    }

    auto it = packetQueue.begin();
    advance(it, pos);

    delete *it;
    packetQueue.erase(it);
}

void CustomPacketQueue::removePacket(Packet *pkt)
{
    packetQueue.remove(pkt);
    delete pkt;
}

bool CustomPacketQueue::isEmpty() const
{
    return packetQueue.empty();
}

int CustomPacketQueue::size() const
{
    return packetQueue.size();
}

} /* namespace rlora */
