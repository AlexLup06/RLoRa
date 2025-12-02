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
              cw(_cw),
              cwCONST(_cw)
        {
        }

        void invalidateBackoffPeriod();
        bool isInvalidBackoffPeriod();
        void generateBackoffPeriod();
        void decreaseBackoffPeriod();
        void scheduleBackoffTimer();
        void cancelBackoffTimer();

        void increaseCw();
        void resetCw();

        int chosenSlot = 0;
        int remainder = 0;

    private:
        simtime_t slotTime;
        int cw;
        cMessage *endBackoff;
        simtime_t backoffPeriod;
        cSimpleModule *owner;
        int cwCONST;
    };
}

#endif