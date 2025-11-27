/*
 * common.h
 *
 *  Created on: 27 Nov 2025
 *      Author: alexanderlupatsiy
 */

#ifndef COMMON_COMMON_H_
#define COMMON_COMMON_H_

#include "inet/physicallayer/wireless/common/contract/packetlevel/IRadio.h"

#include "inet/common/FSMA.h"
#include "inet/common/Protocol.h"
#include "inet/common/ModuleAccess.h"
#include "inet/common/ProtocolTag_m.h"
#include "inet/common/packet/Packet.h"
#include "inet/common/ProtocolGroup.h"

#include "inet/queueing/contract/IActivePacketSink.h"
#include "inet/queueing/contract/IPacketQueue.h"

#include "inet/linklayer/base/MacProtocolBase.h"

#include "inet/linklayer/contract/IMacProtocol.h"

#include "inet/linklayer/csmaca/CsmaCaMac.h"
#include "inet/linklayer/csmaca/CsmaCaMacHeader_m.h"

#include "inet/linklayer/common/UserPriority.h"
#include "inet/linklayer/common/Ieee802Ctrl.h"
#include "inet/linklayer/common/MacAddressTag_m.h"
#include "inet/linklayer/common/InterfaceTag_m.h"

#include "../loraSpecific/LoRa/LoRaRadio.h"
#include "../loraSpecific/LoRa/LoRaMacFrame_m.h"
#include "../loraSpecific/LoRa/LoRaTagInfo_m.h"
#include "../loraSpecific/LoRa/LoRaTagInfo_m.h"

#include "../LoRaApp/LoRaRobotPacket_m.h"

#include "../helpers/generalHelpers.h"
#include "../helpers/CustomPacketQueue.h"
#include "../helpers/IncompletePacketList.h"
#include "../helpers/TimeOfLastTrajectory.h"
#include "../helpers/MissionIdTracker.h"

#include "../tags/MessageInfoTag_m.h"
#include "../tags/WaitTimeTag_m.h"
#include "../messages/NodeAnnounce_m.h"
#include "../messages/BroadcastHeader_m.h"
#include "../messages/BroadcastContinuousHeader_m.h"
#include "../messages/BroadcastLeaderFragment_m.h"
#include "../messages/BroadcastCTS_m.h"
#include "../messages/BroadcastFragment_m.h"


#endif /* COMMON_COMMON_H_ */
