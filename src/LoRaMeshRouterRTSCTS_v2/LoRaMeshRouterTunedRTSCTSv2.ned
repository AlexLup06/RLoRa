//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
// 
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
// 
// You should have received a copy of the GNU Lesser General Public License
// along with this program.  If not, see http://www.gnu.org/licenses/.
// 

package rlora.LoRaMeshRouterRTSCTS_v2;

import inet.linklayer.base.MacProtocolBase;
import inet.linklayer.contract.IMacProtocol;

module LoRaMeshRouterRTSCTSv2 extends MacProtocolBase like IMacProtocol
{
    parameters:
        string radioModule = default("^.radio"); // The path to the Radio module 
        string address @mutable = default("auto");
        double bitrate @unit(bps) = 250bps; // 802.15.4-2006 - IEEE Standard for Information technology

        @statistic[timeInQueue](source=timeInQueue; record=vector; interpolationmode=none);
        @statistic[timeOfLastTrajectory](source=timeOfLastTrajectorySignal; record=vector; interpolationmode=none);
        @statistic[throughputBps](source=throughputBps; record=vector; interpolationmode=none; unit=Bps);
        @statistic[effectiveThroughputBps](source=effectiveThroughputBps; record=vector; interpolationmode=none; unit=Bps);
        @statistic[missionIdRtsSent](source=missionIdRtsSent; record=vector; interpolationmode=none; unit=Bps);
        @statistic[missionIdFragmentSent](source=missionIdFragmentSent; record=vector; interpolationmode=none; unit=Bps);
        @statistic[receivedMissionId](source=receivedMissionId; record=vector; interpolationmode=none; unit=Bps);

        @class(LoRaMeshRouterRTSCTSv2);
    gates:
        input upperMgmtIn;
        output upperMgmtOut;
}
