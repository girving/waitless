// The snapshot data structure

#ifndef __snapshot_h__
#define __snapshot_h__

#include "hash.h"

/*
 * The snapshot contains information about what files we consider "current".
 * Snapshot information is unique to the current invocation of waitless.
 */

struct snapshot_entry
{
    // What's being/been done to this file
    unsigned stat : 1;
    unsigned read : 1;
    unsigned written : 1;
    unsigned writing : 1;

    // The contents hash that we consider current.  All zeroes mean the file
    // doesn't exist.  All ones mean the file does exist but we haven't nailed
    // down its contents yet.
    struct hash hash;
};

/*
 * Make a fresh snapshot file and store its path in WAITLESS_SNAPSHOT.
 *
 * TODO: Currently this file needs to be manually unlinked by waitless
 * after the execution of the process.  This could be fixed by threading
 * it through one of the available file descriptors to all downstream
 * processes.
 */
extern void make_fresh_snapshot();

// Each process must call this before other snapshot usage
extern void snapshot_init();

extern struct shared_map snapshot;

/*
 * Add a file to the snapshot, hashing its contents if desired, and return
 * a pointer to the entry.  Important: snapshot_add locks the snapshot, but it
 * it is the caller's responsibility to unlock it.
 *
 * If do_hash is 0, only the existence or nonexistence is recorded and hash is
 * set to all zeroes or all ones accordingly.
 */
extern struct snapshot_entry *snapshot_update(struct hash *hash, const char *path, const struct hash *path_hash, int do_hash);

extern void snapshot_dump();

extern void snapshot_verify();

#endif
