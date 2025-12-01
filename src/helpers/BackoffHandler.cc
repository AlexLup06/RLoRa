#include "BackoffHandler.h"
using omnetpp::uniform;
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
        int slot = owner->intrand(cw);
        chosenSlot = slot;
        backoffPeriod = slot * slotTime + owner->uniform(0, 0.003); // random jitter
        ASSERT(backoffPeriod >= 0);
        remainder = cw - slot - 1;
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