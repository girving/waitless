// Search PATH for executable files

#include "search_path.h"
#include "real_call.h"
#include "util.h"
#include <string.h>
#include <errno.h>

// We call stat directly instead of real_stat to trigger action logic
extern int stat(const char *path, struct stat *buf) STAT_ALIAS(stat);

const char *search_path(char buffer[PATH_MAX], const char *file, const char *PATH)
{
    // Skip the search if file contains a slash
    if (strchr(file, '/'))
        return file;

    // Lookup PATH if necessary
    if (!PATH) {
        PATH = getenv("PATH");
        if (!PATH)
            die("search_path: PATH not set");
    }

    // TODO: The following code treats empty entries in PATH as if they weren't
    // there.  That's wrong if empty entries are supposed to mean ".".

    // Check each component of PATH in order
    size_t nf = strlen(file);
    while (*PATH) {
        const char *p = strchr(PATH, ':');
        int np = p ? p - PATH : strlen(PATH);
        if (np + 2 + nf > PATH_MAX)
            die("execvP: buffer space exceeded");
        memcpy(buffer, PATH, np);
        buffer[np] = '/';
        memcpy(buffer+np+1, file, nf+1);

        // TODO: The real execve is more permissive (it'll keep searching on
        // a large class of execve errors).
        struct stat st;
        if (stat(buffer, &st) < 0) {
            if (errno != ENOENT)
                die("execvP: stat '%s' failed: %s", buffer, strerror(errno));
        }
        else if (st.st_mode & S_IXUSR) {
            // Found an executable file!
            return buffer;
        }

        // Advance to the next entry
        if (!p)
            break;
        PATH += np+1;
    }

    // Failed
    errno = ENOENT;
    return 0;
}
