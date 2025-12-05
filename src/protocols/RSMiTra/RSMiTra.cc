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
                FSMA_Event_Transition(able - to - listen,
                                      msg == mediumStateChange || msg == shortWait,
                                      LISTENING, );
                FSMA_Event_Transition(we - got - rts - now-- send - cts,
                                      msg == initiateCTS && isFreeToSend(),
                                      CW_CTS, );
                FSMA_Event_Transition(able - to - receive,
                                      isReceiving(),
                                      RECEIVING, cancelEvent(shortWait));
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
            }
            FSMA_State(SEND_RTS)
            {
                FSMA_Enter(turnOnTransmitter());
                FSMA_Event_Transition(transmitter - is - ready - to - send,
                                      msg == transmitSwitchDone,
                                      SEND_RTS,
                                      sendRTS());
                FSMA_Event_Transition(rts - was - sent - now - wait - cts,
                                      msg == endTransmission,
                                      WAIT_CTS, );
            }
            FSMA_State(WAIT_CTS)
            {
                FSMA_Enter(turnOnReceiver());
                FSMA_Event_Transition(got some other CTS - wait f0r the maximum CTS CW time,
                                      isStrayCTS(packet),
                                      SWITCHING,
                                      cancelEvent(CTSWaitTimeout);
                                      handleStrayCTS(packet, true);
                                      handleCTSTimeout(true););
                FSMA_Event_Transition(we - didnt - get - cts - go - back - to - listening,
                                      msg == CTSWaitTimeout,
                                      SWITCHING,
                                      handleCTSTimeout(true);
                                      scheduleAfter(sifs, shortWait));
                FSMA_Event_Transition(Listening - Receiving,
                                      isOurCTS(packet),
                                      READY_TO_SEND, );
            }
            FSMA_State(READY_TO_SEND)
            {
                FSMA_Event_Transition(got some other CTS - wait f0r the maximum CTS CW time,
                                      isStrayCTS(packet),
                                      SWITCHING,
                                      cancelEvent(CTSWaitTimeout);
                                      handleStrayCTS(packet, true);
                                      handleCTSTimeout(true););
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
                                      sendDataFrame());
                FSMA_Event_Transition(finished - transmission - turn - to - receiver,
                                      msg == endTransmission,
                                      SWITCHING,
                                      finishCurrentTransmission());
            }
            FSMA_State(CW_CTS)
            {
                FSMA_Enter(ctsBackoff->scheduleBackoffTimer(););
                FSMA_Event_Transition(got some other CTS - wait f0r the maximum CTS CW time,
                                      isStrayCTS(packet),
                                      SWITCHING,
                                      ctsBackoff->invalidateBackoffPeriod();
                                      ctsBackoff->cancelBackoffTimer();
                                      handleStrayCTS(packet, true));
                FSMA_Event_Transition(had - to - wait - cw - to - send - cts,
                                      msg == ctsCWTimeout && !isReceiving(),
                                      SEND_CTS,
                                      ctsBackoff->invalidateBackoffPeriod());
                FSMA_Event_Transition(got - packet - from - rts - source,
                                      isPacketFromRTSSource(packet),
                                      SWITCHING,
                                      ctsBackoff->invalidateBackoffPeriod();
                                      ctsBackoff->cancelBackoffTimer();
                                      handlePacket(packet);
                                      scheduleAfter(sifs, shortWait););
                FSMA_Event_Transition(got - packet - from - rts - source,
                                      !ctsCWTimeout->isScheduled() && isPacketNotFromRTSSource(packet),
                                      SWITCHING,
                                      ctsBackoff->invalidateBackoffPeriod();
                                      scheduleAfter(sifs, shortWait));
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
                FSMA_Event_Transition(got some other CTS - wait f0r the maximum CTS CW time,
                                      isStrayCTS(packet),
                                      AWAIT_TRANSMISSION,
                                      handleStrayCTS(packet, true));
                FSMA_Event_Transition(source - didnt - get - cts - just - go - back - to - regular - listening,
                                      msg == transmissionStartTimeout && !isReceiving(),
                                      SWITCHING,
                                      clearRTSsource();
                                      cancelEvent(transmissionEndTimeout);
                                      scheduleAfter(sifs, shortWait););
                FSMA_Event_Transition(received - packet - check - whether - from - rts - source - then - handle,
                                      isPacketFromRTSSource(packet),
                                      SWITCHING,
                                      handlePacket(packet);
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
                                      handlePacket(packet);
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
                currentTxFrame = packetQueue.dequeuePacket();
                handleWithFsm(moreMessagesToSend);
            }
        }

        if (packet != nullptr)
        {
            delete packet;
        }
    }

    void RSMiTra::handlePacket(Packet *packet)
    {
        auto chunk = packet->peekAtFront<inet::Chunk>();
        Ptr<const MessageInfoTag> infoTag = packet->getTag<MessageInfoTag>();

        logEffectiveReception(packet);

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

            int sizeOfFragment = msg->getSize() > MAXIMUM_PACKET_SIZE - BROADCAST_FRAGMENT_META_SIZE ? MAXIMUM_PACKET_SIZE : msg->getSize() + BROADCAST_FRAGMENT_META_SIZE;
            scheduleAfter(0, initiateCTS);
            sizeOfFragment_CTSData = sizeOfFragment;
            sourceOfRTS_CTSData = msg->getHop();
            setRTSsource(msg->getHop());
        }
        else if (auto msg = dynamic_cast<const BroadcastContinuousRts *>(chunk.get()))
        {
            int messageId = msg->getMessageId();
            int source = msg->getSource();
            int missionId = msg->getMissionId();

            if (incompleteMissionPktList.isNewIdSame(source, missionId) || incompleteNeighbourPktList.isNewIdSame(source, messageId))
            {
                scheduleAfter(0, initiateCTS);
                sizeOfFragment_CTSData = msg->getPayloadSizeOfNextFragment();
                sourceOfRTS_CTSData = msg->getHopId();
                setRTSsource(msg->getHopId());
            }
        }
        else if (auto msg = dynamic_cast<const BroadcastFragment *>(chunk.get()))
            handleFragment(msg, infoTag);
        else if (auto msg = dynamic_cast<const BroadcastCTS *>(chunk.get()))
            handleCTS(msg);
    }

    void RSMiTra::createPacket(int payloadSize, int missionId, int source, bool isMission)
    {
        createBroadcastPacketWithContinuousRTS(payloadSize, missionId, source, isMission);
    }
}