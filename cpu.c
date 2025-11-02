/* cpu_features.c -- Processor features detection.
 *
 * Copyright 2018 The Chromium Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the Chromium source repository LICENSE file.
 */

#include "cpu.h"

#ifndef CPU_NO_SIMD
    #if defined(__x86_64__) || defined(_M_AMD64)
        #if defined(_WIN32)
            #define X86_WINDOWS
        #else
            #define X86_NOT_WINDOWS
        #endif
    #elif defined(__aarch64__) || defined(_M_ARM64)
        #ifdef _WIN32
            #define ARMV8_OS_WINDOWS
        #elif __linux__
            #define ARMV8_OS_LINUX
        #elif __ANDROID__
            #define ARMV8_OS_ANDROID
        #elif __APPLE__
            #if TARGET_OS_IPHONE
                #define ARMV8_OS_IOS
            #elif TARGET_OS_MAC
                #define ARMV8_OS_MACOS
            #endif
        #endif
    #endif
#endif

#if defined(_MSC_VER)
    #include <intrin.h>
#elif defined(__x86_64__)
    #include <cpuid.h>
#endif

#if defined(ARMV8_OS_MACOS)
    /* Crypto extensions (crc32/pmull) are a baseline feature in ARMv8.1-A, and
     * OSX running on arm64 is new enough that these can be assumed without
     * runtime detection.
     */
    bool cpu_enable_simd = true;
#else
    bool cpu_enable_simd = false;
#endif

#ifndef CPU_NO_SIMD
    #if defined(ARMV8_OS_ANDROID) || defined(ARMV8_OS_LINUX) || defined(ARMV8_OS_IOS)
        #include <pthread.h>
    #endif

    #if defined(ARMV8_OS_ANDROID)
        #include <cpu-features.h>
    #elif defined(ARMV8_OS_LINUX)
        #include <asm/hwcap.h>
        #include <sys/auxv.h>
    #elif defined(ARMV8_OS_WINDOWS) || defined(X86_WINDOWS)
        #include <windows.h>
    #elif defined(ARMV8_OS_IOS)
        #include <sys/sysctl.h>
    #elif !defined(_MSC_VER)
        #include <pthread.h>
    #else
        #error cpu.c CPU feature detection in not defined for your platform
    #endif

    #if !defined(CPU_NO_SIMD) && !defined(ARMV8_OS_MACOS)
        static void _cpu_check_features();
    #endif

    #if defined(ARMV8_OS_ANDROID) || defined(ARMV8_OS_LINUX) || \
        defined(ARMV8_OS_MACOS) || defined(X86_NOT_WINDOWS) || \
        defined(ARMV8_OS_IOS)
        #if !defined(ARMV8_OS_MACOS)
            // _cpu_check_features() doesn't need to do anything on mac/arm since all
            // features are known at build time, so don't call it.
            // Do provide cpu_check_features() (with a no-op implementation) so that we
            // don't have to make all callers of it check for mac/arm.
            static pthread_once_t cpu_check_inited_once = PTHREAD_ONCE_INIT;
        #endif

        void cpu_check_features() {
            #if !defined(ARMV8_OS_MACOS)
                pthread_once(&cpu_check_inited_once, _cpu_check_features);
            #endif
        }

    #elif defined(ARMV8_OS_WINDOWS) || defined(X86_WINDOWS)
        static INIT_ONCE cpu_check_inited_once = INIT_ONCE_STATIC_INIT;
        static BOOL CALLBACK _cpu_check_features_forwarder(PINIT_ONCE once, PVOID param, PVOID* context) {
            _cpu_check_features();
            return TRUE;
        }

        void cpu_check_features() {
            InitOnceExecuteOnce(&cpu_check_inited_once, _cpu_check_features_forwarder, NULL, NULL);
        }
    #endif

    #if (defined(__aarch64__) || defined(_M_ARM64)) && !defined(ARMV8_OS_MACOS)
        /*
         * See http://bit.ly/2CcoEsr for run-time detection of ARM features and also
         * crbug.com/931275 for android_getCpuFeatures() use in the Android sandbox.
         */
        static void _cpu_check_features() {
            #if defined(ARMV8_OS_ANDROID)
                cpu_enable_simd = android_getCpuFeatures() & ANDROID_CPU_ARM64_FEATURE_PMULL;
            #elif defined(ARMV8_OS_LINUX)
                cpu_enable_simd = getauxval(AT_HWCAP) & HWCAP_PMULL;
            #elif defined(ARMV8_OS_WINDOWS)
                cpu_enable_simd = IsProcessorFeaturePresent(PF_ARM_V8_CRYPTO_INSTRUCTIONS_AVAILABLE);
            #elif defined(ARMV8_OS_IOS)
                // Determine what features are supported dynamically. This code is applicable to macOS
                // as well if we wish to do that dynamically on that platform in the future.
                // See https://developer.apple.com/documentation/kernel/1387446-sysctlbyname/determining_instruction_set_characteristics
                int val = 0;
                size_t len = sizeof(val);
                cpu_enable_simd = sysctlbyname("hw.optional.arm.FEAT_PMULL", &val, &len, 0, 0) == 0 && val != 0;
            #endif
        }

    #elif defined(X86_NOT_WINDOWS) || defined(X86_WINDOWS)
        /*
         * iOS@x86 (i.e. emulator) is another special case where we disable
         * SIMD optimizations.
         */

        /* On x86 we simply use a instruction to check the CPU features.
         * (i.e. CPUID).
         */
        static void _cpu_check_features() {
            int abcd[4];

            #ifdef _MSC_VER
                __cpuid(abcd, 1);
            #else
                __cpuid(1, abcd[0], abcd[1], abcd[2], abcd[3]);
            #endif

            int x86_cpu_has_ssse3 = abcd[2] & 0x200;
            int x86_cpu_has_pclmulqdq = abcd[2] & 0x2;

            cpu_enable_simd = x86_cpu_has_ssse3 && x86_cpu_has_pclmulqdq;
        }
    #endif // ARM | x86
#endif // NO SIMD CPU