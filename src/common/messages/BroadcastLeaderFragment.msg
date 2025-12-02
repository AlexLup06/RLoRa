import inet.common.packet.chunk.Chunk; 
import inet.common.Units;

cplusplus {{
using namespace inet;
}}

namespace rlora;

class BroadcastLeaderFragment extends inet::FieldsChunk {
    int source;
    int hop;
    int messageId;
    int size;
    int payloadSize;
    int missionId;
    bool isMission;
}
