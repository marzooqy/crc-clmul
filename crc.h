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
   Calculate the values of k1, k2, and the table. */
params_t DLL_EXPORT crc_params(uint8_t width, uint64_t poly, uint64_t init, bool refin, bool refout, uint64_t xorout, uint64_t check, uint8_t *error);

/* List of crc_params errors */
enum DLL_EXPORT crc_params_errors {
    CRC_WIDTH_NOT_SUPPORTED = 1,
    CRC_POLY_BIG = 2,
    CRC_INIT_BIG = 3,
    CRC_XOROUT_BIG = 4,
    CRC_CHECK_INVALID = 5
};

/* Print an error message depending on the error received from crc_params. */
void DLL_EXPORT crc_print_error(uint8_t error);

/* Calculate the CRC using the table-based algorithm.
   Use params.init as the initial CRC value.*/
uint64_t DLL_EXPORT crc_table(params_t *params, uint64_t crc, unsigned char const *buf, uint64_t len);

/* Calculate the CRC using the SIMD algorithm. Falls back to the table-based
   algorithm if SIMD intrinsics are not available. Use params.init as the
   initial CRC value.*/
uint64_t DLL_EXPORT crc_calc(params_t *params, uint64_t crc, unsigned char const *buf, uint64_t len);

/* Compute the combine constant to be used in crc_combine_fixed. len is the
   length of the CRC's message, which is expected to be constant. */
uint64_t DLL_EXPORT crc_combine_constant(params_t *params, uint64_t len);

/* Combine two CRCs. xp is the constant precomputed using crc_combine_constant.
   This is more efficient than crc_combine if the length of the message is the
   same for all CRCs. */
uint64_t DLL_EXPORT crc_combine_fixed(params_t *params, uint64_t crc, uint64_t crc2, uint64_t xp);

/* Combine two CRCs. len is the length of the second CRC's message. */
uint64_t DLL_EXPORT crc_combine(params_t *params, uint64_t crc, uint64_t crc2, uint64_t len);

#endif
