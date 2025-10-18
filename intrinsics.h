static const unsigned char SWAP_TABLE[] = {15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0};

#if defined(__x86_64__) || defined(_M_AMD64)

    /* Just to clear my confusion around the selection/control bits of the PCLMULQDQ
       instruction. 1 picks out the 64 MSBs and 0 picks the least 64. The left control
       bit is for b and the right is for a */

    // #define CLMUL(a, b, ac, bc) _mm_clmulepi64_si128(a, b, (bc ? 0x10 : 0x00) | (ac ? 0x01 : 0x00))

    #include <emmintrin.h>
    #include <tmmintrin.h>
    #include <wmmintrin.h>

    typedef __m128i uint128_t;
    typedef __m128i uint8x16_t;

    #define SET(hi, lo) _mm_set_epi64x(hi, lo)
    #define GET_SWAP_TABLE() _mm_loadu_si128((__m128i*)SWAP_TABLE)
    #define LOAD(ptr) _mm_loadu_si128((__m128i*)(ptr))
    #define CLMUL_HI(a, b) _mm_clmulepi64_si128(a, b, 0x11)
    #define CLMUL_LO(a, b) _mm_clmulepi64_si128(a, b, 0x00)
    #define SWAP(x, tbl) _mm_shuffle_epi8(x, tbl)
    #define XOR(a, b) _mm_xor_si128(a, b)

#elif defined(__aarch64__) || defined(_M_ARM64)

    #include <arm_neon.h>

    typedef uint64x2_t uint128_t;

    #define SET(hi, lo) vsetq_lane_u64(hi, vsetq_lane_u64(lo, vdupq_n_u64(0), 0), 1)
    #define GET_SWAP_TABLE() vld1q_u8(SWAP_TABLE)
    #define LOAD(ptr) vld1q_u64((const uint64_t*)(ptr))
    #define CLMUL_HI(a, b) vreinterpretq_u64_p128(vmull_high_p64(vreinterpretq_p64_u64(a), vreinterpretq_p64_u64(b)))
    #define CLMUL_LO(a, b) vreinterpretq_u64_p128(vmull_p64(vgetq_lane_p64(vreinterpretq_p64_u64(a), 0), \
                                                            vgetq_lane_p64(vreinterpretq_p64_u64(b), 0)))
    #define SWAP(x, tbl) vreinterpretq_u64_u8(vqtbl1q_u8(x, tbl))
    #define XOR(a, b) veorq_u64(a, b)

#endif