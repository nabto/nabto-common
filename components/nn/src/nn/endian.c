#include <nn/endian.h>

uint16_t nn_ntohs(uint16_t in)
{
    uint8_t* ptr = (uint8_t*)(&in);
    uint16_t val =
        ((uint16_t)ptr[0]) << 8 |
        ((uint16_t)ptr[1]);
    return val;
}

uint16_t nn_htons(uint16_t in)
{
    return nn_ntohs(in);
}

uint32_t nn_ntohl(uint32_t in)
{
    uint8_t* ptr = (uint8_t*)(&in);
    uint32_t val =
        ((uint32_t)ptr[0]) << 24 |
        ((uint32_t)ptr[1]) << 16 |
        ((uint32_t)ptr[2]) << 8 |
        ((uint32_t)ptr[3]);
    return val;
}

uint32_t nn_htonl(uint32_t in)
{
    return nn_ntohl(in);
}

#if defined(UINT64_MAX)
uint64_t nn_htonll(uint64_t in)
{
    uint8_t* ptr = (uint8_t*)(&in);
    uint64_t val =
        ((uint64_t)ptr[0]) << 56 |
        ((uint64_t)ptr[1]) << 48 |
        ((uint64_t)ptr[2]) << 40 |
        ((uint64_t)ptr[3]) << 32 |
        ((uint64_t)ptr[4]) << 24 |
        ((uint64_t)ptr[5]) << 16 |
        ((uint64_t)ptr[6]) << 8 |
        ((uint64_t)ptr[7]);
    return val;

}

uint64_t nn_ntohll(uint64_t in)
{
    return nn_htonll(in);
}
#endif
