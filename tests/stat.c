#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s <path>\n", argv[0]);
        return 1;
    }

    struct stat st;
    if (stat(argv[1], &st) < 0) {
        fprintf(stderr, "lstat failed: %s", strerror(errno));
        return 1;
    }

    printf("st_dev       %d\n", st.st_dev);
    printf("st_ino       %lld\n", st.st_ino);
    printf("st_mode      %d\n", st.st_mode);
    printf("st_nlink     %d\n", st.st_nlink);
    printf("st_uid       %d\n", st.st_uid);
    printf("st_gid       %d\n", st.st_gid);
    printf("st_rdev      %d\n", st.st_rdev);
    printf("st_atimespec %ld.%09ld\n", st.st_atimespec.tv_sec, st.st_atimespec.tv_nsec);
    printf("st_mtimespec %ld.%09ld\n", st.st_mtimespec.tv_sec, st.st_mtimespec.tv_nsec);
    printf("st_ctimespec %ld.%09ld\n", st.st_ctimespec.tv_sec, st.st_ctimespec.tv_nsec);
    printf("st_size      %lld\n", st.st_size);
    printf("st_blocks    %lld\n", st.st_blocks);
    printf("st_blksize   %d\n", st.st_blksize);
    printf("st_flags     %d\n", st.st_flags);
    printf("st_gen       %d\n", st.st_gen);
    return 0;
}
