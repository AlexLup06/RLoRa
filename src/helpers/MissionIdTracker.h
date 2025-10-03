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

#ifndef HELPERS_MISSIONIDTRACKER_H_
#define HELPERS_MISSIONIDTRACKER_H_

#include <unordered_set>

namespace rlora {

/**
 * MissionTracker: keeps track of which mission IDs were already sent.
 */
class MissionIdTracker
{
  private:
    std::unordered_set<int> sentMissionIds;

  public:
    MissionIdTracker() = default;
    ~MissionIdTracker() = default;

    /**
     * Add missionId to the set (only if not already present).
     * Returns true if it was newly inserted, false if it was already there.
     */
    bool add(int missionId);

    /**
     * Check if missionId is already in the set.
     */
    bool contains(int missionId) const;
};

} // namespace rlora


#endif /* HELPERS_MISSIONIDTRACKER_H_ */
