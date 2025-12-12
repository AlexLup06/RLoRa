#include "LoRaApp.h"

namespace rlora
{

    Define_Module(LoRaApp);

    void LoRaApp::initialize(int stage)
    {
        cSimpleModule::initialize(stage);
        if (stage == INITSTAGE_APPLICATION_LAYER)
        {
            timeToFirstTrajectory = par("timeToFirstTrajectory");
            timeToFirstMission = par("timeToFirstMission");
            timeToNextTrajectory = par("timeToNextTrajectory");
            timeToNextMission = par("timeToNextMission");

            sendTrajectory = new cMessage("SendTrajectory");
            sendMission = new cMessage("sendMission");

            scheduleAt(simTime() + timeToFirstTrajectory, sendTrajectory);
            scheduleAt(simTime() + timeToFirstMission, sendMission);

            // LoRa physical layer parameters
            loRaRadio = check_and_cast<LoRaRadio *>(getParentModule()->getSubmodule("LoRaNic")->getSubmodule("radio"));
            loRaRadio->loRaTP = par("initialLoRaTP").doubleValue();
            loRaRadio->loRaCF = units::values::Hz(par("initialLoRaCF").doubleValue());
            loRaRadio->loRaSF = par("initialLoRaSF");
            loRaRadio->loRaBW = inet::units::values::Hz(par("initialLoRaBW").doubleValue());
            loRaRadio->loRaCR = par("initialLoRaCR");
            loRaRadio->loRaUseHeader = par("initialUseHeader");
        }
    }

    void LoRaApp::finish()
    {
        cancelAndDelete(sendTrajectory);
        cancelAndDelete(sendMission);

        sendTrajectory = nullptr;
        sendMission = nullptr;
    }

    void LoRaApp::handleMessage(cMessage *msg)
    {
        if (msg->isSelfMessage())
        {
            if (msg == sendTrajectory)
            {
                sendMessageDown(false);
                simtime_t delta = normal(0, timeToNextTrajectory / 10);
                if (delta < -timeToNextTrajectory / 10)
                {
                    delta = -timeToNextTrajectory / 10;
                }
                if (delta > timeToNextTrajectory / 10)
                {
                    delta = timeToNextTrajectory / 10;
                }

                scheduleAt(simTime() + timeToNextTrajectory + delta, sendTrajectory);
            }

            if (msg == sendMission)
            {
                sendMessageDown(true);
                simtime_t delta = normal(0, timeToNextMission / 10);
                if (delta < -timeToNextTrajectory / 10)
                {
                    delta = -timeToNextTrajectory / 10;
                }
                if (delta > timeToNextTrajectory / 10)
                {
                    delta = timeToNextTrajectory / 10;
                }
                scheduleAt(simTime() + timeToNextMission + delta, sendMission);
            }
        }
        else
        {
            delete msg;
        }
    }

    void LoRaApp::sendMessageDown(bool isMission)
    {
        auto pktRequest = new Packet("DataFrame");
        auto payload = makeShared<LoRaRobotPacket>();

        payload->setIsMission(isMission);
        if (isMission)
        {
            payload->setChunkLength(B(intuniform(50, 245)));
        }
        else
        {
            payload->setChunkLength(B(144));
        }

        auto loraTag = pktRequest->addTagIfAbsent<LoRaTag>();
        loraTag->setBandwidth(getBW());
        loraTag->setCenterFrequency(getCF());
        loraTag->setSpreadFactor(getSF());
        loraTag->setCodeRendundance(getCR());
        loraTag->setPower(mW(math::dBmW2mW(getTP())));

        pktRequest->insertAtBack(payload);
        send(pktRequest, "socketOut");
    }

    bool LoRaApp::handleOperationStage(LifecycleOperation *operation, IDoneCallback *doneCallback)
    {
        Enter_Method_Silent();

        throw cRuntimeError("Unsupported lifecycle operation '%s'", operation->getClassName());
        return true;
    }

    void LoRaApp::setSF(int SF)
    {
        loRaRadio->loRaSF = SF;
    }

    int LoRaApp::getSF()
    {
        return loRaRadio->loRaSF;
    }

    void LoRaApp::setTP(int TP)
    {
        loRaRadio->loRaTP = TP;
    }

    double LoRaApp::getTP()
    {
        return loRaRadio->loRaTP;
    }

    void LoRaApp::setCF(units::values::Hz CF)
    {
        loRaRadio->loRaCF = CF;
    }

    units::values::Hz LoRaApp::getCF()
    {
        return loRaRadio->loRaCF;
    }

    void LoRaApp::setBW(units::values::Hz BW)
    {
        loRaRadio->loRaBW = BW;
    }

    units::values::Hz LoRaApp::getBW()
    {
        return loRaRadio->loRaBW;
    }

    void LoRaApp::setCR(int CR)
    {
        loRaRadio->loRaCR = CR;
    }

    int LoRaApp::getCR()
    {
        return loRaRadio->loRaCR;
    }
}
