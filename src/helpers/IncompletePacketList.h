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

#ifndef HELPERS_INCOMPLETEPACKETLIST_H_
#define HELPERS_INCOMPLETEPACKETLIST_H_

#include <vector>
#include <cstdint>
#include <sstream>

#include "inet/common/Units.h"

#include "../messages/BroadcastFragment_m.h"
#include "generalHelpers.h"
#include <unordered_map>

namespace rlora {

using namespace omnetpp;
using namespace std;

struct FragmentedPacket
{
    int messageId = -1;
    int missionId = -1;
    int size = -1;
    int received = 0;
    bool fragments[256] = { false };
    int sourceNode = -1;
    int lastHop = -1;
    bool corrupted = false;
    bool retransmit = false;

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

    FragmentedPacket* getPacketById(int id);
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

} // namespace rlora

#endif // HELPERS_INCOMPLETEPACKETLIST_H_
