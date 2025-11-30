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
        simtime_t backoffFS = 0.021; // timeOnAir of Header + 0.001 puffer
        simtime_t ctsFS = 0.018;     // timeOnAir of Header + 0.0005 puffer
        int cwCTS = 16;
        int cwBackoff = 8;

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

        void handleCTSTimeout(bool withRetry);
        void handleCTS(Packet *pkt);
        void sendCTS(bool withRemainder);
        void sendRTS();
        bool isCTSForSameRTSSource(cMessage *msg);
        bool isPacketFromRTSSource(cMessage *msg);
        bool isOurCTS(cMessage *msg);
        bool isFreeToSend();
        bool withRTS();
        void clearRTSsource();
        void setRTSsource(int rtsSourceId);

    private:
    };
}

#endif