/* cpu_features.h -- Processor features detection.
 *
 * Copyright 2018 The Chromium Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the Chromium source repository LICENSE file.
 */

#ifndef CPU_FEATURES_H
#define CPU_FEATURES_H

#include <stdbool.h>

#ifdef _MSC_VER
#define DLL_EXPORT __declspec(dllexport)
#else
#define DLL_EXPORT
#endif

DLL_EXPORT extern bool cpu_enable_simd;
DLL_EXPORT void cpu_check_features();

#endif