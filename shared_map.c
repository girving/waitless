// A persistent, shared, cryptographic hash table

#include "shared_map.h"
#include "util.h"
#include "real_call.h"

void shared_map_init(const struct shared_map *map, int fd)
{
    if (fd < 0)
        die("could not create shared map '%s'", map->name);

    struct stat st;
    real_fstat(fd, &st);
    if (!st.st_size) {
        // Seek way out and write a single zero byte to grow the file
        lseek(fd, map->default_count * (sizeof(struct hash) + map->value_size) - 1, SEEK_SET);
        char zero = 0;
        write(fd, &zero, 1);
    }
}

void shared_map_open(struct shared_map *map, const char *path)
{
    // Each entry is a (key,value) pair
    map->entry_size = sizeof(struct hash) + map->value_size;

    int fd = real_open(path, O_RDWR, 0);
    if (fd < 0)
        die("can't open shared map '%s'", path);
    struct stat st;
    real_fstat(fd, &st);
    map->addr = mmap(NULL, st.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (map->addr == MAP_FAILED)
        die("can't mmap shared map '%s'", path);
    map->count = st.st_size / map->entry_size;
    if (map->count * map->entry_size != st.st_size)
        die("shared map '%s' is corrupt: %ld is not a multiple of %ld", path, st.st_size, map->entry_size);

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
    if (map->lock_held)
        die("called shared_map_unlock with no lock held");
    map->lock_held = 0;
}

struct entry {
    struct hash key;
    char value[0];
};

int shared_map_lookup(struct shared_map *map, const struct hash *key, void **value, int create)
{
    if (map->lock_held)
        die("called shared_map_lookup without lock");

    // Use the first few bytes of the hash key mod count as the index.  key is a
    // cryptographic hash, so the first few bytes are a good hash index.  We use
    // native little endian order for speed.
    // TODO: on big endian machines, we'll have to swap bytes here
    uint32_t index = *(uint32_t*)key % map->count;

    // Use linear chaining to find either key or the next free entry
    for (;;) {
        struct entry *entry = map->addr + map->entry_size * index;
        if (hash_null(&entry->key)) {
            if (create) {
                // TODO: keep track of filled entries and occasionally resize
                entry->key = *key;
            }
            return 0;
        }
        else if (hash_equal(&entry->key, key)) {
            *value = &entry->value;
            return 1;
        }
        index = (index + 1) % map->count; 
    }
}
