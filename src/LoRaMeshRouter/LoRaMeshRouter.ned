package rlora.LoRaMeshRouter;

import inet.linklayer.base.MacProtocolBase;
import inet.linklayer.contract.IMacProtocol;

module LoRaMeshRouter extends MacProtocolBase like IMacProtocol
{
    parameters:
        string radioModule = default("^.radio"); // The path to the Radio module 
        string address @mutable = default("auto");
        double bitrate @unit(bps) = 250bps; // 802.15.4-2006 - IEEE Standard for Information technology

        @statistic[addedToQueueId](source=addedToQueueId; record=vector; interpolationmode=none);
        @statistic[sentId](source=sentId; record=vector; interpolationmode=none);
        @statistic[throughputBps](source=throughputBps; record=vector; interpolationmode=none; unit=Bps);
        @statistic[effectiveThroughputBps](source=effectiveThroughputBps; record=vector; interpolationmode=none; unit=Bps);

        @class(LoRaMeshRouter);
}
