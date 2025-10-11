#ifndef CRC_CLMUL_H
#define CRC_CLMUL_H

#include <stdbool.h>
#include <stdint.h>

/* Holds frequently used CRC parameters.
   Most of these are due to translating the algorithm from hardware to software.

   width is the width of the polynomial.

   poly is the divisor in the CRC algorithm.

   When refin is false, poly is multiplied by x^(64 - w). In the non-reflected table algorithm,
   this truncates any bits that are shifted out of the register from the left. In the non-reflected SIMD algorithm,
   this converts any CRC to 64 bits (Intel paper p16), allowing us to use the same algorithm for all CRC parameters
   without any adjustments. This has the additional benefit of using the same alignment for both algorithms.

   When refin is true, poly is reflected.

   Polynomials have an implicit x^(w+1) term, which is typically not included in code.
   The CRC could be computed without it, and CRC64 would require a larger integer if it were used.

   refin specifies if the incoming bytes should be reflected before being used to compute the CRC.
   Alternatively we could "reflect the world" (Ross Williams guide section 11).
   That is, we reflect the init, poly, and reverse the algorithm.

   refout specifies if result should be reflected at the end of the calculation.

   init is the initial content of the register. It's XORed with the first few incoming bits.
   pycrc labels it more clearly as xor_in.

   xorout is XORed with the CRC at the end of the calculation.

   k1 and k2 are the constant used to fold the buffer (Intel paper p12).

   table holds the values for the byte-by-byte (or Sarwate) algorithm.
   It's the result of computing the CRC for every possible input byte.*/

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
params_t crc_params(uint8_t width, uint64_t poly, uint64_t init, bool refin, bool refout, uint64_t xorout);

/* Calculate a CRC using the table-based algorithm.
   Use params.init as the initial CRC value.*/
uint64_t crc_table(params_t *params, uint64_t crc, unsigned char const *buf, uint64_t len);

/* Calculate CRC using the SIMD algorithm.
   Use params.init as the initial CRC value.*/
uint64_t crc_clmul(params_t *params, uint64_t crc, unsigned char const *buf, uint64_t len);

#endif
