#include "Aloha.h"

namespace rlora
{
    Define_Module(Aloha);

    void Aloha::initializeProtocol()
    {
        fsm.setState(LISTENING);
    }

    void Aloha::handleWithFsm(cMessage *msg)
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
                                      currentTxFrame != nullptr && !isReceiving(),
                                      TRANSMITING, );
            }
            FSMA_State(TRANSMITING)
            {
                FSMA_Enter(turnOnTransmitter());
                FSMA_Event_Transition(Transmit - Listening,
                                      msg == transmitSwitchDone,
                                      TRANSMITING,
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

        if (!packetQueue.isEmpty() && currentTxFrame == nullptr)
        {
            currentTxFrame = packetQueue.dequeuePacket();
            handleWithFsm(moreMessagesToSend);
        }

        if (packet != nullptr)
        {
            delete packet;
        }
    }

    void Aloha::handlePacket(Packet *packet)
    {
        auto chunk = packet->peekAtFront<inet::Chunk>();

        logEffectiveReception(packet);

        if (auto msg = dynamic_cast<const BroadcastLeaderFragment *>(chunk.get()))
            handleLeaderFragment(msg);
        else if (auto msg = dynamic_cast<const BroadcastFragment *>(chunk.get()))
            handleFragment(msg);
    }

    void Aloha::handleLeaderFragment(const BroadcastLeaderFragment *msg)
    {
        int messageId = msg->getMessageId();
        int source = msg->getSource();
        int missionId = msg->getMissionId();
        bool isMissionMsg = msg->isMission();

        if (!shouldHandleRTS(isMissionMsg, source, messageId, missionId))
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

    void Aloha::handleFragment(const BroadcastFragment *msg)
    {
        int missionId = msg->getMissionId();
        bool isMissionMsg = missionId > 0;

        // We do check if we have started this packet with a header in the function
        Result result = addToIncompletePacket(msg, isMissionMsg);
        retransmitPacket(result);
    }

    void Aloha::createPacket(int payloadSize, int missionId, int source, bool isMission)
    {
        createBroadcastPacket(payloadSize, missionId, source, isMission);
    }
}