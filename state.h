// Shared state for waitless

#ifndef __state_h__
#define __state_h__

/*
 * The major data structures in waitless are
 *
 * 1. Subgraph: maps from node names in the universal computation graph to
 *    their outgoing edges.  The universal computation graph is assumed to
 *    be a global constant, so all we're doing is exploring a piece of
 *    it with the subgraph.
 *
 * 2. Snapshot: information about what files we consider "current".  Snapshot
 *    information is unique to the current invocation of waitless.
 *
 * 3. Stat cache: map from hash(filename) to hash(contents) the last time we
 *    checked, plus lstat information in order to check whether the file
 *    might have changed.
 */

#include "shared_map.h"

extern struct shared_map subgraph;
extern struct shared_map snapshot;
extern struct shared_map stat_cache;

extern void initialize();

#endif
