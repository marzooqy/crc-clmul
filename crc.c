#include <emmintrin.h>
#include <tmmintrin.h>
#include <wmmintrin.h>
#include "crc.h"

/* References:
- GF2 Math:
Joey: pclmulqdq Tricks (https://wunkolo.github.io/post/2020/05/pclmulqdq-tricks)
Tad McCorkle: Cyclic Redundancy Check (https://tadmccorkle.com/blog/posts/cyclic-redundancy-check)

- CRC Basics and Software Implementations:
Ross Williams: A PAINLESS GUIDE TO CRC ERROR DETECTION ALGORITHMS (https://www.zlib.net/crc_v3.txt)
Bastian Molkenthin: Understanding CRC (https://www.sunshine2k.de/articles/coding/crc/understanding_crc.html)
Greg Cook: Catalogue of parametrised CRC algorithms (https://reveng.sourceforge.io/crc-catalogue)
Stephan Brumme: Fast CRC32 (https://create.stephan-brumme.com/crc32)
Mark Adler: crcany (https://github.com/madler/crcany)

- Intel Intrinsics:
Daniel Graham and Charles Reiss: A Quick Guide to SSE/SIMD (https://www.cs.virginia.edu/~cr4bd/3330/F2018/simdref.html)
Intel: Intel Intrinsics Guide (https://www.intel.com/content/www/us/en/docs/intrinsics-guide/index.html)
Stack Overflow Answer: Header files for x86 SIMD intrinsics (https://stackoverflow.com/a/11228864)
Free Software Foundation: GCC's x86 Compile Options (https://gcc.gnu.org/onlinedocs/gcc/x86-Options.html)

- Hardware Acceleration using the CLMUL Instruction:
Intel: Fast CRC Computation for Generic Polynomials Using PCLMULQDQ Instruction (https://web.archive.org/web/20230315165408/https://www.intel.com/content/dam/www/public/us/en/documents/white-papers/fast-crc-computation-generic-polynomials-pclmulqdq-paper.pdf)
Chromium: Chromium's CRC32 implementation (https://chromium.googlesource.com/chromium/src/third_party/zlib/+/refs/heads/main/crc32_simd.c)
Mark Adler: Calculating constants for CRC32 using PCLMULQDQ (https://stackoverflow.com/a/21201497)
*/

/*
//Debug code

#include <stdio.h>

//Prints a 64-bit integer in hexadecimal form.
static void print_hex64(uint64_t n) {
    printf("0x%llx\n", n);
}

//Prints the value of a 128-bit register in hexadecimal form.
static void print_hex128(__m128i n) {
    unsigned char* c = (unsigned char*) &n;
    printf("0x");
    for(uint8_t i = 0; i < 16; i++) {
        printf("%02x", c[15 - i]);
    }
    printf("\n");
}
*/

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

/* Computes x^n mod p. The polynomial is assumed to be scaled by x^(64-w).
   This is similar to the regular CRC calculation but we have to stop earlier
   since we don't have the extra x^w factor that's in the CRC definition.
   */
static uint64_t xnmodp(params_t* params, uint16_t n) {
    uint64_t mask = (uint64_t)1 << 63;
    uint64_t mod = params->spoly;

    while(n-- > 64) {
        mod = mod & mask ? (mod << 1) ^ params->spoly : mod << 1;
    }

    return mod;
}

/* Computes the 256 element table for the tabular algorithm. */
static void crc_build_table(params_t *params) {
    for(uint16_t i = 0; i < 256; i++) {
        uint64_t crc = i;

        if(params->refin) {
            for(uint8_t j = 0; j < 8; j++) {
                crc = crc & 1 ? (crc >> 1) ^ params->rpoly : crc >> 1;
            }
        }
        else {
            uint64_t mask = (uint64_t)1 << 63;
            crc <<= 56;
            for(uint8_t j = 0; j < 8; j++) {
                crc = crc & mask ? (crc << 1) ^ params->spoly : crc << 1;
            }
        }
        params->table[i] = crc;
    }
}

/* Returns a params_t struct with the specified parameters.
   The table and other constants are computed as well */
params_t crc_params(uint8_t width, uint64_t poly, uint64_t init, bool refin, bool refout, uint64_t xorout) {
    params_t params;

    params.width = width;
    params.spoly = poly << (64 - width); //p * x^(64 - w)
    params.rpoly = reflect(poly, width);
    params.refin = refin;
    params.refout = refout;

    /* Reflect the init if refin or refout is true, and XOR it with xorout to
       yield the result of computing the CRC of an empty buffer. */
    params.init = (refout ? reflect(init, width) : init) ^ xorout;

    params.xorout = xorout;

    /* The Intel paper offers three different solutions for maintaining alignment in the reflected case (Intel paper p18-20):
       1- Shift to the left by 1 in each iteration. This requires an additional instruction inside the loop.
       2- Do the left shift on the constants instead. This seems to cause the constants to be 65-bits long in some cases, which is undesireable.
       3- Use x^(n-1) mod p for computing the constants. This method appears to work and it is the simplest to implement.
       This basically makes the CLMUL instruction compute the correct result despite the fact that we are working in the reflected domain*/

    //x^(512+64) mod p, p is scaled by x^(64 - w), reflected: (x^(512+64-1) mod p)'
    params.k1 = refin ? reflect(xnmodp(&params, 575), 64) : xnmodp(&params, 576);

    //x^512 mod p, p is scaled by x^(64 - w), reflected: (X^(512 - 1) mod p)'
    params.k2 = refin ? reflect(xnmodp(&params, 511), 64) : xnmodp(&params, 512);

    crc_build_table(&params);

    return params;
}

/* Applied before computing the CRC.
   Reverse the xorout from the last application.
   Reflection is only necessary in if either refin or refout are true.
   if refin is false then scale the CRC by 64 - w just like the polynomial */
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

/* Applied after computing the CRC.
   If refin is false then scale back by 64 - w.
   Reflection is only necessary in if either refin or refout are true.
   XOR with xorout. */
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

/* Table-based implementation of CRC. */
uint64_t crc_table(params_t *params, uint64_t crc, unsigned char const *buf, uint64_t len) {
    crc = crc_initial(params, crc);
    crc = crc_bytes(params, crc, buf, len);
    crc = crc_final(params, crc);
    return crc;
}

/* Just to clear my confusion around the selection/control bits.
   1 picks out the 64 MSBs and 0 picks the least 64. */
// #define CLMUL(a, b, ac, bc) _mm_clmulepi64_si128(a, b, (bc ? 0x10 : 0x00) | (ac ? 0x01 : 0x00))

/* Hardware accelerated algorithm based on the version used in Chromium.
   The fold-by-4 method (Intel paper p11-12) is used to reduce the buffer to a smaller buffer
   "congruent (modulo the polynomial) to the original one" (Intel paper p7).
   Since the new buffer is congruent, we could just use the table-based algorithm on the new buffer to find the CRC.
   This allows to skip much of the paper.
   This shouldn't affect performance much, since the table-wise algorithm is used for less than 200 bytes.
   It would be noticably slower if the input data buffer is small, but in that case peformance doesn't matter.
   It should be possible to extend this algorithm to use 256 and 512 bit variants of PCLMULQDQ,
   using a similar approach to the one shown here.*/
uint64_t crc_clmul(params_t *params, uint64_t crc, unsigned char const *buf, uint64_t len) {
    crc = crc_initial(params, crc);

    if(len >= 128) {
        uint64_t k2k1[] = {params->k2, params->k1};

        __m128i x = _mm_loadu_si128((__m128i*)k2k1);
        __m128i b1, b2, b3, b4;

        //After every multiplication the result is split into an upper and lower half
        //to avoid overflowing the register (Intel paper p8-9).
        //I'm not actually sure which one ends up holding which half,
        //but the way it's done now gives out the expected result.
        __m128i h1, h2, h3, h4;
        __m128i l1, l2, l3, l4;

        if(params->refin) {
            //Reflected algorithm
            //Data alignment: [ax^0 bx^1 ... cx^n]

            //Load 64 bytes from buf into the registers.
            b1 = _mm_loadu_si128((__m128i*)(buf + 0x00));
            b2 = _mm_loadu_si128((__m128i*)(buf + 0x10));
            b3 = _mm_loadu_si128((__m128i*)(buf + 0x20));
            b4 = _mm_loadu_si128((__m128i*)(buf + 0x30));

            b1 = _mm_xor_si128(b1, _mm_cvtsi64_si128(crc));

            buf += 64;
            len -= 64;

            while(len >= 64) {
                //Multiply by k2.
                h1 = _mm_clmulepi64_si128(b1, x, 0x10);
                h2 = _mm_clmulepi64_si128(b2, x, 0x10);
                h3 = _mm_clmulepi64_si128(b3, x, 0x10);
                h4 = _mm_clmulepi64_si128(b4, x, 0x10);

                //Multiply by k1.
                l1 = _mm_clmulepi64_si128(b1, x, 0x01);
                l2 = _mm_clmulepi64_si128(b2, x, 0x01);
                l3 = _mm_clmulepi64_si128(b3, x, 0x01);
                l4 = _mm_clmulepi64_si128(b4, x, 0x01);

                //Load the next chunk into the registers.
                b1 = _mm_loadu_si128((__m128i *)(buf + 0x00));
                b2 = _mm_loadu_si128((__m128i *)(buf + 0x10));
                b3 = _mm_loadu_si128((__m128i *)(buf + 0x20));
                b4 = _mm_loadu_si128((__m128i *)(buf + 0x30));

                //XOR.
                b1 = _mm_xor_si128(b1, h1);
                b2 = _mm_xor_si128(b2, h2);
                b3 = _mm_xor_si128(b3, h3);
                b4 = _mm_xor_si128(b4, h4);

                b1 = _mm_xor_si128(b1, l1);
                b2 = _mm_xor_si128(b2, l2);
                b3 = _mm_xor_si128(b3, l3);
                b4 = _mm_xor_si128(b4, l4);

                buf += 64;
                len -= 64;
            }

        } else {
            //Non-reflected algorithm
            //Data alignment: [ax^n bx^(n-1) ... cx^0]

            //Shuffle mask for _mm_shuffle_epi8.
            //Swaps the endianess of the register.
            __m128i m = _mm_set_epi8(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);

            //Load 64 bytes from buf into the registers.
            b1 = _mm_loadu_si128((__m128i*)(buf + 0x00));
            b2 = _mm_loadu_si128((__m128i*)(buf + 0x10));
            b3 = _mm_loadu_si128((__m128i*)(buf + 0x20));
            b4 = _mm_loadu_si128((__m128i*)(buf + 0x30));

            //Byte swap.
            b1 = _mm_shuffle_epi8(b1, m);
            b2 = _mm_shuffle_epi8(b2, m);
            b3 = _mm_shuffle_epi8(b3, m);
            b4 = _mm_shuffle_epi8(b4, m);

            //XOR the left side of buf with the initial value.
            b1 = _mm_xor_si128(b1, _mm_slli_si128(_mm_cvtsi64_si128(crc), 8));

            buf += 64;
            len -= 64;

            while(len >= 64) {
                //Multiply by k1.
                h1 = _mm_clmulepi64_si128(b1, x, 0x11);
                h2 = _mm_clmulepi64_si128(b2, x, 0x11);
                h3 = _mm_clmulepi64_si128(b3, x, 0x11);
                h4 = _mm_clmulepi64_si128(b4, x, 0x11);

                //Multiply by k2.
                l1 = _mm_clmulepi64_si128(b1, x, 0x00);
                l2 = _mm_clmulepi64_si128(b2, x, 0x00);
                l3 = _mm_clmulepi64_si128(b3, x, 0x00);
                l4 = _mm_clmulepi64_si128(b4, x, 0x00);

                //Load the next chunk into the registers.
                b1 = _mm_loadu_si128((__m128i *)(buf + 0x00));
                b2 = _mm_loadu_si128((__m128i *)(buf + 0x10));
                b3 = _mm_loadu_si128((__m128i *)(buf + 0x20));
                b4 = _mm_loadu_si128((__m128i *)(buf + 0x30));

                //Byte swap.
                b1 = _mm_shuffle_epi8(b1, m);
                b2 = _mm_shuffle_epi8(b2, m);
                b3 = _mm_shuffle_epi8(b3, m);
                b4 = _mm_shuffle_epi8(b4, m);

                //XOR.
                b1 = _mm_xor_si128(b1, h1);
                b2 = _mm_xor_si128(b2, h2);
                b3 = _mm_xor_si128(b3, h3);
                b4 = _mm_xor_si128(b4, h4);

                b1 = _mm_xor_si128(b1, l1);
                b2 = _mm_xor_si128(b2, l2);
                b3 = _mm_xor_si128(b3, l3);
                b4 = _mm_xor_si128(b4, l4);

                buf += 64;
                len -= 64;
            }

            //Byte swap.
            b1 = _mm_shuffle_epi8(b1, m);
            b2 = _mm_shuffle_epi8(b2, m);
            b3 = _mm_shuffle_epi8(b3, m);
            b4 = _mm_shuffle_epi8(b4, m);
        }

        //Calculate the CRC of what's left using the table-based algorithm.
        crc = crc_bytes(params, 0, (unsigned char*) &b1, 16);
        crc = crc_bytes(params, crc, (unsigned char*) &b2, 16);
        crc = crc_bytes(params, crc, (unsigned char*) &b3, 16);
        crc = crc_bytes(params, crc, (unsigned char*) &b4, 16);
    }

    //Compute the remaining bytes and return the CRC.
    return crc_final(params, crc_bytes(params, crc, buf, len));
}