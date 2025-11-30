#include "BackoffHandler.h"

namespace rlora
{

    void BackoffHandler::invalidateBackoffPeriod()
    {
        backoffPeriod = -1;
    }

    bool BackoffHandler::isInvalidBackoffPeriod()
    {
        return backoffPeriod == -1;
    }

    void BackoffHandler::generateBackoffPeriod()
    {
        int slots = owner->intrand(cw);
        backoffPeriod = slots * slotTime;
        ASSERT(backoffPeriod >= 0);
        remainder = cw - slots - 1;
    }

    void BackoffHandler::decreaseBackoffPeriod()
    {
        simtime_t elapsedBackoffTime = simTime() - endBackoff->getSendingTime();
        backoffPeriod -= ((int)(elapsedBackoffTime / slotTime)) * slotTime;
        ASSERT(backoffPeriod >= 0);
    }

    void BackoffHandler::scheduleBackoffTimer()
    {
        if (isInvalidBackoffPeriod())
            generateBackoffPeriod();
        owner->scheduleAfter(backoffPeriod, endBackoff);
    }

    void BackoffHandler::cancelBackoffTimer()
    {
        owner->cancelEvent(endBackoff);
    }

}