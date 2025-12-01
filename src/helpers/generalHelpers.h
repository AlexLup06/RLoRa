#ifndef HELPERS_GENERALHELPERS_H_
#define HELPERS_GENERALHELPERS_H_

namespace rlora
{

    inline int predictSendTime(int size)
    {
        if (size > 255)
        {
            return 255 + 20;
        }
        return size + 20;
    }

#define NODE_ANNOUNCE_SIZE 3
#define BROADCAST_CTS 4
#define BROADCAST_HEADER_SIZE 8
#define BROADCAST_FRAGMENT_META_SIZE 5
#define BROADCAST_CONTINIOUS_HEADER 4
#define BROADCAST_LEADER_FRAGMENT_META_SIZE 7
}

#endif
