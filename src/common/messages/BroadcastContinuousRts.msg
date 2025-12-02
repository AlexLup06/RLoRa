import inet.common.packet.chunk.Chunk; 
import inet.common.Units;

cplusplus {{
using namespace inet;
}}

namespace rlora;

class BroadcastContinuousRts extends inet::FieldsChunk {
    int messageId;
    int payloadSizeOfNextFragment;
    int missionId;
    int source;
    int hopId;
}

