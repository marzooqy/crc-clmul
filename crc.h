#ifndef CRC_CLMUL_H
#define CRC_CLMUL_H

#include <stdbool.h>
#include <stdint.h>

#ifdef _MSC_VER
#define DLL_EXPORT __declspec(dllexport)
#else
#define DLL_EXPORT
#endif

/* Holds frequently used CRC parameters. */
typedef struct {
    uint8_t width;
    uint64_t poly;
    bool refin;
    bool refout;
    uint64_t init;
    uint64_t xorout;
    uint64_t k1;
    uint64_t k2;
    uint64_t table[256];
    uint64_t combine_table[64];
} params_t;

/* Create a params_t struct and initialize it with the provided parameters.
   Calculate the values of k1, k2, and the table. Check the input if CHECK_PARAMS
   is defined and crash on error. check is only used if CHECK_PARAMS is defined.*/
params_t DLL_EXPORT crc_params(uint8_t width, uint64_t poly, uint64_t init, bool refin, bool refout, uint64_t xorout, uint64_t check);

/* Calculate the CRC using the table-based algorithm.
   Use params.init as the initial CRC value.*/
uint64_t DLL_EXPORT crc_table(params_t *params, uint64_t crc, unsigned char const *buf, uint64_t len);

/* Calculate the CRC using the SIMD algorithm. Falls back to the table-based
   algorithm if SIMD intrinsics are not available. Use params.init as the
   initial CRC value.*/
uint64_t DLL_EXPORT crc_calc(params_t *params, uint64_t crc, unsigned char const *buf, uint64_t len);

/* Combine two CRCs. len is the length of the second CRC. */
uint64_t DLL_EXPORT crc_combine(params_t *params, uint64_t crc, uint64_t crc2, uint64_t len);

#endif
