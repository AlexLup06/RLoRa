import inet.common.packet.chunk.Chunk; 

namespace rlora;

class BroadcastFragment extends inet::FieldsChunk {
    int messageId;
    int fragmentId;
    int source;
	int payloadSize;
	int missionId;
}
