#ifndef LORAAPP_LORAAPP_H_
#define LORAAPP_LORAAPP_H_

#include <omnetpp.h>
#include "../common/common.h"

using namespace omnetpp;
using namespace inet;

namespace rlora
{

    class LoRaApp : public cSimpleModule, public ILifecycle
    {
    protected:
        virtual void initialize(int stage) override;
        virtual void handleMessage(cMessage *msg) override;
        virtual void finish() override;
        void sendMessageDown(bool isMission);
        int numInitStages() const override { return NUM_INIT_STAGES; }
        virtual bool handleOperationStage(LifecycleOperation *operation,
                                          IDoneCallback *doneCallback) override;

        cMessage *sendTrajectory;
        cMessage *sendMission;

        simtime_t timeToFirstTrajectory;
        simtime_t timeToFirstMission;
        simtime_t timeToNextTrajectory;
        simtime_t timeToNextMission;

        // LoRa parameters control
        LoRaRadio *loRaRadio;

        void setSF(int SF);
        int getSF();
        void setTP(int TP);
        double getTP();
        void setCR(int CR);
        int getCR();
        void setCF(units::values::Hz CF);
        units::values::Hz getCF();
        void setBW(units::values::Hz BW);
        units::values::Hz getBW();

    public:
        LoRaApp()
        {
        }
    };

}

#endif
