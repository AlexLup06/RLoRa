#include "RSMiTra.h"

namespace rlora
{
    Define_Module(RSMiTra);

    void RSMiTra::initializeRtsCtsProtocol()
    {
        fsm.setState(LISTENING);
    }

    void RSMiTra::handleWithFsm(cMessage *msg)
    {
        auto pkt = dynamic_cast<Packet *>(msg);

        EV << "Handlewigthfsms" << endl;
        EV << "Handlewigthfsms: " << msg << endl;
        EV << "CurrentTxFrame: " << currentTxFrame << endl;
        EV << "We are in: " << fsm.getStateName() << endl;

        FSMA_Switch(fsm)
        {

            FSMA_State(SWITCHING)
            {
                FSMA_Enter(turnOnReceiver());
                FSMA_Event_Transition(able - to - listen,
                                      msg == mediumStateChange || msg == shortWait,
                                      LISTENING, );
                FSMA_Event_Transition(we - got - rts - now-- send - cts,
                                      msg == initiateCTS && isFreeToSend(),
                                      CW_CTS, );
            }
            FSMA_State(LISTENING)
            {
                FSMA_Event_Transition(able - to - receive,
                                      isReceiving(),
                                      RECEIVING, );
                FSMA_Event_Transition(we - got - rts - now-- send - cts,
                                      msg == initiateCTS && isFreeToSend(),
                                      CW_CTS, );
                FSMA_Event_Transition(something - to - send - medium - free - start - backoff,
                                      currentTxFrame != nullptr && isMediumFree() && isFreeToSend(),
                                      BACKOFF, );
            }
            FSMA_State(BACKOFF)
            {
                FSMA_Enter(regularBackoff->scheduleBackoffTimer());
                FSMA_Event_Transition(backoff - finished - send - rts,
                                      msg == endBackoff && withRTS(),
                                      SEND_RTS,
                                      regularBackoff->invalidateBackoffPeriod());
                FSMA_Event_Transition(backoff - finished - message - without - rts - only - nodeannounce,
                                      msg == endBackoff && !withRTS(),
                                      TRANSMITING,
                                      regularBackoff->invalidateBackoffPeriod());
                FSMA_Event_Transition(receiving - msg - cancle - backoff - listen - now,
                                      isReceiving(),
                                      RECEIVING,
                                      regularBackoff->cancelBackoffTimer();
                                      regularBackoff->decreaseBackoffPeriod(););
                FSMA_Event_Transition(we - got - rts - now-- send - cts,
                                      msg == initiateCTS && isFreeToSend(),
                                      CW_CTS,
                                      regularBackoff->cancelBackoffTimer();
                                      regularBackoff->decreaseBackoffPeriod(););
            }
            FSMA_State(SEND_RTS)
            {
                FSMA_Enter(turnOnTransmitter());
                FSMA_Event_Transition(transmitter - is - ready - to - send,
                                      msg == transmitSwitchDone,
                                      SEND_RTS,
                                      sendRTS(););
                FSMA_Event_Transition(rts - was - sent - now - wait - cts,
                                      msg == endTransmission,
                                      WAIT_CTS, );
            }
            FSMA_State(WAIT_CTS)
            {
                FSMA_Enter(turnOnReceiver());
                FSMA_Event_Transition(we - didnt - get - cts - go - back - to - listening,
                                      msg == CTSWaitTimeout,
                                      SWITCHING,
                                      handleCTSTimeout(true);
                                      scheduleAfter(sifs, shortWait));
                FSMA_Event_Transition(Listening - Receiving,
                                      isOurCTS(msg),
                                      READY_TO_SEND,
                                      delete pkt;);
            }
            FSMA_State(READY_TO_SEND)
            {
                FSMA_Event_Transition(we - didnt - get - cts - go - back - to - listening,
                                      msg == CTSWaitTimeout,
                                      TRANSMITING, );
            }
            FSMA_State(TRANSMITING)
            {
                FSMA_Enter(turnOnTransmitter());
                FSMA_Event_Transition(send - data - now,
                                      msg == transmitSwitchDone,
                                      TRANSMITING,
                                      sendDataFrame(););
                FSMA_Event_Transition(finished - transmission - turn - to - receiver,
                                      msg == endTransmission,
                                      SWITCHING,
                                      finishCurrentTransmission(););
            }
            FSMA_State(CW_CTS)
            {
                FSMA_Enter(ctsBackoff->scheduleBackoffTimer(););
                FSMA_Event_Transition(had - to - wait - cw - to - send - cts,
                                      msg == ctsCWTimeout && !isReceiving(),
                                      SEND_CTS,
                                      ctsBackoff->invalidateBackoffPeriod());
                FSMA_Event_Transition(got - packet - from - rts - source,
                                      isPacketFromRTSSource(msg),
                                      SWITCHING,
                                      ctsBackoff->invalidateBackoffPeriod();
                                      ctsBackoff->cancelBackoffTimer();
                                      handlePacket(pkt);
                                      scheduleAfter(sifs, shortWait););
            }
            FSMA_State(SEND_CTS)
            {
                FSMA_Enter(turnOnTransmitter());
                FSMA_Event_Transition(transmitter - on - now - send - cts,
                                      msg == transmitSwitchDone,
                                      SEND_CTS,
                                      sendCTS(true););
                FSMA_Event_Transition(finished - cts - sending - turn - to - receiver,
                                      msg == endTransmission,
                                      AWAIT_TRANSMISSION, );
            }
            FSMA_State(AWAIT_TRANSMISSION)
            {
                FSMA_Enter(turnOnReceiver());
                FSMA_Event_Transition(source - didnt - get - cts - just - go - back - to - regular - listening,
                                      msg == transmissionStartTimeout && !isReceiving(),
                                      SWITCHING,
                                      clearRTSsource();
                                      cancelEvent(transmissionEndTimeout);
                                      scheduleAfter(sifs, shortWait););
                FSMA_Event_Transition(received - packet - check - whether - from - rts - source - then - handle,
                                      isPacketFromRTSSource(msg),
                                      SWITCHING,
                                      handlePacket(pkt);
                                      cancelEvent(transmissionStartTimeout);
                                      cancelEvent(transmissionEndTimeout);
                                      scheduleAfter(sifs, shortWait););
                FSMA_Event_Transition(got - some - random - message - just - remove - then - go - back - to - listening,
                                      msg == transmissionEndTimeout,
                                      SWITCHING,
                                      scheduleAfter(sifs, shortWait););
            }
            FSMA_State(RECEIVING)
            {
                FSMA_Event_Transition(received - message - handle - keep - listening,
                                      isLowerMessage(msg),
                                      SWITCHING,
                                      handlePacket(pkt);
                                      scheduleAfter(sifs, shortWait));
            }
        }

        if (fsm.getState() == LISTENING && isFreeToSend() && isMediumFree() && msg != initiateCTS)
        {
            if (currentTxFrame != nullptr)
            {
                handleWithFsm(moreMessagesToSend);
            }
            else if (!packetQueue.isEmpty() && currentTxFrame == nullptr)
            {
                EV << "Dequeuing and deleting current tx" << endl;
                currentTxFrame = packetQueue.dequeuePacket();
                handleWithFsm(moreMessagesToSend);
            }
        }
    }

    void RSMiTra::handlePacket(Packet *packet)
    {
        decapsulate(packet);
        auto chunk = packet->peekAtFront<inet::Chunk>();

        if (auto msg = dynamic_cast<const BroadcastHeader *>(chunk.get()))
        {
            int messageId = msg->getMessageId();
            int source = msg->getSource();
            int missionId = msg->getMissionId();
            bool isMissionMsg = msg->getRetransmit();

            if (!isMissionMsg && !incompleteNeighbourPktList.isNewIdHigher(source, messageId))
            {
                delete packet;
                return;
            }

            if (isMissionMsg && !incompleteMissionPktList.isNewIdHigher(source, missionId))
            {
                delete packet;
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
            incompletePacket.retransmit = msg->getRetransmit();

            addPacketToList(incompletePacket, isMissionMsg);

            int sizeOfFragment = msg->getSize() > 255 - BROADCAST_FRAGMENT_META_SIZE ? 255 : msg->getSize() + BROADCAST_FRAGMENT_META_SIZE;
            scheduleAfter(0, initiateCTS);
            sizeOfFragment_CTSData = sizeOfFragment;
            sourceOfRTS_CTSData = msg->getHop();
            setRTSsource(msg->getHop());
        }
        else if (auto msg = dynamic_cast<const BroadcastContinuousHeader *>(chunk.get()))
        {
            int messageId = msg->getMessageId();
            int source = msg->getSource();
            int missionId = msg->getMissionId();

            if (incompleteMissionPktList.isNewIdSame(source, missionId) || incompleteNeighbourPktList.isNewIdSame(source, messageId))
            {
                scheduleAfter(0, initiateCTS);
                sizeOfFragment_CTSData = msg->getPayloadSizeOfNextFragment(); // TODO
                sourceOfRTS_CTSData = msg->getHopId();
                setRTSsource(msg->getHopId());
            }
        }
        else if (auto msg = dynamic_cast<const BroadcastFragment *>(chunk.get()))
        {
            int missionId = msg->getMissionId();
            auto typeTag = packet->getTag<MessageInfoTag>();
            bool isMissionMsg = !typeTag->isNeighbourMsg();

            Result result = addToIncompletePacket(msg, isMissionMsg);
            retransmitPacket(result);
        }
        else if (auto msg = dynamic_cast<const BroadcastCTS *>(chunk.get()))
        {
            ASSERT(msg->getHopId() != nodeId);
            int size = msg->getSizeOfFragment();
            if (endOngoingMsg->isScheduled())
            {
                cancelEvent(endOngoingMsg);
            }
            ASSERT(size > 0);
            scheduleAfter(predictOngoingMsgTime(size) + sifs, endOngoingMsg);
        }
        delete packet;
    }

    void RSMiTra::handleLowerPacket(Packet *msg)
    {
        if (fsm.getState() == RECEIVING || fsm.getState() == WAIT_CTS || fsm.getState() == CW_CTS || fsm.getState() == AWAIT_TRANSMISSION)
        {
            handleWithFsm(msg);
        }
        else
        {
            delete msg;
        }
    }

    void RSMiTra::createPacket(int payloadSize, int missionId, int source, bool retransmit)
    {
        createBroadcastPacketWithContinuousRTS(payloadSize, missionId, source, retransmit);
    }
}