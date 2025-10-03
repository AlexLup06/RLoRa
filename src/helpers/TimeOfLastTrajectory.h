#ifndef __TIMEOFTRJECTORY_H
#define __TIMEOFTRJECTORY_H

#include <map>
#include <omnetpp.h>

using namespace omnetpp;

class TimeOfLastTrajectory
{
private:
    std::map<int, simtime_t> lastUpdate;

public:
    // Update the time of a trajectory message from nodeId
    void addTime(int nodeId, simtime_t time)
    {
        lastUpdate[nodeId] = time;
    }

    // Calculate AoI for a given nodeId at current simulation time
    simtime_t calcAgeOfInformation(int nodeId, simtime_t currentTime) const
    {
        auto it = lastUpdate.find(nodeId);
        if (it == lastUpdate.end()) {
            // No info yet → return infinity-like value
            return SIMTIME_MAX;
        }
        return currentTime - it->second;
    }
};

#endif
