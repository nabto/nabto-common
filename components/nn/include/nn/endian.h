#ifndef _NN_ENDIAN_H_
#define _NN_ENDIAN_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint16_t nn_ntohs(uint16_t in);
uint16_t nn_htons(uint16_t in);
uint32_t nn_ntohl(uint32_t in);
uint32_t nn_htonl(uint32_t in);

#if defined(UINT64_MAX)
uint64_t nn_htonll(uint64_t in);
uint64_t nn_ntohll(uint64_t in);
#endif

#ifdef __cplusplus
} //extern "C"
#endif

#endif
