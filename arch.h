// Architecture specific types

#ifndef __arch_h__
#define __arch_h__

/*
 * These includes are chosen to avoid pulling in any function signatures.
 * All we want is data types and macros.
 *
 * Other files should include arch.h if possible rather than the system
 * header in order to reduce the chance of leakage.
 */

#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>
#include <sys/syslimits.h>

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

// Pull in WIFEXITED, etc. without pulling in system call signatures
#include "hacked-wait.h"

// The 64-bit versions of stat/fstat/lstat have weird suffixes on Mac for
// backwards compatibility reasons.
#ifdef __APPLE__
#define STAT_NAME(name) #name __DARWIN_SUF_64_BIT_INO_T
#define STAT_ALIAS(name) __asm("_" STAT_NAME(name))
#else
#define STAT_NAME(name) #name
#define STAT_ALIAS(name)
#endif

#endif
