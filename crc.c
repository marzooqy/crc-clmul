#include <stdio.h>
#include "crc.h"

#ifndef DISABLE_SIMD
#include "cpu.h"
#include "intrinsics.h"

#ifdef __GNUC__
#ifdef __x86_64__
    #define TARGET_ATTRIBUTE __attribute__((target("sse4.2,pclmul")))
#elif __aarch64__
    #define TARGET_ATTRIBUTE __attribute__((target("+aes")))
#else
    #error "Unsupported Architecture. Compile on x86-64 or aarch64 or use DISABLE_SIMD."
#endif
#elif _MSC_VER
#if defined(_M_AMD64) || defined(_M_ARM64)
    #define TARGET_ATTRIBUTE
#else
    #error "Unsupported Architecture. Compile on x86-64 or aarch64 or use DISABLE_SIMD."
#endif
#else
#error "Unsupported Compiler. Use GCC, Clang, or MSVC."
#endif
#endif

//----------------------------------------

/* Static function definitions */

static uint64_t reflect(uint64_t x, uint8_t w);
static uint64_t xnmodp(params_t *params, uint16_t n);
static uint64_t crc_initial(params_t *params, uint64_t crc);
static uint64_t crc_final(params_t *params, uint64_t crc);
static uint64_t crc_bits(params_t *params, uint64_t crc, uint8_t byte, uint8_t len);
static uint64_t crc_bytes(params_t *params, uint64_t crc, unsigned char const *buf, uint64_t len);
static uint64_t multmodp_sw(params_t *params, uint64_t a, uint64_t b);
static uint64_t multmodp(params_t *params, uint64_t a, uint64_t b);
static void crc_build_table(params_t *params);
static void crc_build_combine_table(params_t *params);

#ifndef DISABLE_SIMD
static uint128_t fold(uint128_t x, uint128_t y, uint128_t k);
static uint64_t crc_clmul(params_t *params, uint64_t crc, unsigned char const *buf, uint64_t len);
static uint64_t multmodp_hw(params_t *params, uint64_t a, uint64_t b);
#endif

//----------------------------------------

/* Debug functions */

#ifdef DEBUG
#include <assert.h>

/* Prints a 64-bit integer in hexadecimal form. */
static void print_hex64(uint64_t n) {
    printf("0x%llx\n", n);
}

#ifndef DISABLE_SIMD
/* Prints the value of a 128-bit register in hexadecimal form. */
static void print_hex128(uint128_t n) {
    unsigned char *c = (unsigned char*) &n;
    printf("0x");
    for(uint8_t i = 0; i < 16; i++) {
        printf("%02x", c[15 - i]);
    }
    printf("\n");
}
#endif
#endif

//----------------------------------------

/* Bit manipulation and math functions */

/* Converts a boolean into an AND mask. */
#define and_mask(c) (-(uint64_t)(c))

/* Reflects an integer x of width w. */
static uint64_t reflect(uint64_t x, uint8_t w) {
    x = ((x >> 32) & 0xffffffff) | ((x << 32) & 0xffffffff00000000);
    x = ((x >> 16) & 0xffff0000ffff) | ((x << 16) & 0xffff0000ffff0000);
    x = ((x >> 8) & 0xff00ff00ff00ff) | ((x << 8) & 0xff00ff00ff00ff00);
    x = ((x >> 4) & 0xf0f0f0f0f0f0f0f) | ((x << 4) & 0xf0f0f0f0f0f0f0f0);
    x = ((x >> 2) & 0x3333333333333333) | ((x << 2) & 0xcccccccccccccccc);
    x = ((x >> 1) & 0x5555555555555555) | ((x << 1) & 0xaaaaaaaaaaaaaaaa);
    return x >> (64 - w);
}

/* Computes x^n mod p. This is similar to the regular CRC calculation but we have
   to stop earlier, since there is no multiplication by x^w like in CRC. Assumes
   that the polynomial has been scaled to 64-bits, and that n >= 64.*/
static uint64_t xnmodp(params_t *params, uint16_t n) {
    uint64_t mod = params->poly;

    if(params->refin) {
        while(n-- > 64) {
            mod = (mod >> 1) ^ (params->poly & and_mask(mod & 1));
        }
    } else {
        while(n-- > 64) {
            mod = (mod << 1) ^ (params->poly & and_mask(mod >> 63));
        }
    }

    return mod;
}

//----------------------------------------

/* params_t constructor */

/* Returns a params_t struct with the specified parameters.
   The table and other constants are computed as well.

   Most of the parameters are due to translating the algorithm from hardware to software.

   width is the width of the polynomial.

   poly is the divisor in the CRC algorithm.

   When refin is false, poly is multiplied by x^(64 - w). In the non-reflected
   table algorithm, this truncates any bits that are shifted out of the register
   from the left. In the non-reflected SIMD algorithm, this converts any CRC to
   64 bits (Intel paper p16), allowing us to use the same algorithm for all CRC
   parameters without any adjustments. This has the additional benefit of using
   the same alignment for both algorithms.

   When refin is true, poly is reflected.

   Polynomials have an implicit x^(w+1) term, which is typically not included in
   code. The CRC could be computed without it, and CRC64 would require a larger
   integer if it were used.

   refin specifies if the incoming bytes should be reflected before being used
   to compute the CRC. Alternatively we could "reflect the world" (Ross Williams
   guide section 11). That is, we reflect the init, poly, and reverse the
   algorithm.

   refout specifies if the result should be reflected at the end of the calculation.

   init is the initial content of the register. It's XORed with the first few
   incoming bits. pycrc labels it more clearly as xor_in.

   xorout is XORed with the CRC at the end of the calculation.

   k1 and k2 are the constants used to fold the buffer (Intel paper p12).

   table holds the values for the byte-by-byte (or Sarwate) algorithm.
   It's the result of computing the CRC for every possible input byte.

   combine_table holds multiples of x^i mod p for combining CRCs. */

/* For using CLMUL in the reflected domain, the Intel paper offers three
   different solutions (Intel paper p18-20):

   1- Shift to the left by 1 in each iteration. This requires
      additional instructions inside the folding loop.

   2- Do the left shift on k1 and k2 instead. This seems to cause the
      constants to be 65-bits long in some cases, which is undesireable.

   3- Use x^(n-1) mod p for computing k1 and k2. This method
      appears to work and it is the simplest to implement.

   This basically makes the CLMUL instruction compute the correct result
   despite the fact that we are working in the reflected domain. */

params_t crc_params(uint8_t width, uint64_t poly, uint64_t init, bool refin, bool refout, uint64_t xorout, uint64_t check, uint8_t *error) {
    #ifndef DISABLE_SIMD
    cpu_check_features();
    #endif

    params_t params;
    *error = 0;

    if(width == 0 || width > 64) {
        *error |= CRC_WIDTH_NOT_SUPPORTED;
    }

    if(width < 64) {
        if(poly >> width) {
            *error |= CRC_POLY_BIG;
        }
        if(init >> width) {
            *error |= CRC_INIT_BIG;
        }
        if(xorout >> width) {
            *error |= CRC_XOROUT_BIG;
        }
    }

    if((poly & 1) != 1) {
        *error |= CRC_POLY_EVEN;
    }

    params.width = width;
    params.poly = refin ? reflect(poly, width) : poly << (64 - width);
    params.refin = refin;
    params.refout = refout;

    /* Reflect the init if refout is true, and XOR it with xorout to
       yield the result of computing the CRC of an empty buffer. */
    params.init = (refout ? reflect(init, width) : init) ^ xorout;

    params.xorout = xorout;

    #ifndef DISABLE_SIMD
    params.k1 = refin ? xnmodp(&params, 512+64-1) : xnmodp(&params, 512+64);
    params.k2 = refin ? xnmodp(&params, 512-1)    : xnmodp(&params, 512);
    params.k3 = refin ? xnmodp(&params, 128+64-1) : xnmodp(&params, 128+64);
    params.k4 = refin ? xnmodp(&params, 128-1)    : xnmodp(&params, 128);
    #endif

    crc_build_table(&params);
    crc_build_combine_table(&params);

    uint64_t crc = crc_table(&params, params.init, "123456789", 9);
    if(crc != check) {
        *error |= CRC_CHECK_INVALID;
    }

    return params;
}

/* Print a readable error message for the errors coming from crc_params. */
void crc_print_errors(uint8_t error) {
    if(error & CRC_WIDTH_NOT_SUPPORTED) {
        printf("width should be larger than 0 and less than or equal to 64.\n");
    }
    if(error & CRC_POLY_BIG) {
        printf("poly width is larger than the width parameter.\n");
    }
    if(error & CRC_INIT_BIG) {
        printf("init width is larger than the width parameter.\n");
    }
    if(error & CRC_XOROUT_BIG) {
        printf("xorout width is larger than the width parameter.\n");
    }
    if (error & CRC_POLY_EVEN) {
        printf("CRC polynomial is even.\n");
    }
    if(error & CRC_CHECK_INVALID) {
        printf("check value doesn't match the CRC computed from the provided parameters.\n");
    }
}

//----------------------------------------

/* CRC pre-processing and post-processing functions */

/* Applied before computing the CRC. Reverse the xorout from the last application.
   Reflect the CRC if refin or refout are true. if refin is false then scale the
   CRC by 64 - w just like the polynomial. */
static uint64_t crc_initial(params_t *params, uint64_t crc) {
    crc ^= params->xorout;
    if(params->refin ^ params->refout) {
        crc = reflect(crc, params->width);
    }
    if(!params->refin) {
        crc <<= 64 - params->width;
    }
    return crc;
}

/* Applied after computing the CRC. If refin is false then scale back by 64 - w.
   Reflect the CRC if refin or refout are true. XOR with xorout. */
static uint64_t crc_final(params_t *params, uint64_t crc) {
    if(!params->refin) {
        crc >>= 64 - params->width;
    }
    if(params->refin ^ params->refout) {
        crc = reflect(crc, params->width);
    }
    return crc ^ params->xorout;
}

//----------------------------------------

/* CRC calculation functions */

/* Computes the 256 element table for the tabular algorithm. */
static void crc_build_table(params_t *params) {
    for(uint16_t i = 0; i < 256; i++) {
        params->table[i] = crc_bits(params, 0, i, 8);
    }
}

/* Computes the CRC of up to 8 bits using the bit-by-bit algorithm. */
static uint64_t crc_bits(params_t *params, uint64_t crc, uint8_t byte, uint8_t len) {
    if(params->refin) {
        uint8_t mask = (uint8_t)-1 >> (8 - len);
        crc ^= byte & mask;
        while(len--) {
            crc = (crc >> 1) ^ (params->poly & and_mask(crc & 1));
        }
    } else {
        uint8_t mask = (uint8_t)-1 << (8 - len);
        crc ^= (uint64_t)(byte & mask) << 56;
        while(len--) {
            crc = (crc << 1) ^ (params->poly & and_mask(crc >> 63));
        }
    }
    return crc;
}

/* Compute the CRC byte-by-byte using the lookup table. */
static uint64_t crc_bytes(params_t *params, uint64_t crc, unsigned char const *buf, uint64_t len) {
    if(params->refin) {
        while(len--) {
            crc = (crc >> 8) ^ params->table[(crc ^ *buf++) & 0xff];
        }
    } else {
        while(len--) {
            crc = (crc << 8) ^ params->table[(crc >> 56) ^ *buf++];
        }
    }
    return crc;
}

/* Table-based implementation of CRC. */
uint64_t crc_table(params_t *params, uint64_t crc, unsigned char const *buf, uint64_t len) {
    crc = crc_initial(params, crc);
    crc = crc_bytes(params, crc, buf, len);
    return crc_final(params, crc);
}

/* Hardware accelerated algorithm based on the version used in Chromium.

   The folding method (Intel paper p11-13) is used to reduce the buffer to a smaller
   buffer "congruent (modulo the polynomial) to the original one" (Intel paper p7).
   Since the new buffer is congruent, we could just use the table-based algorithm
   on the new buffer to find the CRC. This allows us to skip much of the paper.

   This doesn't affect performance much, as the table-wise algorithm is used for
   <= 46 bytes. It would be noticably slower if the input data buffer is small,
   but in that case the speed of the table algorithm is enough.

   It should be possible to extend this algorithm to use the 256 and 512 bit
   variants of CLMUL, using a similar approach to the one shown here. */

#ifndef DISABLE_SIMD
/* Fold x and y using the folding constants stored in k. */
TARGET_ATTRIBUTE
static uint128_t fold(uint128_t x, uint128_t y, uint128_t k) {
    uint128_t h = intrin_clmul_hi(x, k);
    uint128_t l = intrin_clmul_lo(x, k);
    return intrin_tri_xor(h, l, y);
}

TARGET_ATTRIBUTE
static uint64_t crc_clmul(params_t *params, uint64_t crc, unsigned char const *buf, uint64_t len) {
    if(len >= 128) {
        //Align to a 16 byte memory boundary.
        uint64_t adrs = (uintptr_t)buf & 0xf;
        if(adrs) {
            uint64_t rem = 16 - adrs;
            crc = crc_bytes(params, crc, buf, rem);
            buf += rem;
            len -= rem;
        }

        #ifdef DEBUG
        assert(((uintptr_t)buf & 0xf) == 0);
        #endif

        if(len >= 128) {
            uint128_t x1, x2, x3, x4;
            uint128_t y1, y2, y3, y4;

            if(params->refin) {
                //Reflected algorithm
                //Data alignment: [ax^0 bx^1 ... cx^n]
                uint128_t c = intrin_set(0, crc);
                uint128_t k2k1 = intrin_set(params->k2, params->k1);
                uint128_t k4k3 = intrin_set(params->k4, params->k3);

                x1 = intrin_load_le(buf);
                x2 = intrin_load_le(buf + 16);
                x3 = intrin_load_le(buf + 32);
                x4 = intrin_load_le(buf + 48);

                buf += 64;
                len -= 64;

                //XOR with the init.
                x1 = intrin_xor(x1, c);

                //Fold by 4.
                while(len >= 64) {
                    y1 = intrin_load_le(buf);
                    y2 = intrin_load_le(buf + 16);
                    y3 = intrin_load_le(buf + 32);
                    y4 = intrin_load_le(buf + 48);

                    x1 = fold(x1, y1, k2k1);
                    x2 = fold(x2, y2, k2k1);
                    x3 = fold(x3, y3, k2k1);
                    x4 = fold(x4, y4, k2k1);

                    buf += 64;
                    len -= 64;
                }

                //Fold to 128 bits.
                x1 = fold(x1, x2, k4k3);
                x1 = fold(x1, x3, k4k3);
                x1 = fold(x1, x4, k4k3);

                //Fold by 1.
                while(len >= 16) {
                    y1 = intrin_load_le(buf);
                    x1 = fold(x1, y1, k4k3);
                    buf += 16;
                    len -= 16;
                }

            } else {
                //Non-reflected algorithm
                //Data alignment: [ax^n bx^(n-1) ... cx^0]
                uint128_t c = intrin_set(crc, 0);
                uint128_t k1k2 = intrin_set(params->k1, params->k2);
                uint128_t k3k4 = intrin_set(params->k3, params->k4);

                x1 = intrin_load_bg(buf);
                x2 = intrin_load_bg(buf + 16);
                x3 = intrin_load_bg(buf + 32);
                x4 = intrin_load_bg(buf + 48);

                buf += 64;
                len -= 64;

                //XOR the left side of buf with the initial value.
                x1 = intrin_xor(x1, c);

                //Fold by 4.
                while(len >= 64) {
                    y1 = intrin_load_bg(buf);
                    y2 = intrin_load_bg(buf + 16);
                    y3 = intrin_load_bg(buf + 32);
                    y4 = intrin_load_bg(buf + 48);

                    x1 = fold(x1, y1, k1k2);
                    x2 = fold(x2, y2, k1k2);
                    x3 = fold(x3, y3, k1k2);
                    x4 = fold(x4, y4, k1k2);

                    buf += 64;
                    len -= 64;
                }

                //Fold to 128 bits.
                x1 = fold(x1, x2, k3k4);
                x1 = fold(x1, x3, k3k4);
                x1 = fold(x1, x4, k3k4);

                //Fold by 1.
                while(len >= 16) {
                    y1 = intrin_load_bg(buf);
                    x1 = fold(x1, y1, k3k4);
                    buf += 16;
                    len -= 16;
                }

                x1 = intrin_swap(x1);
            }

            //Find the CRC using the folded data.
            crc = crc_bytes(params, 0, (unsigned char*) &x1, 16);
        }
    }

    //Compute the remaining bytes and return the CRC.
    return crc_bytes(params, crc, buf, len);
}
#endif

/* SIMD implementation of CRC with software fallback. */
uint64_t crc_calc(params_t *params, uint64_t crc, unsigned char const *buf, uint64_t len) {
    crc = crc_initial(params, crc);

    #ifndef DISABLE_SIMD
    if(cpu_enable_simd) {
        crc = crc_clmul(params, crc, buf, len);
    } else {
        crc = crc_bytes(params, crc, buf, len);
    }

    #else
    crc = crc_bytes(params, crc, buf, len);
    #endif

    return crc_final(params, crc);
}

/* Same as above but accepts the length in bits. */
uint64_t crc_calc_bits(params_t *params, uint64_t crc, unsigned char const *buf, uint64_t len) {
    uint64_t bytes = len / 8;
    uint8_t bits = len % 8;

    crc = crc_initial(params, crc);

    #ifndef DISABLE_SIMD
    if(cpu_enable_simd) {
        crc = crc_clmul(params, crc, buf, bytes);
    } else {
        crc = crc_bytes(params, crc, buf, bytes);
    }

    #else
    crc = crc_bytes(params, crc, buf, bytes);
    #endif

    crc = crc_bits(params, crc, *(buf + bytes), bits);
    return crc_final(params, crc);
}

//----------------------------------------

/* CRC combine functions */

/* Adler's multmodp. Computes (a * b) mod p.
   a*b mod p = sum(b * a[i] * x*i  mod p) for i = 0...63 */
static uint64_t multmodp_sw(params_t *params, uint64_t a, uint64_t b) {
    uint64_t prod;

    if(params->refin) {
        prod = b & and_mask(a >> 63);
        while(a) {
            a <<= 1;
            b = (b >> 1) ^ (params->poly & and_mask(b & 1));
            prod ^= b & and_mask(a >> 63);
        }

    } else {
        prod = b & and_mask(a & 1);
        while(a) {
            a >>= 1;
            b = (b << 1) ^ (params->poly & and_mask(b >> 63));
            prod ^= b & and_mask(a & 1);
        }
    }

    return prod;
}

/* Hardware version of multmodp. Multiplies a and b using the CLMUL intrinsic
   and finds the modulos using the CRC table. */
#ifndef DISABLE_SIMD
TARGET_ATTRIBUTE
static uint64_t multmodp_hw(params_t *params, uint64_t a, uint64_t b) {
    //Load into two registers and multiply.
    uint128_t ar = intrin_set(0, a);
    uint128_t br = intrin_set(0, b);

    uint128_t prod = intrin_clmul_lo(ar, br);

    uint64_t hi = intrin_get(prod, 1);
    uint64_t lo = intrin_get(prod, 0);

    //Shift to the left by 1 to account for reflection (Intel paper p20).
    if(params->refin) {
        hi = (hi << 1) | (lo >> 63);
        lo <<= 1;
        for(uint8_t i = 0; i < 8; i++) {
            lo = (lo >> 8) ^ params->table[lo & 0xff];
        }

    } else {
        for(uint8_t i = 0; i < 8; i++) {
            hi = (hi << 8) ^ params->table[hi >> 56];
        }
    }

    return hi ^ lo;
}
#endif

/* Selects the version of multmodp to use based on the availability of
   hardware intrinsics.*/
static uint64_t multmodp(params_t *params, uint64_t a, uint64_t b) {
    #ifndef DISABLE_SIMD
    if(cpu_enable_simd) {
        return multmodp_hw(params, a, b);
    } else {
        return multmodp_sw(params, a, b);
    }
    #else
    return multmodp_sw(params, a, b);
    #endif
}

/* Fills combine_table with values of x^2^i mod p. */
static void crc_build_combine_table(params_t *params) {
    uint64_t sq = params->refin ? (uint64_t)1 << 62 : 2; //x^1

    sq = multmodp(params, sq, sq); //x^2
    sq = multmodp(params, sq, sq); //x^4

    //First value is x^8 mod p.
    for(uint8_t i = 0; i < 64; i++) {
        sq = multmodp(params, sq, sq); //x^2^(i+3)
        params->combine_table[i] = sq;
    }
}

/* Multiplies the various x^2^i mod p factors to get x^8n mod p. This constant
   can be precomputed once and reused in conjunction with crc_combine_fixed
   if the length of the second CRC's message is always the same. */
uint64_t crc_combine_constant(params_t *params, uint64_t len) {
    if(len == 0) {
        return params->refin ? (uint64_t)1 << 63 : 1;
    }

    uint64_t xp;
    uint8_t i = 0;

    while((len & 1) == 0) {
        len >>= 1;
        i++;
    }

    xp = params->combine_table[i];

    while(len) {
        len >>= 1;
        i++;
        if(len & 1) {
            xp = multmodp(params, xp, params->combine_table[i]);
        }
    }

    return xp;
}

/* Same as above but accepts the length in bits. */
uint64_t crc_combine_constant_bits(params_t *params, uint64_t len) {
    uint64_t xp = crc_combine_constant(params, len / 8);
    return crc_bits(params, xp, 0, len % 8);
}

/* Find CRC(A + B) from CRC(A) and CRC(B) if the length of B is fixed using the
   constant precomputed from crc_combine_constant.*/
uint64_t crc_combine_fixed(params_t *params, uint64_t crc, uint64_t crc2, uint64_t xp) {
    /* It's not clear why we should XOR with the initial. It could be that we
       are treating the CRC as the first incoming bits to the register. */
    crc ^= params->init ^ params->xorout;
    crc = crc_initial(params, crc);
    crc2 = crc_initial(params, crc2);

    crc = multmodp(params, crc, xp) ^ crc2;

    return crc_final(params, crc);
}

/* Same as crc_combine_fixed but the combine constant is computed in real-time. */
uint64_t crc_combine(params_t *params, uint64_t crc, uint64_t crc2, uint64_t len) {
    return crc_combine_fixed(params, crc, crc2, crc_combine_constant(params, len));
}

/* Same as above but accepts the length in bits. */
uint64_t crc_combine_bits(params_t *params, uint64_t crc, uint64_t crc2, uint64_t len) {
    return crc_combine_fixed(params, crc, crc2, crc_combine_constant_bits(params, len));
}