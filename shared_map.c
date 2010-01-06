// A persistent, shared, cryptographic hash table

#include "shared_map.h"
#include "util.h"
#include "real_call.h"
#include <errno.h>

void shared_map_init(const struct shared_map *map, int fd)
{
    if (fd < 0)
        die("could not create shared map '%s'", map->name);

    struct stat st;
    if (real_fstat(fd, &st) < 0)
        die("fstat failed in shared_map_init: %s", strerror(errno));
    if (!st.st_size) {
        size_t default_size = map->default_count * (sizeof(struct hash) + map->value_size);
        if (ftruncate(fd, default_size) < 0)
            die("shared_map_init failed in ftruncate: %s", strerror(errno));
    }
    if (real_close(fd) < 0)
        die("shared_map_init close failed: %s", strerror(errno));
}

void shared_map_open(struct shared_map *map, const char *path)
{
    // Each entry is a (key,value) pair
    map->entry_size = sizeof(struct hash) + map->value_size;

    int fd = real_open(path, O_RDWR, 0);
    if (fd < 0)
        die("can't open shared map '%s'", path);
    struct stat st;
    if (real_fstat(fd, &st) < 0)
        die("fstat failed: %s", strerror(errno));
    if (!st.st_size)
        die("shared_map %s has zero size", path);
    map->count = st.st_size / map->entry_size;
    if (map->count * map->entry_size != st.st_size)
        die("shared map '%s' is corrupt: %ld is not a multiple of %ld", path, st.st_size, map->entry_size);
    map->addr = mmap(NULL, st.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (map->addr == MAP_FAILED)
        die("can't mmap shared map %s of size %ld: %s", path, (long)st.st_size, strerror(errno));

    map->lock_held = 0;
}

void shared_map_lock(struct shared_map *map)
{
    if (map->lock_held)
        die("called shared_map_lock with lock already held");
    map->lock_held = 1;
}

void shared_map_unlock(struct shared_map *map)
{
    if (!map->lock_held)
        die("called shared_map_unlock with no lock held");
    map->lock_held = 0;
}

struct entry {
    struct hash key;
    char value[0];
};

int shared_map_lookup(struct shared_map *map, const struct hash *key, void **value, int create)
{
    if (!map->lock_held)
        die("called shared_map_lookup without lock");
    if (!map->count)
        die("shared_map_lookup called before init");

    // Use the first few bytes of the hash key mod count as the index.  key is a
    // cryptographic hash, so the first few bytes are a good hash index.  We use
    // native little endian order for speed.
    // TODO: on big endian machines, we'll have to swap bytes here
    uint32_t index = *(uint32_t*)key % map->count;

    // Use linear chaining to find either key or the next free entry
    uint32_t count = 0;
    for (;;) {
        struct entry *entry = map->addr + map->entry_size * index;
        if (hash_is_null(&entry->key)) {
            if (create) {
                // TODO: keep track of filled entries and occasionally resize
                entry->key = *key;
                *value = entry->value;
            }
            return 0;
        }
        else if (hash_equal(&entry->key, key)) {
            *value = entry->value;
            return 1;
        }
        index = (index + 1) % map->count; 
        count++;
        if (++count == map->count)
            die("shared_map %s filled with %d entries", map->name, count);
    }
}

int shared_map_iter(struct shared_map *map, int (*f)(const struct hash *key, void *value))
{
    if (!map->lock_held)
        die("called shared_map_iter without lock");

    uint32_t index;
    for (index = 0; index < map->count; index++) {
        struct entry *entry = map->addr + map->entry_size * index;
        if (!hash_is_null(&entry->key)) {
            int r = f(&entry->key, entry->value);
            if (r)
                return r;
        }
    }

    return 0;
}
