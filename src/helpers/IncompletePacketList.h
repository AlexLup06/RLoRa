#ifndef HELPERS_INCOMPLETEPACKETLIST_H_
#define HELPERS_INCOMPLETEPACKETLIST_H_

#include <vector>
#include <cstdint>
#include <sstream>

#include "inet/common/Units.h"

#include "../common/messages/BroadcastFragment_m.h"
#include "generalHelpers.h"
#include <unordered_map>

namespace rlora
{
    using namespace omnetpp;
    using namespace std;

    struct FragmentedPacket
    {
        int messageId = -1;
        int missionId = -1;
        int size = -1;
        int received = 0;
        bool fragments[256] = {false};
        int sourceNode = -1;
        int lastHop = -1;
        bool corrupted = false;
        bool isMission = false;
    };

    struct Result
    {
        bool isComplete;
        bool sendUp;
        bool isMission = false;
        bool isRelevant = true;
        int waitTime;
        FragmentedPacket completePacket = FragmentedPacket();
    };

    class IncompletePacketList
    {
    public:
        IncompletePacketList(bool isMissionList = false);
        virtual ~IncompletePacketList();

        FragmentedPacket *getPacketById(int id);
        void removePacketById(int id);
        void addPacket(const FragmentedPacket &packet);
        void removePacketBySource(int source);
        Result addToIncompletePacket(const BroadcastFragment *fragment);

        void updatePacketId(int sourceId, int newId);
        bool isNewIdLower(int sourceId, int newId) const;
        bool isNewIdSame(int sourceId, int newId) const;
        bool isNewIdHigher(int sourceId, int newId) const;

    private:
        std::vector<FragmentedPacket> packets_;
        std::unordered_map<int, int> latestIds_;
        bool isMissionList_;
    };

}

#endif
