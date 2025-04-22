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

#include "../LoRa/LoRaRadio.h"
#include "LoRaRobotpacket_m.h"

using namespace omnetpp;
using namespace inet;

namespace rlora {

class LoRaApp: public cSimpleModule, public ILifecycle {
protected:
    void initialize(int stage) override;
    void handleMessage(cMessage *msg) override;
    void sendMessageDown();
    int numInitStages() const override { return NUM_INIT_STAGES; }
    virtual bool handleOperationStage(LifecycleOperation *operation,
            IDoneCallback *doneCallback) override;

    int numberOfPacketsToSend;
    int sentPackets;
    cMessage *sendMessage;

    simtime_t timeToFirstPacket;
    simtime_t timeToNextPacket;

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
