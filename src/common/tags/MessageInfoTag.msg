import inet.common.TagBase;

cplusplus {{
using namespace inet;
}}

namespace rlora;

class MessageInfoTag extends inet::TagBase
{
    bool isNeighbourMsg;
    bool isHeader;
    bool hasRegularHeader=true;
    bool isNodeAnnounce=false;
    bool hasUsefulData=false;
    bool withRTS=true;
    int missionId=-1;
    int messageId=-1;
    int hopId=-1;
    int payloadSize=0;
    int tries=0;
}