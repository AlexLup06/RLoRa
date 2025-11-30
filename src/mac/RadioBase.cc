#include "RadioBase.h"

namespace rlora
{

    void RadioBase::initializeRadio()
    {
        // subscribe for the information of the carrier sense
        cModule *radioModule = getModuleFromPar<cModule>(par("radioModule"), this);
        radioModule->subscribe(IRadio::receptionStateChangedSignal, this);
        radioModule->subscribe(IRadio::transmissionStateChangedSignal, this);
        radioModule->subscribe(LoRaRadio::droppedPacket, this);
        radio = check_and_cast<IRadio *>(radioModule);
        loRaRadio = check_and_cast<LoRaRadio *>(radioModule);

        mediumStateChange = new cMessage("MediumStateChange");
        endTransmission = new cMessage("End Transmission");
        transmitSwitchDone = new cMessage("transmitSwitchDone");
        receptionStated = new cMessage("receptionStated");
    }

    void RadioBase::finishRadio()
    {
        cancelAndDelete(receptionStated);
        cancelAndDelete(transmitSwitchDone);
        cancelAndDelete(endTransmission);
        cancelAndDelete(mediumStateChange);

        receptionStated = nullptr;
        transmitSwitchDone = nullptr;
        endTransmission = nullptr;
        mediumStateChange = nullptr;
    }

    void RadioBase::receiveSignal(cComponent *source, simsignal_t signalID, intval_t value, cObject *details)
    {
        Enter_Method_Silent();
        if (signalID == IRadio::receptionStateChangedSignal)
        {
            IRadio::ReceptionState newRadioReceptionState = (IRadio::ReceptionState)value;
            handleWithFsm(mediumStateChange);
            receptionState = newRadioReceptionState;
        }
        else if (signalID == IRadio::transmissionStateChangedSignal)
        {
            IRadio::TransmissionState newRadioTransmissionState = (IRadio::TransmissionState)value;
            if (transmissionState == IRadio::TRANSMISSION_STATE_TRANSMITTING && newRadioTransmissionState == IRadio::TRANSMISSION_STATE_IDLE)
            {
                transmissionState = newRadioTransmissionState;
                handleWithFsm(endTransmission);
            }

            if (transmissionState == IRadio::TRANSMISSION_STATE_UNDEFINED && newRadioTransmissionState == IRadio::TRANSMISSION_STATE_IDLE)
            {
                transmissionState = newRadioTransmissionState;
                handleWithFsm(transmitSwitchDone);
            }
            transmissionState = newRadioTransmissionState;
        }
    }

    bool RadioBase::isReceiving()
    {
        return radio->getReceptionState() == IRadio::RECEPTION_STATE_RECEIVING;
    }

    void RadioBase::turnOnReceiver()
    {

        loRaRadio->setRadioMode(IRadio::RADIO_MODE_RECEIVER);
    }

    void RadioBase::turnOffReceiver()
    {

        loRaRadio->setRadioMode(IRadio::RADIO_MODE_SLEEP);
    }

    void RadioBase::turnOnTransmitter()
    {
        loRaRadio->setRadioMode(IRadio::RADIO_MODE_TRANSMITTER);
    }

    bool RadioBase::isMediumFree()
    {
        return radio->getReceptionState() == IRadio::RECEPTION_STATE_IDLE;
    }
};