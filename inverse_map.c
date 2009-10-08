// An inverse map from hashes to preimages

#include "inverse_map.h"
#include "real_call.h"
#include "env.h"
#include "util.h"
#include <errno.h>

static const char INVERSE[] = "/inverse/";

void remember_hash_memory(struct hash *hash, const void *p, size_t n)
{
    hash_memory(hash, p, n);

    const char *waitless_dir = getenv(WAITLESS_DIR);
    if (!waitless_dir)
        die("WAITLESS_DIR not set");
    int dn = strlen(waitless_dir), in = strlen(INVERSE);
    int total = dn + in + 3 + SHOW_HASH_SIZE;
    if (total > PATH_MAX)
        die("WAITLESS_DIR is too long: %d > %d", dn, PATH_MAX - total + dn);

    // path = "waitless_dir/inverse/hash[0:2]/hash"
    char path[total];
    memcpy(path, waitless_dir, dn);
    memcpy(path+dn, INVERSE, in);
    show_hash(path+dn+in+3, SHOW_HASH_SIZE, hash);
    memcpy(path+dn+in, path+dn+in+3, 2);
    path[dn+in+2] = '/';

retry:;

    // Try to open path for exclusive create
    int fd = real_open(path, O_WRONLY | O_CREAT | O_EXCL, 0644);
    if (fd < 0) {
        // Failed: either file exists (great!) or we need to make directories
        int errno_ = errno;
        if (errno_ == EEXIST) {
            // If the file exists, we assume it already has the desired contents
            // (go cryptographic hashing).
            return;
        }
        else if (errno_ == ENOENT) {
            // Create the necessary directory components
            path[dn+in-1] = 0;
            if (mkdir(path, 0755) < 0 && errno != EEXIST)
                die("mkdir(\"%s\") failed: %s", path, strerror(errno));
            path[dn+in-1] = '/';
            path[dn+in+2] = 0;
            if (mkdir(path, 0755) < 0 && errno != EEXIST)
                die("mkdir(\"%s\") failed: %s", path, strerror(errno));
            path[dn+in+2] = '/';

            // Try to open again.  Note that it's possible to hit EEXIST on
            // retry if another thread is trying to remember the same hash.
            goto retry;
        }
        die("failed to create %s: %s", path, strerror(errno_));
    }

    if (write(fd, p, n) != n)
        die("remember_hash_memory: write failed: %s", strerror(errno));
    if (real_close(fd) < 0)
        die("remember_hash_memory: close failed: %s", strerror(errno));
}

void remember_hash_string(struct hash *hash, const char *s)
{
    remember_hash_memory(hash, s, strlen(s));
}

// TODO: calling getcwd() every time might be slow.  This could be sped up by
// caching the current directory and intercepting chdir calls.
void remember_hash_path(struct hash *hash, const char *path)
{
    char cwd[PATH_MAX];
    if (!real_getcwd(cwd, PATH_MAX))
        die("remember_hash_path: getcwd failed: %s", strerror(errno));
    remember_hash_string(hash, path_join(cwd, path));
}

int inverse_hash_memory(const struct hash *hash, void *p, size_t n)
{
    const char *waitless_dir = getenv(WAITLESS_DIR);
    if (!waitless_dir)
        die("WAITLESS_DIR not set");
    int dn = strlen(waitless_dir), in = strlen(INVERSE);
    int total = dn + in + 3 + SHOW_HASH_SIZE;
    if (total > PATH_MAX)
        die("WAITLESS_DIR is too long: %d > %d", dn, PATH_MAX - total + dn);

    // path = "waitless_dir/inverse/hash[0:2]/hash"
    char path[total];
    memcpy(path, waitless_dir, dn);
    memcpy(path+dn, INVERSE, in);
    show_hash(path+dn+in+3, SHOW_HASH_SIZE, hash);
    memcpy(path+dn+in, path+dn+in+3, 2);
    path[dn+in+2] = '/';

    // Read path
    // TODO: do enough file locking to prevent reading a partially written file
    int fd = real_open(path, O_RDONLY, 0);
    if (fd < 0)
        die("inverse_hash_memory: failed to open %s: %s", path, strerror(errno));
    size_t r = read(fd, p, n);
    if (r < 0)
        die("inverse_hash_memory: read failed: %s", strerror(errno));
    real_close(fd);
    return r;
}

int inverse_hash_string(const struct hash *hash, char *s, size_t n)
{
    int r = inverse_hash_memory(hash, s, n-1);
    s[r] = 0;
    return r;
}
