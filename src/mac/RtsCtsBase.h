#ifndef RTS_CTS_BASE_H_
#define RTS_CTS_BASE_H_

#include "../common/common.h"
#include "MacBase.h"

using namespace inet;

namespace rlora
{
    class RtsCtsBase : public MacBase
    {
    public:
    protected:
        simtime_t sifs = 0.002;

        int cwBackoff = 16;
        simtime_t backoffFS = 0.019 + 0.003;

        simtime_t ctsFS = 0.016 + 0.003;
        int cwCTS = 16;

        int sizeOfFragment_CTSData = -1;
        int sourceOfRTS_CTSData = -1;
        int rtsSource = -1;

        cMessage *endBackoff = nullptr;
        cMessage *CTSWaitTimeout = nullptr;
        cMessage *receivedCTS = nullptr;
        cMessage *endOngoingMsg = nullptr;
        cMessage *initiateCTS = nullptr;
        cMessage *transmissionStartTimeout = nullptr;
        cMessage *transmissionEndTimeout = nullptr;
        cMessage *ctsCWTimeout = nullptr;
        cMessage *shortWait = nullptr;

        BackoffHandler *ctsBackoff;
        BackoffHandler *regularBackoff;

        void initializeProtocol() override;
        virtual void initializeRtsCtsProtocol() {};
        void finishProtocol() override;

        void handleCTS(const BroadcastCTS *cts);
        void handleFragment(const BroadcastFragment *fragment, Ptr<const MessageInfoTag> infoTag);

        void handleCTSTimeout(bool withRetry);
        void sendCTS(bool withRemainder);
        void sendRTS();
        bool isCTSForSameRTSSource(Packet *packet);
        bool isPacketFromRTSSource(Packet *packet);
        bool isOurCTS(Packet *packet);
        bool isFreeToSend();
        bool withRTS();
        void clearRTSsource();
        void setRTSsource(int rtsSourceId);

        bool isStrayCTS(Packet *packet);
        void handleStrayCTS(Packet *packet, bool withRemainder);

        bool isRTS(Packet *packet);
        void handleUnhandeledRTS();

        bool isNotOurDataPacket(Packet *packet);
        bool isDataPacket(Packet *packet);

    private:
    };
}

#endif