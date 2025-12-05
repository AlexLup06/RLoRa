#ifndef PACKET_BASE_H_
#define PACKET_BASE_H_

#include "../common/common.h"
#include "RadioBase.h"

using namespace inet;
using namespace physicallayer;

namespace rlora
{
    class PacketBase : public RadioBase
    {
    public:
        PacketBase();

    protected:
        void finishPacketBase();

        Result addToIncompletePacket(const BroadcastFragment *pkt, bool isMission);
        void removePacketById(int missionId, int messageId, bool isMission);
        void addPacketToList(FragmentedPacket incompletePacket, bool isMissionMsg);

        void encapsulate(Packet *msg);
        void decapsulate(Packet *frame);

        void createBroadcastPacket(int payloadSize, int missionId, int source, bool isMission);
        void createBroadcastPacketWithRTS(int payloadSize, int missionId, int source, bool isMission);
        void createBroadcastPacketWithContinuousRTS(int payloadSize, int missionId, int source, bool isMission);
        Packet *createHeader(int missionId, int source, int payloadSize, bool isMission);
        Packet *createContinuousHeader(int missionId, int source, int payloadSize, bool isMission);
        void createNeighbourPacket(int payloadSize, int source, bool isMission);
        Packet *dequeueCustomPacket();

        void logReceivedFragmentId(int id);

        IncompletePacketList incompleteMissionPktList;
        IncompletePacketList incompleteNeighbourPktList;
        CustomPacketQueue packetQueue;

    private:
    };
}

#endif