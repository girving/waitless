// Architecture specific stuff

#ifndef __arch_h__
#define __arch_h__

/*
 * All of the types declared here can be found in standard header files.
 * However, these standard headers declare a slew of things we don't want.
 * It's much better to duplicate a handful of lines of code here than to
 * sacrifice compiler checking of name usage.
 */

#include <stdint.h>
#include <sys/types.h>
#include <sys/syslimits.h>

// TODO: This assumes sizeof(long) == sizeof(void*).
typedef unsigned long size_t;
typedef long ssize_t;

#ifndef NULL
#define NULL ((void *)0)
#endif

// Pull in struct stat and surrounding defines without pulling in the system
// call signatures.
#if defined(__APPLE__)
    // Macs don't have bits/stat.h, but they do have tempting
    // __BEGIN_DECLS / __END_DECLS blocks around exactly the
    // section of code we don't want.  sed to the rescue...
#   include "hacked-stat.h"
#else
#   define _SYS_STAT_H // lie
#   include <bits/stat.h>
#   undef _SYS_STAT_H // unlie
#endif

#endif
