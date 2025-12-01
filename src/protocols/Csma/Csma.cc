#include "Csma.h"

namespace rlora
{
    Define_Module(Csma);

    void Csma::initializeProtocol()
    {
        endBackoff = new cMessage("Backoff");
        backoffHandler = new BackoffHandler(this, endBackoff, slotTime, cw);
        fsm.setState(LISTENING);
    }

    void Csma::finishProtocol()
    {
        cancelAndDelete(endBackoff);
        endBackoff = nullptr;
        delete backoffHandler;
    }

    void Csma::handleWithFsm(cMessage *msg)
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
                FSMA_Event_Transition(Start - Receive,
                                      msg == mediumStateChange && isReceiving(),
                                      RECEIVING, );
                FSMA_Event_Transition(Start - Backoff,
                                      currentTxFrame != nullptr && !isReceiving(),
                                      BACKOFF, );
            }
            FSMA_State(BACKOFF)
            {
                FSMA_Enter(backoffHandler->scheduleBackoffTimer());
                FSMA_Event_Transition(Start - Transmit,
                                      msg == endBackoff,
                                      TRANSMITTING,
                                      backoffHandler->invalidateBackoffPeriod(););
                FSMA_Event_Transition(Start - Receive,
                                      msg == mediumStateChange && isReceiving(),
                                      RECEIVING,
                                      backoffHandler->cancelBackoffTimer();
                                      backoffHandler->decreaseBackoffPeriod(););
            }
            FSMA_State(TRANSMITTING)
            {
                FSMA_Enter(turnOnTransmitter());
                FSMA_Event_Transition(Transmit - Listening,
                                      msg == transmitSwitchDone,
                                      TRANSMITTING,
                                      sendDataFrame(););
                FSMA_Event_Transition(Transmit - Listening,
                                      msg == endTransmission,
                                      SWITCHING,
                                      finishCurrentTransmission(););
            }
            FSMA_State(RECEIVING)
            {
                FSMA_Event_Transition(Receive - Broadcast,
                                      isLowerMessage(msg),
                                      LISTENING,
                                      handlePacket(packet););
            }
        }

        if (fsm.getState() == LISTENING)
        {
            if (currentTxFrame != nullptr)
            {
                handleWithFsm(moreMessagesToSend);
            }
            else if (!packetQueue.isEmpty() && currentTxFrame == nullptr)
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

    void Csma::handlePacket(Packet *packet)
    {
        auto chunk = packet->peekAtFront<inet::Chunk>();

        if (auto msg = dynamic_cast<const BroadcastLeaderFragment *>(chunk.get()))
            handleLeaderFragment(msg);
        else if (auto msg = dynamic_cast<const BroadcastFragment *>(chunk.get()))
            handleFragment(msg);
    }

    void Csma::handleLeaderFragment(const BroadcastLeaderFragment *msg)
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

        // wir senden schon mit dem ersten packet daten
        BroadcastFragment fragmentPayload;
        fragmentPayload.setChunkLength(msg->getChunkLength());
        fragmentPayload.setPayloadSize(msg->getPayloadSize());
        fragmentPayload.setMessageId(messageId);
        fragmentPayload.setMissionId(missionId);
        fragmentPayload.setSource(source);
        fragmentPayload.setFragmentId(0);

        Result result = addToIncompletePacket(&fragmentPayload, isMissionMsg);
        retransmitPacket(result);
    }

    void Csma::handleFragment(const BroadcastFragment *msg)
    {
        int missionId = msg->getMissionId();
        bool isMissionMsg = missionId > 0;

        Result result = addToIncompletePacket(msg, isMissionMsg);
        retransmitPacket(result);
    }

    void Csma::createPacket(int payloadSize, int missionId, int source, bool isMission)
    {
        createBroadcastPacket(payloadSize, missionId, source, isMission);
    }
}