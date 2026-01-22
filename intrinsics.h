/* Defines equivalent macros for both Intel and Arm intrinsics. */

#ifndef INTRINSICS_H
#define INTRINSICS_H

#ifndef _MSC_VER
#define ALIGN_ARRAY __attribute__((aligned(16)))
#else
#define ALIGN_ARRAY __declspec(align(16))
#endif

const unsigned char ALIGN_ARRAY SWAP_TABLE[] = {15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0};

//----------------------------------------

#if defined(__x86_64__) || defined(_M_AMD64)

/* Just to clear my confusion around the selection/control bits of the PCLMUL
   intrinsic. 1 picks out the 64 MSBs and 0 picks the least 64. The high control
   bit is for b and the other is for a. */

// #define CLMUL(a, b, ac, bc) _mm_clmulepi64_si128(a, b, (bc ? 0x10 : 0x00) | (ac ? 0x01 : 0x00))

#include <nmmintrin.h> //SSE4.2
#include <wmmintrin.h> //AES + PCLMUL

typedef __m128i uint128_t;

//Create a 128-bit integer from two 64-bit integers.
#define intrin_set(hi, lo) _mm_set_epi64x(hi, lo)

//Extract a 64-bit integer from a 128-bit integer.
#define intrin_get(x, i) _mm_extract_epi64(x, i)

//Load 16 bytes from ptr into a 128-bit integer. Assumes that ptr is aligned on
//a 16 byte memory boundary.
#define intrin_load_le(ptr) _mm_load_si128((__m128i*)(ptr))

//Multiply the high 64-bits of two 128-bit integers.
#define intrin_clmul_hi(a, b) _mm_clmulepi64_si128(a, b, 0x11)

//Multiply the low 64-bits of two 128-bit integers.
#define intrin_clmul_lo(a, b) _mm_clmulepi64_si128(a, b, 0x00)

//Swap the endianess of a 128-bit integer.
#define intrin_swap(x) _mm_shuffle_epi8(x, _mm_load_si128((__m128i*)SWAP_TABLE))

//XOR two 128-bit integers.
#define intrin_xor(a, b) _mm_xor_si128(a, b)

//----------------------------------------

#elif defined(__aarch64__) || defined(_M_ARM64)

#include <arm_neon.h>

typedef uint64x2_t uint128_t;

//Create a 64x2 vector from two 64-bit integers.
#define intrin_set(hi, lo) vsetq_lane_u64(hi, vdupq_n_u64(lo), 1)

//Extract a 64-bit integer from a 64x2 vector.
#define intrin_get(x, i) vgetq_lane_u64(x, i)

//Load 16 bytes from ptr into a 64x2 vector.
#define intrin_load_le(ptr) vld1q_u64((uint64_t*)(ptr))

//Multiply the high lanes of two 64x2 vectors.
#define intrin_clmul_hi(a, b) vreinterpretq_u64_p128(vmull_high_p64(vreinterpretq_p64_u64(a), \
                                                                    vreinterpretq_p64_u64(b)))

//Multiply the low lanes of two 64x2 vectors.
//It gets ugly.
#ifndef _MSC_VER
#define intrin_clmul_lo(a, b) vreinterpretq_u64_p128(vmull_p64(vgetq_lane_p64(vreinterpretq_p64_u64(a), 0), \
                                                               vgetq_lane_p64(vreinterpretq_p64_u64(b), 0)))
#else
#define intrin_clmul_lo(a, b) vreinterpretq_u64_p128(vmull_p64(vreinterpret_p64_u64(vget_low_u64(a)), \
                                                               vreinterpret_p64_u64(vget_low_u64(b))))
#endif

//Swap the endianess of a 64x2 vector.
#define intrin_swap(x) vreinterpretq_u64_u8(vqtbl1q_u8(vreinterpretq_u8_u64(x), vld1q_u8(SWAP_TABLE)))

//XOR two 64x2 vectors.
#define intrin_xor(a, b) veorq_u64(a, b)

//----------------------------------------

#else
#error "Unsupported Architecture. Compile on x86-64 or aarch64 or use DISABLE_SIMD."
#endif

//XOR three registers.
//Not a real tri-xor.
#define intrin_tri_xor(a, b, c) intrin_xor(a, intrin_xor(b, c))

//Load 16 bytes in big endian.
#define intrin_load_bg(ptr) intrin_swap(intrin_load_le(ptr))

#endif