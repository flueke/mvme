#ifndef __A2_UTIL_ASSERT_H__
#define __A2_UTIL_ASSERT_H__

#include <cassert>

#define InvalidCodePath assert(!"invalid code path")
#define InvalidDefaultCase default: { assert(!"invalid default case"); }

#ifndef do_and_assert

#ifdef NDEBUG
    #define do_and_assert(x) (x)
#else
    #define do_and_assert(x) assert(x)
#endif

#endif // ifndef do_and_assert

#endif /* __A2_UTIL_ASSERT_H__ */
