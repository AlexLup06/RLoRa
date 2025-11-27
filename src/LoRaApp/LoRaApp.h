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

#ifndef LORAAPP_LORAAPP_H_
#define LORAAPP_LORAAPP_H_

#include <omnetpp.h>
#include "inet/common/lifecycle/ILifecycle.h"
#include "inet/common/lifecycle/NodeStatus.h"
#include "inet/common/ModuleAccess.h"
#include "inet/common/lifecycle/LifecycleOperation.h"

#include "../loraSpecific/LoRa/LoRaRadio.h"
#include "../loraSpecific/LoRa/LoRaTagInfo_m.h"

#include "LoRaRobotPacket_m.h"

using namespace omnetpp;
using namespace inet;

namespace rlora {

class LoRaApp: public cSimpleModule, public ILifecycle {
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

    //LoRa parameters control
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
    LoRaApp() {
    }
};

} /* namespace rlora */

#endif /* LORAAPP_LORAAPP_H_ */
