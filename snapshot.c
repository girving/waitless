// The snapshot data structure

#include "snapshot.h"
#include "env.h"
#include "util.h"
#include "real_call.h"
#include "shared_map.h"
#include "inverse_map.h"
#include "stat_cache.h"
#include <errno.h>

// TODO: Rethink default counts and make them resizable
struct shared_map snapshot = { "snapshot.XXXXXXX", sizeof(struct snapshot_entry), 1<<15 };

// TODO: thread safety
void snapshot_init()
{
    static int initialized = 0;
    if (initialized)
        return;
    initialized = 1;

    const char *waitless_snapshot = getenv(WAITLESS_SNAPSHOT);
    if (!waitless_snapshot)
        die("WAITLESS_SNAPSHOT not set");
    shared_map_open(&snapshot, waitless_snapshot);
}

void make_fresh_snapshot()
{
    const char *waitless_dir = getenv(WAITLESS_DIR);
    if (!waitless_dir)
        die("WAITLESS_DIR not set");

    char snapshot_path[PATH_MAX];
    strcpy(snapshot_path, path_join(waitless_dir, "snapshot.XXXXXXX")); // TODO: one unnecessary copy
    shared_map_init(&snapshot, mkstemp(snapshot_path));
    setenv(WAITLESS_SNAPSHOT, snapshot_path, 1);
}

struct snapshot_entry *snapshot_update(struct hash *hash, const char *path, const struct hash *path_hash, int do_hash)
{
    // Hash the file's contents or existence
    stat_cache_update(hash, path, path_hash, do_hash);

    // Look up the path_hash in the snapshot to see if we know about the file
    snapshot_init();
    shared_map_lock(&snapshot);
    struct snapshot_entry *entry;
    if (!shared_map_lookup(&snapshot, path_hash, (void**)&entry, 1)) {
        // New entry: set hash
        entry->hash = *hash;
    }
    else if (!hash_equal(&entry->hash, hash)) {
        // TODO: once we're speculative, unravel process trees instead of dying
        if (hash_is_null(&entry->hash) != hash_is_null(hash))
            die("snapshot disagrees about existence of '%s'", path);
        if (hash_is_all_one(&entry->hash))
            entry->hash = *hash;
        else if (!hash_is_all_one(hash))
            die("snapshot contains a different version of '%s'", path);
    }

    // Note: The snapshot is left unlocked so that the caller can further read
    // or modify the entry.
    return entry;
}

static int dump_fd;

static int dump_helper(const struct hash *name, void *value)
{
    struct snapshot_entry *entry = value;
    char buffer[1024], *p = buffer;
#define SPACE() (buffer+sizeof(buffer)-p-1) // -1 is for the newline
    p += strlcpy(p, "  ", SPACE());
    p += inverse_hash_string(name, p, SPACE());
    p += strlcpy(p, ": ", SPACE());
    p = show_hash(p, min(8, SPACE()), &entry->hash);
    if (entry->writing)
        p += strlcpy(p, ", writing", SPACE());
    if (entry->written)
        p += strlcpy(p, ", written", SPACE());
    if (entry->stat)
        p += strlcpy(p, ", stat", SPACE());
    if (entry->read)
        p += strlcpy(p, ", read", SPACE());
    *p++ = '\n';
    *p = 0;

    write_str(dump_fd, buffer);
    return 0;
}

void snapshot_dump()
{
    snapshot_init();

    shared_map_lock(&snapshot);
    write_str(STDOUT_FILENO, "snapshot dump:\n");
    int fds[2];
    real_pipe(fds);
    pid_t pid = real_fork();
    if (pid < 0)
        die("fork failed: %s", strerror(errno));
    else if (!pid) { // child
        real_close(fds[1]);
        real_dup2(fds[0], 0);
        const char *const argv[2] = { "sort", 0 };
        real_execve("/usr/bin/sort", argv, argv+1);
        die("exec sort failed: %s", strerror(errno));
    }
    real_close(fds[0]);
    dump_fd = fds[1];
    shared_map_iter(&snapshot, dump_helper);
    real_close(dump_fd);
    real_waitpid(pid, 0, 0);
    shared_map_unlock(&snapshot);
}

static int verify_helper(const struct hash *path_hash, void *value)
{
    struct snapshot_entry *entry = value;
    if (entry->writing)
        return 0;
    char path[PATH_MAX];
    inverse_hash_string(path_hash, path, sizeof(path));
    int do_hash = !(hash_is_null(&entry->hash) || hash_is_all_one(&entry->hash));
    struct hash hash;
    stat_cache_update(&hash, path, path_hash, do_hash);
    if (!hash_equal(&hash, &entry->hash)) {
        char sh[8], fh[8];
        show_hash(fh, 8, &hash);
        show_hash(sh, 8, &entry->hash);
        fdprintf(STDERR_FILENO, "warning: snapshot mismatch for %s: snapshot says %s, file says %s\n", path, sh, fh);
    }
    return 0;
}

void snapshot_verify()
{
    snapshot_init();
    shared_map_lock(&snapshot);
    shared_map_iter(&snapshot, verify_helper);
    shared_map_unlock(&snapshot);
}
