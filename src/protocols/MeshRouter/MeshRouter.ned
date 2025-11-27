package rlora.protocols.MeshRouter;

import inet.linklayer.base.MacProtocolBase;
import inet.linklayer.contract.IMacProtocol;

module MeshRouter extends MacProtocolBase like IMacProtocol
{
    parameters:
        string radioModule = default("^.radio"); // The path to the Radio module 
        string address @mutable = default("auto");
        double bitrate @unit(bps) = 250bps; // 802.15.4-2006 - IEEE Standard for Information technology

        @statistic[timeInQueue](source=timeInQueue; record=vector; interpolationmode=none);
        @statistic[timeOfLastTrajectory](source=timeOfLastTrajectorySignal; record=vector; interpolationmode=none);
        @statistic[throughputBps](source=throughputBps; record=vector; interpolationmode=none; unit=Bps);
        @statistic[effectiveThroughputBps](source=effectiveThroughputBps; record=vector; interpolationmode=none; unit=Bps);
        @statistic[missionIdFragmentSent](source=missionIdFragmentSent; record=vector; interpolationmode=none; unit=Bps);
        @statistic[receivedMissionId](source=receivedMissionId; record=vector; interpolationmode=none; unit=Bps);

        @class(MeshRouter);
}
