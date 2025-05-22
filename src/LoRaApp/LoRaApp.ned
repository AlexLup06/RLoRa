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

package rlora.LoRaApp;

import inet.applications.contract.IApp;

simple LoRaApp like IApp
{
    parameters:
        @class(LoRaApp);
        volatile double timeToFirstTrajectory @unit(s) = default(10s);
        volatile double timeToFirstMission @unit(s) = default(10s);
        volatile double timeToNextTrajectory @unit(s) = default(10s);
        volatile double timeToNextMission @unit(s) = default(10s);
        
        double initialLoRaTP @unit(dBm) = default(14dBm);
        double initialLoRaCF @unit(Hz) = default(868MHz);
        int initialLoRaSF = default(12);
        double initialLoRaBW @unit(Hz) = default(125kHz);
        int initialLoRaCR = default(4);
        bool initialUseHeader = default(true);
    gates:
        input socketIn @labels(LoRaAppPacket/up);
        output socketOut @labels(LoRaAppPacket/down);
}
