#ifndef HELPERS_GENERALHELPERS_H_
#define HELPERS_GENERALHELPERS_H_


using namespace inet;
namespace rlora
{
#define BROADCAST_RTS_SIZE 8
#define BROADCAST_CONTINIOUS_RTS_SIZE 4
#define BROADCAST_LEADER_FRAGMENT_META_SIZE 8
#define BROADCAST_CTS_SIZE 4
#define BROADCAST_FRAGMENT_META_SIZE 5
#define MAXIMUM_PACKET_SIZE 255

    inline int predictSendTime(int size)
    {
        if (size > MAXIMUM_PACKET_SIZE)
        {
            return MAXIMUM_PACKET_SIZE + 20;
        }
        return size + 20;
    }

    inline void scheduleOrExtend(cSimpleModule *caller, cMessage *msg, double delay)
    {
        simtime_t newTime = simTime() + delay;

        if (!msg->isScheduled())
        {
            caller->scheduleAt(newTime, msg);
            return;
        }

        simtime_t oldTime = msg->getArrivalTime();

        if (newTime > oldTime)
        {
            caller->rescheduleAt(newTime, msg);
        }
    }
}

#endif
