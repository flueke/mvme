#ifndef __MVME_UTIL_ASSERT_H__
#define __MVME_UTIL_ASSERT_H__

#include <cassert>

#ifdef NDEBUG
    #define TRY_ASSERT(x) (x)
#else
    #define TRY_ASSERT(x) assert(x)
#endif

#define UNUSED_IF_ASSERT_DISABLED(x) ((void)(x))

#endif /* __MVME_UTIL_ASSERT_H__ */
