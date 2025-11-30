#ifndef RADIO_BASE_H_
#define RADIO_BASE_H_

#include "../common/common.h"
#include "MacContext.h"

using namespace inet;
using namespace physicallayer;

namespace rlora
{
    class RadioBase : public MacContext
    {
    protected:
        cMessage *mediumStateChange = nullptr;
        cMessage *endTransmission = nullptr;
        cMessage *transmitSwitchDone = nullptr;
        cMessage *receptionStated = nullptr;

        IRadio *radio = nullptr;
        IRadio::TransmissionState transmissionState = IRadio::TRANSMISSION_STATE_UNDEFINED;
        IRadio::ReceptionState receptionState = IRadio::RECEPTION_STATE_UNDEFINED;

        void initializeRadio();
        void finishRadio();

        virtual bool isReceiving();
        bool isMediumFree();

        void turnOnReceiver();
        void turnOnTransmitter();
        void turnOffReceiver();
        virtual void receiveSignal(cComponent *source, simsignal_t signalID, intval_t value, cObject *details) override;
    };
}

#endif