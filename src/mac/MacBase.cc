#include "MacBase.h"

namespace rlora
{
    void MacBase::initialize(int stage)
    {
        MacProtocolBase::initialize(stage);
        if (stage == INITSTAGE_LOCAL)
        {
            initializeRadio();
            initializeMacContext();

            moreMessagesToSend = new cMessage("moreMessagesToSend");

            missionIdRtsSent = registerSignal("missionIdRtsSent");
            receivedMissionId = registerSignal("receivedMissionId");
        }
        else if (stage == INITSTAGE_LINK_LAYER)
        {
            turnOnReceiver();
            initializeProtocol();
        }
    }

    void MacBase::finish()
    {
        cancelAndDelete(moreMessagesToSend);

        moreMessagesToSend = nullptr;

        currentTxFrame = nullptr;

        cSimulation *sim = getSimulation();
        cFutureEventSet *fes = sim->getFES();
        for (int i = 0; i < fes->getLength(); ++i)
        {
            cEvent *event = fes->get(i);
            cMessage *msg = dynamic_cast<cMessage *>(event);
            if (msg != nullptr)
            {
                cModule *mod = msg->getArrivalModule();
                if (msg && msg->isScheduled() && msg->getOwner() == this)
                {
                    cancelAndDelete(msg);
                    msg = nullptr;
                }
            }
        }

        finishRadio();
        finishPacketBase();
        finishProtocol();
    }

    void MacBase::handleSelfMessage(cMessage *msg)
    {
        handleWithFsm(msg);
    }

    void MacBase::handleUpperPacket(Packet *packet)
    {
        const auto &payload = packet->peekAtFront<LoRaRobotPacket>();
        bool isMission = payload->isMission();
        int missionId = -2;
        if (isMission)
        {
            missionId = -1;
        }

        createPacket(packet->getByteLength(), missionId, -1, isMission);

        if (currentTxFrame == nullptr)
        {
            Packet *packetToSend = dequeueCustomPacket();
            currentTxFrame = packetToSend;
        }
        handleWithFsm(moreMessagesToSend);
        delete packet;
    }

    void MacBase::finishCurrentTransmission()
    {
        deleteCurrentTxFrame();
    }

    Packet *MacBase::getCurrentTransmission()
    {
        ASSERT(currentTxFrame != nullptr);
        return currentTxFrame;
    }

    void MacBase::retransmitPacket(Result result)
    {
        if (result.isComplete)
        {
            if (result.sendUp)
            {
                cMessage *readyMsg = new cMessage("Ready");
                sendUp(readyMsg);

                if (!result.completePacket.isMission || result.completePacket.sourceNode == nodeId)
                {
                    return;
                }

                emit(receivedMissionId, result.completePacket.missionId);
                createPacket(result.completePacket.size, result.completePacket.missionId, result.completePacket.sourceNode, result.completePacket.isMission);
            }

            removePacketById(result.completePacket.missionId, result.completePacket.messageId, result.isMission);
        }
    }

    void MacBase::sendDataFrame()
    {
        auto frameToSend = getCurrentTransmission();
        sendDown(frameToSend->dup());

        auto infoTag = frameToSend->getTag<MessageInfoTag>();
        if (!infoTag->isNeighbourMsg())
        {
            if (infoTag->isHeader())
            {
                emit(missionIdRtsSent, infoTag->getMissionId());
            }
        }

        if (infoTag->getHasUsefulData())
        {
            DataLogger::getInstance()->logEffectiveTransmission();
            DataLogger::getInstance()->logEffectiveBytesSent(infoTag->getPayloadSize());
        }

        DataLogger::getInstance()->logTransmission();
        DataLogger::getInstance()->logBytesSent(frameToSend->getByteLength());
    }

    void MacBase::logEffectiveReception(Packet *packet)
    {
        Ptr<const MessageInfoTag> infoTag = packet->getTag<MessageInfoTag>();

        if (infoTag->getHasUsefulData())
        {
            DataLogger::getInstance()->logEffectiveBytesReceived(infoTag->getPayloadSize());
            DataLogger::getInstance()->logEffectiveReceptions();
        }
    }

    void MacBase::handleLowerPacket(Packet *msg)
    {
        handleWithFsm(msg);
    }
}
