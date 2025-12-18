/* cpu_features.c -- Processor features detection.
 *
 * Copyright 2018 The Chromium Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the Chromium source repository LICENSE file.
 */

#include "cpu.h"

bool cpu_enable_simd = false;

#ifdef DISABLE_SIMD

void cpu_check_features() {}

#else

static void _cpu_check_features();

//----------------------------------------

/* Init once */

#ifdef _WIN32

#include <windows.h>

static INIT_ONCE cpu_check_inited_once = INIT_ONCE_STATIC_INIT;

static BOOL CALLBACK _cpu_check_features_forwarder(PINIT_ONCE once, PVOID param, PVOID* context) {
    _cpu_check_features();
    return TRUE;
}

void cpu_check_features() {
    InitOnceExecuteOnce(&cpu_check_inited_once, _cpu_check_features_forwarder, NULL, NULL);
}

#else

#include <pthread.h>

static pthread_once_t cpu_check_inited_once = PTHREAD_ONCE_INIT;

void cpu_check_features() {
    pthread_once(&cpu_check_inited_once, _cpu_check_features);
}

#endif

//----------------------------------------

/* Check CPU features */

//Check for availability of SSE4.2 and PCLMULQDQ intrinsics.
#if defined(__x86_64__) || defined(_M_AMD64)

#ifdef _MSC_VER
#include <intrin.h>
#else
#include <cpuid.h>
#endif

static void _cpu_check_features() {
    int abcd[4];

    #ifdef _MSC_VER
    __cpuid(abcd, 1);
    #else
    __cpuid(1, abcd[0], abcd[1], abcd[2], abcd[3]);
    #endif

    int x86_cpu_has_sse42 = abcd[2] & 0x100000;
    int x86_cpu_has_pclmulqdq = abcd[2] & 0x2;

    cpu_enable_simd = x86_cpu_has_sse42 && x86_cpu_has_pclmulqdq;
}

//Check for availability of the PMULL intrinsic.
#elif defined(__aarch64__) || defined(_M_ARM64)

#ifdef __ANDROID__
#include <cpu-features.h>
#elif __linux__
#include <asm/hwcap.h>
#include <sys/auxv.h>
#elif _WIN32
#include <windows.h>
#elif __APPLE__
#include <sys/sysctl.h>
#endif

static void _cpu_check_features() {
    #ifdef __ANDROID__
    cpu_enable_simd = android_getCpuFeatures() & ANDROID_CPU_ARM64_FEATURE_PMULL;
    #elif __linux__
    cpu_enable_simd = getauxval(AT_HWCAP) & HWCAP_PMULL;
    #elif _WIN32
    cpu_enable_simd = IsProcessorFeaturePresent(PF_ARM_V8_CRYPTO_INSTRUCTIONS_AVAILABLE);
    #elif __APPLE__
    int val = 0;
    size_t len = sizeof(val);
    cpu_enable_simd = sysctlbyname("hw.optional.arm.FEAT_PMULL", &val, &len, 0, 0) == 0 && val != 0;
    #endif
}

#endif

#endif