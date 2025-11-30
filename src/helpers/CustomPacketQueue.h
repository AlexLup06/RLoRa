/*
 * CustomPacketQueue.h
 *
 *  Created on: 5 Mar 2025
 *      Author: alexanderlupatsiy
 */

#ifndef HELPERS_CUSTOMPACKETQUEUE_H_
#define HELPERS_CUSTOMPACKETQUEUE_H_

#include <list>
#include "inet/common/packet/Packet.h"

namespace rlora {

using namespace std;
using namespace inet;

class CustomPacketQueue
{
private:
    list<Packet*> packetQueue;

public:
    // Constructor & Destructor
    CustomPacketQueue();
    virtual ~CustomPacketQueue();

    // Queue operations
    void enqueuePacket(Packet *pkt);
    void enqueueNodeAnnounce(Packet *pkt);
    void enqueuePacketAtPosition(Packet *pkt, int pos);
    Packet* dequeuePacket();
    void removePacketAtPosition(int pos);
    void removePacket(Packet *entry);
    bool isEmpty() const;
    int size() const;
    string toString() const;
};

} /* namespace rlora */

#endif /* HELPERS_CUSTOMPACKETQUEUE_H_ */
