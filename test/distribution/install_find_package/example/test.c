#include <stdio.h>
#include <nn/endian.h>

int main() {
    uint16_t foo = 42;
    uint16_t bar = nn_htons(foo);
    printf("%i converted from host byte order to network byte order is %i\n", foo, bar);
}
