import inet.common.TagBase;

cplusplus {{
using namespace inet;
}}

namespace rlora;

class WaitTimeTag extends inet::TagBase
{
    int waitTime;
}