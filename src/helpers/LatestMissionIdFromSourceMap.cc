/*
 * LatestMissionIdFromSourceMap.cpp
 *
 *  Created on: 15 Sep 2025
 *      Author: alexanderlupatsiy
 */

#include "LatestMissionIdFromSourceMap.h"

namespace rlora {

LatestMissionIdFromSourceMap::LatestMissionIdFromSourceMap()
{
}

LatestMissionIdFromSourceMap::~LatestMissionIdFromSourceMap()
{
}

bool LatestMissionIdFromSourceMap::updateMissionId(int nodeId, int newMessageId)
{
    auto it = latestMessageIds_.find(nodeId);
    if (it == latestMessageIds_.end() || newMessageId > it->second) {
        latestMessageIds_[nodeId] = newMessageId;
        return true; // Successfully updated
    }
    return false; // Not updated, newMessageId was not larger
}

bool LatestMissionIdFromSourceMap::isNewMissionIdLarger(int nodeId, int newMessageId) const
{
    if (newMessageId < 0) {
        // this is not a mission message
        return true;
    }
    auto it = latestMessageIds_.find(nodeId);
    if (it == latestMessageIds_.end()) {
        return true; // No messageId yet, so it's considered "larger"
    }
    return newMessageId > it->second;
}

} /* namespace rlora */
