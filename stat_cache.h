// The stat cache data structure

#ifndef __stat_cache_h__
#define __stat_cache_h__

#include "hash.h"

/*
 * The stat cache is a map from hash(filename) to hash(contents) the last time
 * we checked, plus lstat information in order to check whether the file might
 * have changed.
 */

// Initialize the stat_cache if it does not already exist.
extern void stat_cache_init();

// Update the entry for one file.  If do_hash is 0, the returned hash will be
// all zero or all one depending on whether the file exists.
extern void stat_cache_update(struct hash *hash, const char *path, const struct hash *path_hash, int do_hash);

// Update the entry for a file based on an open file descriptor.
extern void stat_cache_update_fd(struct hash *hash, int fd, const struct hash *path_hash);

#endif
