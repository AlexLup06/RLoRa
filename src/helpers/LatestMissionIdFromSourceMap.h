/*
 * LatestMissionIdFromSourceMap.h
 *
 *  Created on: 15 Sep 2025
 *      Author: alexanderlupatsiy
 */

#ifndef HELPERS_LATESTMISSIONIDFROMSOURCEMAP_H_
#define HELPERS_LATESTMISSIONIDFROMSOURCEMAP_H_

#include <unordered_map>

namespace rlora {

class LatestMissionIdFromSourceMap
{
public:
    LatestMissionIdFromSourceMap();
    virtual ~LatestMissionIdFromSourceMap();

    // Updates the messageId for a node. Returns true if update was successful.
    bool updateMissionId(int nodeId, int newMessageId);

    // Checks if the new messageId is larger than the stored one
    bool isNewMissionIdLarger(int nodeId, int newMessageId) const;

private:
    std::unordered_map<int, int> latestMessageIds_;
};

} /* namespace rlora */

#endif /* HELPERS_LATESTMISSIONIDFROMSOURCEMAP_H_ */
