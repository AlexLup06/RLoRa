package rlora.mac;

import inet.linklayer.base.MacProtocolBase;
import inet.linklayer.contract.IMacProtocol;

module MacContext extends MacProtocolBase like IMacProtocol
{
    parameters:
        string radioModule = default("^.radio"); // The path to the Radio module 
        string address @mutable = default("auto");
        double bitrate @unit(bps) = 250bps; // 802.15.4-2006 - IEEE Standard for Information technology

        @statistic[missionIdRtsSent](source=missionIdRtsSent; record=vector; interpolationmode=none);
        @statistic[receivedMissionId](source=receivedMissionId; record=vector; interpolationmode=none);

        @statistic[receivedFragmentId](source=receivedFragmentId; record=vector; interpolationmode=none);

        @class(MacContext);
}