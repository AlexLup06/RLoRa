import inet.common.packet.chunk.Chunk; 

namespace rlora;

class LoRaOptions {
    double LoRaTP = -1;
    double LoRaCF = -1;
    int LoRaSF = -1;
    double LoRaBW = -1;
    int LoRaCR = -1;
    bool UseHeader = true;
    bool ADRACKReq = false;
}

class LoRaRobotPacket extends inet::FieldsChunk {
    LoRaOptions options;
    bool isMission;
}
