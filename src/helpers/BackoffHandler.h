#ifndef BACKOFF_HANDLER_H_
#define BACKOFF_HANDLER_H_

#include "../common/common.h"
#include <omnetpp.h>

using namespace inet;

namespace rlora
{
    class BackoffHandler
    {
    public:
        BackoffHandler(cSimpleModule *_owner, cMessage *_endBackoff, simtime_t _slotTime, int _cw)
            : owner(_owner),
              backoffPeriod(-1),
              endBackoff(_endBackoff),
              slotTime(_slotTime),
              cw(_cw)
        {
        }

        void invalidateBackoffPeriod();
        bool isInvalidBackoffPeriod();
        void generateBackoffPeriod();
        void decreaseBackoffPeriod();
        void scheduleBackoffTimer();
        void cancelBackoffTimer();

        cSimpleModule *owner;

        int chosenSlot = 0;

        simtime_t backoffPeriod;
        cMessage *endBackoff;
        int remainder = 0;

        simtime_t slotTime;
        int cw;
    };
}

#endif