// Environment variables shared between waitless and libwaitless.so

#ifndef __env_h__
#define __env_h__

static const char WAITLESS_DIR[] = "WAITLESS_DIR";
static const char WAITLESS_SNAPSHOT[] = "WAITLESS_SNAPSHOT";
static const char WAITLESS_PROCESS[] = "WAITLESS_PROCESS";
static const char WAITLESS_VERBOSE[] = "WAITLESS_VERBOSE";

// TODO: this routine is extremely slow.  The most natural way to speed it up
// is probably to have a global "initialize" function that does the environment
// variable lookups once.
extern int is_verbose();

#endif
