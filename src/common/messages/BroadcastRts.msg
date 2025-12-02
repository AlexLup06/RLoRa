import inet.common.packet.chunk.Chunk; 
import inet.common.Units;

cplusplus {{
using namespace inet;
}}

namespace rlora;

class BroadcastRts extends inet::FieldsChunk {
    int source;
    int hop;
    int messageId;
    int missionId;
    int size;
    bool isMission;
}
