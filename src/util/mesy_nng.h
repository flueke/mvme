#ifndef B18E3651_CA9A_43BC_AA25_810EA16533CD
#define B18E3651_CA9A_43BC_AA25_810EA16533CD

#include <nng/nng.h>
#include <nng/protocol/pair0/pair.h>
#include <nng/protocol/pipeline0/pull.h>
#include <nng/protocol/pipeline0/push.h>
#include <nng/protocol/pubsub0/pub.h>
#include <nng/protocol/pubsub0/sub.h>

inline nng_msg *alloc_message(size_t size)
{
    nng_msg *msg = {};
    if (int res = nng_msg_alloc(&msg, size))
        return nullptr;
    return msg;
}

inline int receive_message(nng_socket sock, nng_msg **msg_ptr, int flags = 0)
{
    if (auto res = nng_recvmsg(sock, msg_ptr, flags))
    {
        nng_msg_free(*msg_ptr);
        *msg_ptr = NULL;
        return res;
    }

    return 0;
}

inline int allocate_reserve_message(nng_msg **msg, size_t reserve = 0)
{
    assert(msg);

    if (auto res = nng_msg_alloc(msg, 0))
        return res;

    if (auto res = nng_msg_reserve(*msg, reserve))
        return res;

    return 0;
}

inline size_t allocated_free_space(nng_msg *msg)
{
    auto capacity = nng_msg_capacity(msg);
    auto used = nng_msg_len(msg);
    assert(capacity >= used);
    return capacity - used;
}


#endif /* B18E3651_CA9A_43BC_AA25_810EA16533CD */
