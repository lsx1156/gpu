#include <stdio.h>
#include <math.h>
#include <string.h>

int main(void) {
    char buf[64];
    snprintf(buf, sizeof(buf), "hello from cross gcc 15.2.1");
    printf("%s, sqrt(2)=%.4f, glibc test ok\n", buf, sqrt(2.0));
    return 0;
}
