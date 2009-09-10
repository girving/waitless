// A persistent, shared, cryptographic hash table

#ifndef __shared_map_h__
#define __shared_map_h__

#include "hash.h"

// A shared map maps a hash value to a fixed size data structure.
// Each map is stored in $WAITLESS_DIR/$NAME and is shared between
// all waitless children processes via mmap.  The memory layout of
// the file is
//
//  struct shared_map_file
//  {
//      int lock; // TODO:

// TODO: I'm currently assuming that munmap is unnecessary since it happens
// automatically on exit.

struct shared_map
{
    // Static header information
    char name[20];
    uint32_t value_size;
    uint32_t default_count;

    // Computed header information (a function of the above)
    uint32_t entry_size;

    // Dynamic information (size, mmap address, etc.)
    uint32_t count; // number of entries in the hash table (filled or unfilled)
    void *addr;
    char lock_held; // for assertions only
};

// Initialize the shared map pointed to by fd if it doesn't already exist.
// fd is checked for validity and closed upon success.
extern void shared_map_init(const struct shared_map *map, int fd);

// Map an existing shared map into our addresses space.
extern void shared_map_open(struct shared_map *map, const char *path);

// Lock or unlock a shared map.  For now, this will be rather slow since there's
// only one lock per map, but that will be easy to fix later.
//
// TODO: Implement these, and add reader/writer locking if we think it's useful.
extern void shared_map_lock(struct shared_map *map);
extern void shared_map_unlock(struct shared_map *map);

// Look up a key in a shared_map.  If the entry doesn't exist, shared_map_lookup
// returns false and wll create the entry iff create.  If the entry exists or
// was created, value is updated to point to it.  The value is mutable.
extern int shared_map_lookup(struct shared_map *map, const struct hash *key, void **value, int create);

#endif
