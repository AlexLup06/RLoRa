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

#ifndef HELPERS_GENERALHELPERS_H_
#define HELPERS_GENERALHELPERS_H_

#include <random>
#include <sstream>

namespace rlora {

using namespace std;
/**
 * @brief Predicts the time needed to send a packet of a given size.
 *
 * @param size The size of the packet in bytes.
 * @return Predicted send time in arbitrary time units.
 */
int predictSendTime(int size);
string generate_uuid();

#define BROADCAST_HEADER_SIZE 8
#define NODE_ANNOUNCE_SIZE 12
#define BROADCAST_FRAGMENT_META_SIZE 4
#define BROADCAST_LEADER_FRAGMENT_META_SIZE 8

} // namespace rlora

#endif /* HELPERS_GENERALHELPERS_H_ */
