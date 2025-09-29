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

namespace rlora {

using namespace omnetpp;
using namespace std;

struct FragmentedPacket
{
    string id = generate_uuid();
    int messageId = -1;
    int missionId = -1;
    int size = -1;
    int received = 0;
    int lastFragment = 0;
    int sourceNode = -1;
    int lastHop = -1;
    bool corrupted = false;
    bool retransmit = false;

    std::string toString() const
    {
        std::ostringstream oss;
        oss << "FragmentedPacket { " << "id: " << id << ", messageId: " << messageId << ", size: " << size << ", received: " << received << ", lastFragment: " << lastFragment << ", sourceNode: " << sourceNode << ", lastHop: " << lastHop << ", corrupted: " << std::boolalpha << corrupted
                << ", retransmit: " << std::boolalpha << retransmit << " }";
        return oss.str();
    }
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
    IncompletePacketList();
    virtual ~IncompletePacketList();

    FragmentedPacket* getById(int messageId);

    void removeById(int messageId);
    void add(const FragmentedPacket &packet);
    void removeBySource(int source);
    bool isFromSameHop(int messageId);
    Result addToIncompletePacket(const BroadcastFragment *fragment);

    void updateMessageId(int sourceId, int newMessageId);
    void updateMissionId(int sourceId, int newMissionId);

    bool isNewMissionIdLower(int sourceId, int newMissionId) const;
    bool isNewMissionIdSame(int sourceId, int newMissionId) const;
    bool isNewMissionIdHigher(int sourceId, int newMissionId) const;

    bool isNewMessageIdLower(int sourceId, int newMessageId) const;
    bool isNewMessageIdSame(int sourceId, int newMessageId) const;
    bool isNewMessageIdHigher(int sourceId, int newMessageId) const;

private:
    std::vector<FragmentedPacket> packets_;
    std::unordered_map<int, int> latestMessageIds_;
    std::unordered_map<int, int> latestMissionIds_;

};

} // namespace rlora

#endif // HELPERS_INCOMPLETEPACKETLIST_H_

