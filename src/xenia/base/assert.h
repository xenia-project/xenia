/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_BASE_ASSERT_H_
#define XENIA_BASE_ASSERT_H_

#include <assert.h>

#include "xenia/base/platform.h"

namespace xe {

#define static_assert_size(type, size) \
  static_assert(sizeof(type) == size,  \
                "bad definition for " #type ": must be " #size " bytes")

// We rely on assert being compiled out in NDEBUG.
#if defined(NDEBUG)
#define xenia_assert static_cast<void>
#else
#define xenia_assert assert
#endif
#define __XENIA_EXPAND(x) x
#define __XENIA_ARGC(...)                                                     \
  __XENIA_EXPAND(__XENIA_ARGC_IMPL(__VA_ARGS__, 15, 14, 13, 12, 11, 10, 9, 8, \
                                   7, 6, 5, 4, 3, 2, 1, 0))
#define __XENIA_ARGC_IMPL(x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, \
                          x13, x14, x15, N, ...)                             \
  N
#define __XENIA_MACRO_DISPATCH(func, ...) \
  __XENIA_MACRO_DISPATCH_(func, __XENIA_ARGC(__VA_ARGS__))
#define __XENIA_MACRO_DISPATCH_(func, nargs) \
  __XENIA_MACRO_DISPATCH__(func, nargs)
#define __XENIA_MACRO_DISPATCH__(func, nargs) func##nargs

#define assert_always(...) xenia_assert(false)

#define assert_true(...) \
  __XENIA_MACRO_DISPATCH(assert_true, __VA_ARGS__)(__VA_ARGS__)
#define assert_true1(expr) xenia_assert(expr)
#define assert_true2(expr, message) xenia_assert((expr) || !message)

#define assert_false(...) \
  __XENIA_MACRO_DISPATCH(assert_false, __VA_ARGS__)(__VA_ARGS__)
#define assert_false1(expr) xenia_assert(!(expr))
#define assert_false2(expr, message) xenia_assert(!(expr) || !message)

#define assert_zero(...) \
  __XENIA_MACRO_DISPATCH(assert_zero, __VA_ARGS__)(__VA_ARGS__)
#define assert_zero1(expr) xenia_assert((expr) == 0)
#define assert_zero2(expr, message) xenia_assert((expr) == 0 || !message)

#define assert_not_zero(...) \
  __XENIA_MACRO_DISPATCH(assert_not_zero, __VA_ARGS__)(__VA_ARGS__)
#define assert_not_zero1(expr) xenia_assert((expr) != 0)
#define assert_not_zero2(expr, message) xenia_assert((expr) != 0 || !message)

#define assert_null(...) \
  __XENIA_MACRO_DISPATCH(assert_null, __VA_ARGS__)(__VA_ARGS__)
#define assert_null1(expr) xenia_assert((expr) == nullptr)
#define assert_null2(expr, message) xenia_assert((expr) == nullptr || !message)

#define assert_not_null(...) \
  __XENIA_MACRO_DISPATCH(assert_not_null, __VA_ARGS__)(__VA_ARGS__)
#define assert_not_null1(expr) xenia_assert((expr) != nullptr)
#define assert_not_null2(expr, message) \
  xenia_assert((expr) != nullptr || !message)

#define assert_unhandled_case(variable) \
  assert_always("unhandled switch(" #variable ") case")

}  // namespace xe

#endif  // XENIA_BASE_ASSERT_H_
