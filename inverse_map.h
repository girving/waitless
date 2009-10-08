// An inverse map from hashes to preimages

#ifndef __inverse_map_h__
#define __inverse_map_h__

#include "hash.h"

/*
 * The inverse map stores the preimages of hash values, similar to the objects
 * directory under .git.  For now it is only used to store file paths, but in
 * future it could be extended to store cached versions of files.
 *
 * Note: The current implementation is simple but slow.
 */

#include "hash.h"

// Hash a block of memory and remember the contents
extern void remember_hash_memory(struct hash *hash, const void *p, size_t n);

// Hash a string and remember the contents
extern void remember_hash_string(struct hash *hash, const char *s);

// Hash and remember a path (converting from relative to absolute if necessary)
extern void remember_hash_path(struct hash *hash, const char *path);

// Grab up to n bytes of a hash preimage.  Returns the amount grabbed.
// This function is ideal only for logging purposes since it provides no
// efficient way to get the rest of the preimage.
extern int inverse_hash_memory(const struct hash *hash, void *p, size_t n);

// Same as inverse_hash_memory, but adds a trailing null.
extern int inverse_hash_string(const struct hash *hash, char *s, size_t n);

#endif
