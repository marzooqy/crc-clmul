#ifndef CRC_CLMUL_H
#define CRC_CLMUL_H

#include <stdbool.h>
#include <stdint.h>

#ifdef _MSC_VER
    #define DllExport __declspec(dllexport)
#else
    #define DllExport
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
} params_t;

/* Create a params_t struct and initialize it with the provided parameters.
   Calculate the values of k1, k2, and the table. */
params_t DllExport crc_params(uint8_t width, uint64_t poly, uint64_t init, bool refin, bool refout, uint64_t xorout);

/* Calculate a CRC using the table-based algorithm.
   Use params.init as the initial CRC value.*/
uint64_t DllExport crc_table(params_t *params, uint64_t crc, unsigned char const *buf, uint64_t len);

/* Calculate CRC using the SIMD algorithm. Uses the table-based algorithm if
   SIMD intrinsics are not available. Use params.init as the initial CRC value.*/
uint64_t DllExport crc_calc(params_t *params, uint64_t crc, unsigned char const *buf, uint64_t len);

#endif
