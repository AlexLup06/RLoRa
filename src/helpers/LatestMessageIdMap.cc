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

#include "LatestMessageIdMap.h"

namespace rlora {

LatestMessageIdMap::LatestMessageIdMap()
{
}

LatestMessageIdMap::~LatestMessageIdMap()
{
}

bool LatestMessageIdMap::updateMessageId(int nodeId, int newMessageId)
{
    auto it = latestMessageIds_.find(nodeId);
    if (it == latestMessageIds_.end() || newMessageId > it->second) {
        latestMessageIds_[nodeId] = newMessageId;
        return true; // Successfully updated
    }
    return false; // Not updated, newMessageId was not larger
}

bool LatestMessageIdMap::isNewMessageIdLarger(int nodeId, int newMessageId) const
{
    auto it = latestMessageIds_.find(nodeId);
    if (it == latestMessageIds_.end()) {
        return true; // No messageId yet, so it's considered "larger"
    }
    return newMessageId > it->second;
}

} // namespace rlora
