/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef POLY_CXX_COMPAT_H_
#define POLY_CXX_COMPAT_H_

#include <poly/config.h>

// C++11 thread local storage.
// http://en.cppreference.com/w/cpp/language/storage_duration
#if XE_COMPILER_MSVC
// VC++2014 may have this.
#define _ALLOW_KEYWORD_MACROS 1
#define thread_local __declspec(thread)
#elif XE_LIKE_OSX
// Clang supports it on OSX but the runtime doesn't.
#define thread_local __thread
#endif  // XE_COMPILER_MSVC

namespace poly {}  // namespace poly

#endif  // POLY_CXX_COMPAT_H_
