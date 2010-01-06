// The stat cache data structure

#include "stat_cache.h"
#include "env.h"
#include "util.h"
#include "real_call.h"
#include "shared_map.h"
#include "errno.h"

struct stat_cache_entry
{
    // Last known stat information
    ino_t st_ino;                 // inode number
    struct timespec st_mtimespec; // last modification time
    off_t st_size;                // file size

    // Last known hash of the file's contents
    struct hash contents_hash;
};

// TODO: Rethink default counts and make them resizable
static struct shared_map stat_cache = { "stat_cache", sizeof(struct stat_cache_entry), 1<<15 };

static const char *stat_cache_path()
{
    const char *waitless_dir = getenv(WAITLESS_DIR);
    if (!waitless_dir)
        die("WAITLESS_DIR not set");
    return path_join(waitless_dir, stat_cache.name);
}

void stat_cache_init()
{
    shared_map_init(&stat_cache, real_open(stat_cache_path(), O_CREAT | O_WRONLY, 0644));
}

// TODO: thread safety
static void initialize()
{
    static int initialized = 0;
    if (initialized)
        return;
    initialized = 1;

    const char *waitless_dir = getenv(WAITLESS_DIR);
    if (!waitless_dir)
        die("WAITLESS_DIR not set");
    shared_map_open(&stat_cache, stat_cache_path());
}

void stat_cache_update(struct hash *hash, const char *path, const struct hash *path_hash, int do_hash)
{
    initialize();

    // lstat the file
    struct stat st;
    if (real_lstat(path, &st) < 0) {
        int errno_ = errno;
        if (errno_ == ENOENT || errno_ == ENOTDIR) {
            // Set hash to zero to represent nonexistent file
            memset(hash, 0, sizeof(struct hash));
            return;
        }
        die("lstat(\"%s\") failed: %s", path, strerror(errno_));
    }

    // TODO: We currently ignore the S_ISLNK flag, which assumes that traced
    // processes never detect symlinks via lstat and never create them.

    // For now we go the simple route and hold the stat_cache lock for the
    // entire duration of the hash computation.  In future we may want to drop
    // the lock while we compute the hash.  Alternatively, switching to a finer
    // grain locking discipline might avoid the problem.
    shared_map_lock(&stat_cache);

    // Lookup entry, creating it if necessary, and check if it's up to date
    struct stat_cache_entry *entry;
    if (!shared_map_lookup(&stat_cache, path_hash, (void**)&entry, 1)
        || entry->st_mtimespec.tv_nsec != st.st_mtimespec.tv_nsec
        || entry->st_mtimespec.tv_sec != st.st_mtimespec.tv_sec
        || entry->st_size != st.st_size
        || entry->st_ino != st.st_ino
        || (do_hash && hash_is_all_one(&entry->contents_hash)))
    {
        // Entry is new or out of date.  In either case, compute hash and
        // record new stat details.
        entry->st_ino = st.st_ino;
        entry->st_mtimespec = st.st_mtimespec;
        entry->st_size = st.st_size;

        if (do_hash) {
            // Hash the file
            int fd = real_open(path, O_RDONLY, 0);
            if (fd < 0)
                die("can't open '%s' to compute hash", path);
            hash_fd(&entry->contents_hash, fd);
            real_close(fd);
        }
        else
            memset(&entry->contents_hash, -1, sizeof(struct hash));
    }
    shared_map_unlock(&stat_cache);
    if (do_hash)
        *hash = entry->contents_hash;
    else
        memset(hash, -1, sizeof(struct hash));
}

void stat_cache_update_fd(struct hash *hash, int fd, const struct hash *path_hash)
{
    initialize();

    // lstat the file
    struct stat st;
    if (real_fstat(fd, &st) < 0)
        die("fstat(%d) failed: %s", fd, strerror(errno));

    // For now we go the simple route and hold the stat_cache lock for the
    // entire duration of the hash computation.  In future we may want to drop
    // the lock while we compute the hash.  Alternatively, switching to a finer
    // grain locking discipline might avoid the problem.
    shared_map_lock(&stat_cache);

    // Lookup entry, creating it if necessary, and check if it's up to date
    struct stat_cache_entry *entry;
    if (!shared_map_lookup(&stat_cache, path_hash, (void**)&entry, 1)
        || entry->st_mtimespec.tv_nsec != st.st_mtimespec.tv_nsec
        || entry->st_mtimespec.tv_sec != st.st_mtimespec.tv_sec
        || entry->st_size != st.st_size
        || entry->st_ino != st.st_ino
        || hash_is_all_one(&entry->contents_hash))
    {
        // Entry is new or out of date.  In either case, compute hash and
        // record new stat details.
        entry->st_ino = st.st_ino;
        entry->st_mtimespec = st.st_mtimespec;
        entry->st_size = st.st_size;

        // Hash the file
        if (lseek(fd, 0, SEEK_SET) < 0)
            die("lseek failed: %s", strerror(errno));
        hash_fd(&entry->contents_hash, fd);
    }
    shared_map_unlock(&stat_cache);
    *hash = entry->contents_hash;
}
