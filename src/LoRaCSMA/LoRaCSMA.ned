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

package rlora.LoRaCSMA;

import inet.linklayer.csmaca.CsmaCaMac;

module LoRaCSMA extends CsmaCaMac
{
    parameters:
        bitrate = 250bps;

        @statistic[addedToQueueId](source=addedToQueueId; record=vector; interpolationmode=none);
        @statistic[sentId](source=sentId; record=vector; interpolationmode=none);
        @statistic[throughputBps](source=throughputBps; record=vector; interpolationmode=none; unit=Bps);
        @statistic[effectiveThroughputBps](source=effectiveThroughputBps; record=vector; interpolationmode=none; unit=Bps);
        
        @class(LoRaCSMA);
    gates:
        input upperMgmtIn;
        output upperMgmtOut;
}
