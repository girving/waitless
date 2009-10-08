#include <stdio.h>
#include <stdlib.h>

int main()
{
    FILE *f = fopen("read.c", "r");
    if (!f) {
        fprintf(stderr, "failed to open read.c\n");
        return 1;
    }
    fclose(f);
    return 0;
}
