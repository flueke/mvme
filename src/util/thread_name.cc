#include "thread_name.h"
#ifdef __linux__
#include <sys/prctl.h>
#endif

namespace mesytec::util
{

#ifdef __linux__
void set_thread_name(const char *name)
{
    prctl(PR_SET_NAME,name,0,0,0);
}

#else

void set_thread_name(const char *)
{
}

#endif
}
