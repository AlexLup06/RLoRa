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

#include "LoRaLogNormalShadowing.h"
#include "inet/common/INETMath.h"

namespace rlora {

Define_Module(LoRaLogNormalShadowing);

LoRaLogNormalShadowing::LoRaLogNormalShadowing()
{
}

void LoRaLogNormalShadowing::initialize(int stage)
{
    FreeSpacePathLoss::initialize(stage);
    if (stage == INITSTAGE_LOCAL) {
        sigma = par("sigma");
        gamma = par("gamma");
        d0 = m(par("d0"));
    }
}

std::ostream& LoRaLogNormalShadowing::printToStream(std::ostream &stream, int level, int evFlags) const
{
    stream << "LoRaLogNormalShadowing";
    if (level <= PRINT_LEVEL_TRACE)
        stream << ", alpha = " << alpha << ", systemLoss = " << systemLoss << ", sigma = " << sigma;
    return stream;
}


double LoRaLogNormalShadowing::computePathLoss(mps propagationSpeed, Hz frequency, m distance) const
{
    double PL_d0_db = 112;
    double PL_db = PL_d0_db + 10 * gamma * log10(unit(distance / d0).get()) + normal(0.0, sigma);

    // Compute max communication range using the transmission power
    m maxRange = computeRange(W(0.112202)); // 0.112202 W = 20.5 dBm total (20 dBm + 0.5 dBi)

    EV << "Distance: " << distance << endl;
    EV << "maxRange " << maxRange << endl;

    return math::dB2fraction(-PL_db);
}

m LoRaLogNormalShadowing::computeRange(W transmissionPower) const
{
    double PL_d0_db = 112;
    double max_sensitivity = -124.5;
    // war vorher:
    double trans_power_db = round(10 * log10(transmissionPower.get() * 1000));

    EV << "LoRaLogNormalShadowing transmissionPower in W = " << transmissionPower << " in dBm = " << trans_power_db << endl;

    double rhs = (trans_power_db - PL_d0_db - max_sensitivity) / (10 * gamma);
    double distance = d0.get() * pow(10, rhs);
    return m(distance);
}


}
