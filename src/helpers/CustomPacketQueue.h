#ifndef HELPERS_CUSTOMPACKETQUEUE_H_
#define HELPERS_CUSTOMPACKETQUEUE_H_

#include <list>
#include "inet/common/packet/Packet.h"

using namespace std;
using namespace inet;
namespace rlora
{

    class CustomPacketQueue
    {
    private:
        list<Packet *> packetQueue;

    public:
        virtual ~CustomPacketQueue();

        // Queue operations
        void enqueuePacket(Packet *pkt);
        void enqueueNodeAnnounce(Packet *pkt);
        void enqueuePacketAtPosition(Packet *pkt, int pos);
        Packet *dequeuePacket();
        void removePacketAtPosition(int pos);
        void removePacket(Packet *entry);
        bool isEmpty() const;
        int size() const;
        string toString() const;
    };

}

#endif
