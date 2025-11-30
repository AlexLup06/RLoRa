import inet.common.packet.chunk.Chunk; 

namespace rlora;

class BroadcastCTS extends inet::FieldsChunk {
    int sizeOfFragment;
    int hopId;
}