#include "MeshRouter.h"

namespace rlora
{
    Define_Module(MeshRouter);

    void MeshRouter::initializeProtocol()
    {
        waitDelay = new cMessage("Wait Delay");
        fsm.setState(LISTENING);
    }

    void MeshRouter::finishProtocol()
    {
        cancelAndDelete(waitDelay);
        waitDelay = nullptr;
    }

    void MeshRouter::handleWithFsm(cMessage *msg)
    {
        Packet *packet = dynamic_cast<Packet *>(msg);
        if (packet != nullptr)
        {
            decapsulate(packet);
        }

        FSMA_Switch(fsm)
        {
            FSMA_State(SWITCHING)
            {
                FSMA_Enter(turnOnReceiver());
                FSMA_Event_Transition(Listening - Receiving,
                                      msg == mediumStateChange,
                                      LISTENING, );
            }
            FSMA_State(LISTENING)
            {
                FSMA_Event_Transition(Listening - Receiving,
                                      isReceiving(),
                                      RECEIVING, );

                FSMA_Event_Transition(Listening - Transmitting_1,
                                      currentTxFrame != nullptr && !waitDelay->isScheduled() && !isReceiving(),
                                      TRANSMITING, );

                FSMA_Event_Transition(Listening - Transmitting_1,
                                      msg == waitDelay && currentTxFrame != nullptr,
                                      TRANSMITING, );
            }
            FSMA_State(TRANSMITING)
            {
                FSMA_Enter(turnOnTransmitter());
                FSMA_Event_Transition(Transmit - Listening,
                                      msg == transmitSwitchDone,
                                      TRANSMITING,
                                      scheduleWaitTimer();
                                      sendDataFrame(););
                FSMA_Event_Transition(Transmit - Listening,
                                      msg == endTransmission,
                                      SWITCHING,
                                      finishCurrentTransmission(););
            }
            FSMA_State(RECEIVING)
            {
                FSMA_Event_Transition(Listening - Receiving,
                                      isLowerMessage(msg),
                                      LISTENING,
                                      handlePacket(packet););
            }
        }

        if (fsm.getState() == LISTENING && !waitDelay->isScheduled())
        {
            if (currentTxFrame != nullptr)
            {
                handleWithFsm(moreMessagesToSend);
            }
            else if (!packetQueue.isEmpty())
            {
                currentTxFrame = packetQueue.dequeuePacket();
                handleWithFsm(moreMessagesToSend);
            }
        }

        if (packet != nullptr)
        {
            delete packet;
        }
    }

    void MeshRouter::handlePacket(Packet *packet)
    {
        auto chunk = packet->peekAtFront<inet::Chunk>();

        if (auto msg = dynamic_cast<const BroadcastRts *>(chunk.get()))
        {
            int messageId = msg->getMessageId();
            int source = msg->getSource();
            int missionId = msg->getMissionId();
            bool isMissionMsg = msg->isMission();

            if (!isMissionMsg && !incompleteNeighbourPktList.isNewIdHigher(source, messageId))
            {
                return;
            }

            if (isMissionMsg && !incompleteMissionPktList.isNewIdHigher(source, missionId))
            {
                return;
            }

            FragmentedPacket incompletePacket;
            incompletePacket.messageId = messageId;
            incompletePacket.missionId = missionId;
            incompletePacket.sourceNode = source;
            incompletePacket.size = msg->getSize();
            incompletePacket.lastHop = msg->getHop();
            incompletePacket.received = 0;
            incompletePacket.corrupted = false;
            incompletePacket.isMission = msg->isMission();

            addPacketToList(incompletePacket, isMissionMsg);

            int size = msg->getSize();
            int waitTime = 20 + predictSendTime(size);
            senderWaitDelay(waitTime);
        }
        else if (auto msg = dynamic_cast<const BroadcastFragment *>(chunk.get()))
        {
            int missionId = msg->getMissionId();
            bool isMissionMsg = missionId > 0;

            Result result = addToIncompletePacket(msg, isMissionMsg);

            if (result.waitTime >= 0)
            {
                senderWaitDelay(result.waitTime);
            }

            retransmitPacket(result);
        }
    }

    void MeshRouter::scheduleWaitTimer()
    {
        Packet *frameToSend = getCurrentTransmission();
        auto waitTimeTag = frameToSend->getTag<WaitTimeTag>();
        int waitTime = waitTimeTag->getWaitTime();

        if (waitTime > 0)
        {
            senderWaitDelay(waitTime);
        }
    }

    void MeshRouter::senderWaitDelay(int waitTime)
    {
        simtime_t newScheduleTime = simTime() + SimTime(waitTime, SIMTIME_MS);

        if (waitDelay->isScheduled())
        {
            simtime_t currentScheduledTime = waitDelay->getArrivalTime();
            if (newScheduleTime > currentScheduledTime)
            {
                cancelEvent(waitDelay);
                scheduleAt(newScheduleTime, waitDelay);
            }
        }
        else
        {
            scheduleAt(newScheduleTime, waitDelay);
        }
    }

    void MeshRouter::createPacket(int payloadSize, int missionId, int source, bool isMission)
    {
        createBroadcastPacketWithRTS(payloadSize, missionId, source, isMission);
    }
}
