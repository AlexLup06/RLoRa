/*
 * CustomPacketQueue.cpp
 *
 *  Created on: 5 Mar 2025
 *      Author: alexanderlupatsiy
 */

#include "CustomPacketQueue.h"
#include "MessageTypeTag_m.h"

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

void CustomPacketQueue::enqueuePacket(Packet *pkt)
{
    auto typeTag = pkt->getTag<MessageTypeTag>();
    EV << "CustomPacketQueue::enqueuePacket" << endl;

    if (!typeTag->isNeighbourMsg()) {
        EV << "This is Mission - Just adding to back" << endl;
        packetQueue.push_back(pkt);
        return;
    }

    if (typeTag->isHeader()) {
        // Remove all NeighbourMsg packets and find the position of the first one
        int firstNeighbourPos = -1;
        int index = 0;

        for (auto it = packetQueue.begin(); it != packetQueue.end();) {
            auto tag = (*it)->getTag<MessageTypeTag>();

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
            auto tag = (*it)->getTag<MessageTypeTag>();

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
}

void CustomPacketQueue::enqueuePacketAtPosition(Packet *pkt, int pos)
{
    auto it = packetQueue.begin();
    advance(it, pos);
    packetQueue.insert(it, pkt);
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
