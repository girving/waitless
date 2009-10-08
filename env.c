// Environment variables shared between waitless and libwaitless.so

#include "env.h"
#include "real_call.h"

// TODO: this routine is extremely slow.  The most natural way to speed it up
// is probably to have a global "initialize" function that does the environment
// variable lookups once.
int is_verbose()
{
    return getenv(WAITLESS_VERBOSE) != 0;
}
