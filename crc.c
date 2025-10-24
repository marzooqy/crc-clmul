#include "cpu.h"
#include "crc.h"

#ifndef CPU_NO_SIMD
#include "intrinsics.h"
#endif

#ifdef DEBUG
#include <stdio.h>

// Prints a 64-bit integer in hexadecimal form.
static void print_hex64(uint64_t n) {
    printf("0x%llx\n", n);
}

#ifndef CPU_NO_SIMD
// Prints the value of a 128-bit register in hexadecimal form.
static void print_hex128(uint128_t n) {
    unsigned char* c = (unsigned char*) &n;
    printf("0x");
    for(uint8_t i = 0; i < 16; i++) {
        printf("%02x", c[15 - i]);
    }
    printf("\n");
}
#endif
#endif

/* Reflects an integer x of width w */
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
   to stop earlier, as the data isn't multiplied by x^w like in CRC. Assumes that
   the polynomial has been scaled to 64-bits, and that n is larger than 64.*/
static uint64_t xnmodp(params_t* params, uint16_t n) {
    uint64_t mod = params->poly;

    if(params->refin) {
        while(n-- > 64) {
            mod = mod & 1 ? (mod >> 1) ^ params->poly : mod >> 1;
        }
    } else {
        uint64_t mask = (uint64_t)1 << 63;
        while(n-- > 64) {
            mod = mod & mask ? (mod << 1) ^ params->poly : mod << 1;
        }
    }

    return mod;
}

/* Computes the 256 element table for the tabular algorithm. */
static void crc_build_table(params_t *params) {
    for(uint16_t i = 0; i < 256; i++) {
        uint64_t crc = i;

        if(params->refin) {
            for(uint8_t j = 0; j < 8; j++) {
                crc = crc & 1 ? (crc >> 1) ^ params->poly : crc >> 1;
            }
        }
        else {
            uint64_t mask = (uint64_t)1 << 63;
            crc <<= 56;
            for(uint8_t j = 0; j < 8; j++) {
                crc = crc & mask ? (crc << 1) ^ params->poly : crc << 1;
            }
        }
        params->table[i] = crc;
    }
}

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

   refout specifies if result should be reflected at the end of the calculation.

   init is the initial content of the register. It's XORed with the first few
   incoming bits. pycrc labels it more clearly as xor_in.

   xorout is XORed with the CRC at the end of the calculation.

   k1 and k2 are the constants used to fold the buffer (Intel paper p12).

   table holds the values for the byte-by-byte (or Sarwate) algorithm.
   It's the result of computing the CRC for every possible input byte.*/

/* The Intel paper offers three different solutions for maintaining
   alignment in the reflected algorithm (Intel paper p18-20):

   1- Shift to the left by 1 in each iteration. This requires an
      additional instruction inside the parallel folding loop.

   2- Do the left shift on k1 and k2 instead. This seems to cause the
      constants to be 65-bits long in some cases, which is undesireable.

   3- Use x^(n-1) mod p for computing k1 and k2. This method
      appears to work and it is the simplest to implement.

   This basically makes the CLMUL instruction compute the correct result
   despite the fact that we are working in the reflected domain. */

params_t crc_params(uint8_t width, uint64_t poly, uint64_t init, bool refin, bool refout, uint64_t xorout) {
    #ifndef CPU_NO_SIMD
    cpu_check_features();
    #endif

    params_t params;
    params.width = width;

    //Reflected:     (p * x^(64-w))'
    //Non-reflected:  p * x^(64-w)
    params.poly = refin ? reflect(poly, width) : poly << (64 - width);

    params.refin = refin;
    params.refout = refout;

    /* Reflect the init if refout is true, and XOR it with xorout to
       yield the result of computing the CRC of an empty buffer. */
    params.init = (refout ? reflect(init, width) : init) ^ xorout;

    params.xorout = xorout;

    //Reflected:     (x^(512+64-1) mod p)'
    //Non-reflected:  x^(512+64) mod p
    params.k1 = refin ? xnmodp(&params, 575) : xnmodp(&params, 576);

    //Reflected:     (x^(512-1) mod p)'
    //Non-reflected:  x^512 mod p
    params.k2 = refin ? xnmodp(&params, 511) : xnmodp(&params, 512);

    crc_build_table(&params);

    return params;
}

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

/* Compute the CRC byte-by-byte using the lookup table. */
static uint64_t crc_bytes(params_t *params, uint64_t crc, unsigned char const *buf, uint64_t len) {
    if(params->refin) {
        while(len--) {
            crc = (crc >> 8) ^ params->table[(crc ^ *buf++) & 0xff];
        }
    }
    else {
        while(len--) {
            crc = (crc << 8) ^ params->table[((crc >> 56) ^ *buf++) & 0xff];
        }
    }

    return crc;
}

/* Hardware accelerated algorithm based on the version used in Chromium.

   The fold-by-4 method (Intel paper p11-12) is used to reduce the buffer to a smaller
   buffer "congruent (modulo the polynomial) to the original one" (Intel paper p7).
   Since the new buffer is congruent, we could just use the table-based algorithm
   on the new buffer to find the CRC. This allows us to skip much of the paper.

   This doesn't affect performance much, as the table-wise algorithm is used
   for < 192 bytes. It would be noticably slower if the input data buffer is
   small, but in that case the speed of the table algorithm is enough.

   It should be possible to extend this algorithm to use the 256 and 512 bit
   variants of CLMUL, using a similar approach to the one shown here.*/

#ifndef CPU_NO_SIMD
#ifdef __x86_64__
__attribute__((target("ssse3,pclmul")))
#elif __aarch64__
__attribute__((target("+aes")))
#endif
static uint64_t crc_clmul(params_t *params, uint64_t crc, unsigned char const *buf, uint64_t len) {
    if(len >= 128) {
        uint128_t b1, b2, b3, b4;

        //After every multiplication the result is split into an upper and
        //lower half to avoid overflowing the register (Intel paper p8-9).
        uint128_t h1, h2, h3, h4;
        uint128_t l1, l2, l3, l4;

        if(params->refin) {
            //Reflected algorithm
            //Data alignment: [ax^0 bx^1 ... cx^n]
            uint128_t c = SET(0, crc);
            uint128_t k2k1 = SET(params->k2, params->k1);

            //Load 64 bytes from buf into the registers.
            b1 = LOAD(buf + 0x00);
            b2 = LOAD(buf + 0x10);
            b3 = LOAD(buf + 0x20);
            b4 = LOAD(buf + 0x30);

            //XOR with the init.
            b1 = XOR(b1, c);

            buf += 64;
            len -= 64;

            while(len >= 64) {
                //Multiply by k1.
                h1 = CLMUL_LO(b1, k2k1);
                h2 = CLMUL_LO(b2, k2k1);
                h3 = CLMUL_LO(b3, k2k1);
                h4 = CLMUL_LO(b4, k2k1);

                //Multiply by k2.
                l1 = CLMUL_HI(b1, k2k1);
                l2 = CLMUL_HI(b2, k2k1);
                l3 = CLMUL_HI(b3, k2k1);
                l4 = CLMUL_HI(b4, k2k1);

                //Load the next chunk into the registers.
                b1 = LOAD(buf + 0x00);
                b2 = LOAD(buf + 0x10);
                b3 = LOAD(buf + 0x20);
                b4 = LOAD(buf + 0x30);

                //XOR.
                b1 = XOR(b1, h1);
                b2 = XOR(b2, h2);
                b3 = XOR(b3, h3);
                b4 = XOR(b4, h4);

                b1 = XOR(b1, l1);
                b2 = XOR(b2, l2);
                b3 = XOR(b3, l3);
                b4 = XOR(b4, l4);

                buf += 64;
                len -= 64;
            }

        } else {
            //Non-reflected algorithm
            //Data alignment: [ax^n bx^(n-1) ... cx^0]
            uint128_t c = SET(crc, 0);
            uint128_t k1k2 = SET(params->k1, params->k2);

            //Shuffle mask/Index table to be used with the SWAP macro.
            //Swaps the endianess of the register.
            table_t tbl = GET_SWAP_TABLE();

            //Load 64 bytes from buf into the registers.
            b1 = LOAD(buf + 0x00);
            b2 = LOAD(buf + 0x10);
            b3 = LOAD(buf + 0x20);
            b4 = LOAD(buf + 0x30);

            //Byte swap.
            b1 = SWAP(b1, tbl);
            b2 = SWAP(b2, tbl);
            b3 = SWAP(b3, tbl);
            b4 = SWAP(b4, tbl);

            //XOR the left side of buf with the initial value.
            b1 = XOR(b1, c);

            buf += 64;
            len -= 64;

            while(len >= 64) {
                //Multiply by k1.
                h1 = CLMUL_HI(b1, k1k2);
                h2 = CLMUL_HI(b2, k1k2);
                h3 = CLMUL_HI(b3, k1k2);
                h4 = CLMUL_HI(b4, k1k2);

                //Multiply by k2.
                l1 = CLMUL_LO(b1, k1k2);
                l2 = CLMUL_LO(b2, k1k2);
                l3 = CLMUL_LO(b3, k1k2);
                l4 = CLMUL_LO(b4, k1k2);

                //Load the next chunk into the registers.
                b1 = LOAD(buf + 0x00);
                b2 = LOAD(buf + 0x10);
                b3 = LOAD(buf + 0x20);
                b4 = LOAD(buf + 0x30);

                //Byte swap.
                b1 = SWAP(b1, tbl);
                b2 = SWAP(b2, tbl);
                b3 = SWAP(b3, tbl);
                b4 = SWAP(b4, tbl);

                //XOR.
                b1 = XOR(b1, h1);
                b2 = XOR(b2, h2);
                b3 = XOR(b3, h3);
                b4 = XOR(b4, h4);

                b1 = XOR(b1, l1);
                b2 = XOR(b2, l2);
                b3 = XOR(b3, l3);
                b4 = XOR(b4, l4);

                buf += 64;
                len -= 64;
            }

            //Byte swap.
            b1 = SWAP(b1, tbl);
            b2 = SWAP(b2, tbl);
            b3 = SWAP(b3, tbl);
            b4 = SWAP(b4, tbl);
        }

        //Calculate the CRC of what's left using the table-based algorithm.
        crc = crc_bytes(params, 0, (unsigned char*) &b1, 16);
        crc = crc_bytes(params, crc, (unsigned char*) &b2, 16);
        crc = crc_bytes(params, crc, (unsigned char*) &b3, 16);
        crc = crc_bytes(params, crc, (unsigned char*) &b4, 16);
    }

    //Compute the remaining bytes and return the CRC.
    return crc_bytes(params, crc, buf, len);
}
#endif

#ifdef DEBUG
/* Table-based implementation of CRC. */
uint64_t crc_table(params_t *params, uint64_t crc, unsigned char const *buf, uint64_t len) {
    crc = crc_initial(params, crc);
    crc = crc_bytes(params, crc, buf, len);
    return crc_final(params, crc);
}
#endif

/* SIMD implementation of CRC. */
uint64_t crc_calc(params_t *params, uint64_t crc, unsigned char const *buf, uint64_t len) {
    crc = crc_initial(params, crc);

    #ifndef CPU_NO_SIMD
    #ifdef __x86_64__
    bool enable_simd = x86_cpu_enable_simd;
    #elif __aarch64__
    bool enable_simd = arm_cpu_enable_pmull;
    #endif

    if(enable_simd) {
        crc = crc_clmul(params, crc, buf, len);
    } else {
        crc = crc_bytes(params, crc, buf, len);
    }

    #else
    crc = crc_bytes(params, crc, buf, len);
    #endif

    return crc_final(params, crc);
}