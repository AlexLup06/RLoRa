import inet.common.packet.chunk.Chunk; 

namespace rlora;

class NodeAnnounce extends inet::FieldsChunk {
    int nodeId;
    int lastHop;
    int respond;
}
