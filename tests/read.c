#include <stdio.h>

int main()
{
    FILE *f = fopen("read.c", "r");
    fclose(f);
    return 0;
}
