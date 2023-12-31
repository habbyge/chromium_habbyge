// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "stdint.h"

#ifndef BUILD_RUST_TESTS_TEST_MIXED_COMPONENT_COMPONENT_H_
#define BUILD_RUST_TESTS_TEST_MIXED_COMPONENT_COMPONENT_H_

// The following is equivalent to //base/base_export.h.

#if defined(COMPONENT_BUILD)
#if defined(WIN32)

#if defined(COMPONENT_IMPLEMENTATION)
#define COMPONENT_EXPORT __declspec(dllexport)
#else
#define COMPONENT_EXPORT __declspec(dllimport)
#endif  // defined(COMPONENT_IMPLEMENTATION)

#else  // defined(WIN32)
#if defined(COMPONENT_IMPLEMENTATION)
#define COMPONENT_EXPORT __attribute__((visibility("default")))
#else
#define COMPONENT_EXPORT
#endif  // defined(COMPONENT_IMPLEMENTATION)
#endif

#else  // defined(COMPONENT_BUILD)
#define COMPONENT_EXPORT
#endif

COMPONENT_EXPORT uint32_t bilingual_math(uint32_t a, uint32_t b);

#endif  //  BUILD_RUST_TESTS_TEST_MIXED_COMPONENT_COMPONENT_H_
