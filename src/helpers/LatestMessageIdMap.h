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

#ifndef HELPERS_LATESTMESSAGEIDMAP_H_
#define HELPERS_LATESTMESSAGEIDMAP_H_

#include <unordered_map>

namespace rlora {

class LatestMessageIdMap
{
public:
    LatestMessageIdMap();
    virtual ~LatestMessageIdMap();

    // Updates the messageId for a node. Returns true if update was successful.
    bool updateMessageId(int nodeId, int newMessageId);

    // Checks if the new messageId is larger than the stored one
    bool isNewMessageIdLarger(int nodeId, int newMessageId) const;

private:
    std::unordered_map<int, int> latestMessageIds_;
};

} /* namespace rlora */

#endif /* HELPERS_LATESTMESSAGEIDMAP_H_ */
