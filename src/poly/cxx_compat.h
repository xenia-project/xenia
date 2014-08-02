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

#include <memory>

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

// C++11 alignas keyword.
// This will hopefully be coming soon, as most of the alignment spec is in the
// latest CTP.
#if XE_COMPILER_MSVC
#define alignas(N) __declspec(align(N))
#endif  // XE_COMPILER_MSVC

#if !XE_COMPILER_MSVC
// C++1y make_unique.
// http://herbsutter.com/2013/05/29/gotw-89-solution-smart-pointers/
// This is present in clang with -std=c++1y, but not otherwise.
namespace std {
template <typename T, typename... Args>
unique_ptr<T> make_unique(Args&&... args) {
  return unique_ptr<T>(new T(forward<Args>(args)...));
}
}  // namespace std
#endif  // !XE_COMPILER_MSVC

namespace poly {}  // namespace poly

#endif  // POLY_CXX_COMPAT_H_
