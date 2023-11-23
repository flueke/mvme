#ifndef B18E3651_CA9A_43BC_AA25_810EA16533CD
#define B18E3651_CA9A_43BC_AA25_810EA16533CD

#include <nng/nng.h>
#include <nng/protocol/pipeline0/push.h>
#include <nng/protocol/pipeline0/pull.h>
#include <nng/protocol/pair0/pair.h>

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

#endif /* B18E3651_CA9A_43BC_AA25_810EA16533CD */
