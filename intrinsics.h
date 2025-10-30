/* Defines equivalent macros for both Intel and Arm intrinsics. */

#ifndef INTRINSICS_H
#define INTRINSICS_H

const unsigned char SWAP_TABLE[] = {15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0};

#if defined(__x86_64__) || defined(_M_AMD64)

/* Just to clear my confusion around the selection/control bits of the PCLMULQDQ
   instruction. 1 picks out the 64 MSBs and 0 picks the least 64. The high control
   bit is for b and the other is for a. */

// #define CLMUL(a, b, ac, bc) _mm_clmulepi64_si128(a, b, (bc ? 0x10 : 0x00) | (ac ? 0x01 : 0x00))

#include <emmintrin.h> //SSE2
#include <tmmintrin.h> //SSSE3
#include <wmmintrin.h> //AES + PCLMUL

typedef __m128i uint128_t;

//Redundant def so the same code would work on both architectures.
typedef __m128i table_t;

//Create a 128-bit integer from two 64-bit integers.
#define SET(hi, lo) _mm_set_epi64x(hi, lo)

//Table for _mm_shuffle_epi8 that swaps the endianess of a 128-bit integer.
#define GET_SWAP_TABLE() _mm_loadu_si128((__m128i*)SWAP_TABLE)

//Load 16 bytes from ptr into a 128-bit integer.
#define LOAD(ptr) _mm_loadu_si128((__m128i*)(ptr))

//Multiply the high 64-bits of two 128-bit integers.
#define CLMUL_HI(a, b) _mm_clmulepi64_si128(a, b, 0x11)

//Multiply the low 64-bits of two 128-bit integers.
#define CLMUL_LO(a, b) _mm_clmulepi64_si128(a, b, 0x00)

//Swap the endianess of a 128-bit integer.
#define SWAP(x, tbl) _mm_shuffle_epi8(x, tbl)

//XOR two 128-bit integers.
#define XOR(a, b) _mm_xor_si128(a, b)

#elif defined(__aarch64__) || defined(_M_ARM64)

#include <arm_neon.h>

typedef uint64x2_t uint128_t;
typedef uint8x16_t table_t;

//Create a 64x2 vector from two 64-bit integers.
#define SET(hi, lo) vsetq_lane_u64(hi, vsetq_lane_u64(lo, vdupq_n_u64(0), 0), 1)

//Table for vqtbl1q_u8 that swaps the endianess of a 64x2 vector.
#define GET_SWAP_TABLE() vld1q_u8(SWAP_TABLE)

//Load 16 bytes from ptr into a 64x2 vector.
#define LOAD(ptr) vld1q_u64((const uint64_t*)(ptr))

//Multiply the high lanes of two 64x2 vectors.
#define CLMUL_HI(a, b) vreinterpretq_u64_p128(vmull_high_p64(vreinterpretq_p64_u64(a), \
                                                             vreinterpretq_p64_u64(b)))

//Multiply the low lanes of two 64x2 vectors.
//It gets ugly.
#ifndef _MSC_VER
#define CLMUL_LO(a, b) vreinterpretq_u64_p128(vmull_p64(vgetq_lane_p64(vreinterpretq_p64_u64(a), 0), \
                                                        vgetq_lane_p64(vreinterpretq_p64_u64(b), 0)))
#else
#define CLMUL_LO(a, b) vreinterpretq_u64_p128(vmull_p64(vreinterpret_p64_u64(vget_low_u64(a)), \
                                                        vreinterpret_p64_u64(vget_low_u64(b))))
#endif

//Swap the endianess of a 64x2 vector.
#define SWAP(x, tbl) vreinterpretq_u64_u8(vqtbl1q_u8(vreinterpretq_u8_u64(x), tbl))

//XOR two 64x2 vectors.
#define XOR(a, b) veorq_u64(a, b)

#endif

#endif