// Analog of sha1sum for Skein

#include "util.h"
#include "hash.h"
#include "real_call.h"
#include <errno.h>

int main(int argc, char **argv)
{
    if (argc < 2 || argv[1][0] == '-') {
        write_str(STDERR_FILENO, "usage: skein <file>...\n");
        real__exit(1);
    }

    int i;
    for (i = 1; i < argc; i++) {
        int fd = real_open(argv[i], O_RDONLY, 0);
        if (fd < 0)
            die("can't open %s: %s", argv[i], strerror(errno));
        struct hash hash;
        hash_fd(&hash, fd);
        real_close(fd);
        char buffer[1024], *p = buffer;
        p = show_hash(p, sizeof(buffer), &hash); 
        *p++ = ' ';
        *p++ = ' ';
        p += strlcpy(p, argv[i], buffer+sizeof(buffer)-p-1);
        *p++ = '\n';
        write(STDERR_FILENO, buffer, p-buffer);
    }
    return 0;
}
