#ifndef __A2_UTIL_ASSERT_H__
#define __A2_UTIL_ASSERT_H__

#include <cassert>

#define InvalidCodePath assert(!"invalid code path")
#define InvalidDefaultCase default: { assert(!"invalid default case"); }

#endif /* __A2_UTIL_ASSERT_H__ */
