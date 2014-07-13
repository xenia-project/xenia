/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef POLY_ASSERT_H_
#define POLY_ASSERT_H_

#include <assert.h>

#include <poly/config.h>
#include <poly/platform.h>

namespace poly {

#define static_assert_size(type, size) \
  static_assert(sizeof(type) == size,  \
                "bad definition for "## #type##": must be "## #size##" bytes")

// We rely on assert being compiled out in NDEBUG.
#define poly_assert assert

#define __POLY_EXPAND(x) x
#define __POLY_ARGC(...)                                                       \
  __POLY_EXPAND(__POLY_ARGC_IMPL(__VA_ARGS__, 15, 14, 13, 12, 11, 10, 9, 8, 7, \
                                 6, 5, 4, 3, 2, 1, 0))
#define __POLY_ARGC_IMPL(x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, \
                         x13, x14, x15, N, ...)                             \
  N
#define __POLY_MACRO_DISPATCH(func, ...) \
  __POLY_MACRO_DISPATCH_(func, __POLY_ARGC(__VA_ARGS__))
#define __POLY_MACRO_DISPATCH_(func, nargs) __POLY_MACRO_DISPATCH__(func, nargs)
#define __POLY_MACRO_DISPATCH__(func, nargs) func##nargs

#define assert_always(...) poly_assert(false)

#define assert_true(...) \
  __POLY_MACRO_DISPATCH(assert_true, __VA_ARGS__)(__VA_ARGS__)
#define assert_true1(expr) poly_assert(expr)
#define assert_true2(expr, message) poly_assert((expr) || !message)

#define assert_false(...) \
  __POLY_MACRO_DISPATCH(assert_false, __VA_ARGS__)(__VA_ARGS__)
#define assert_false1(expr) poly_assert(!(expr))
#define assert_false2(expr, message) poly_assert(!(expr) || !message)

#define assert_zero(...) \
  __POLY_MACRO_DISPATCH(assert_zero, __VA_ARGS__)(__VA_ARGS__)
#define assert_zero1(expr) poly_assert((expr) == 0)
#define assert_zero2(expr, message) poly_assert((expr) == 0 || !message)

#define assert_not_zero(...) \
  __POLY_MACRO_DISPATCH(assert_not_zero, __VA_ARGS__)(__VA_ARGS__)
#define assert_not_zero1(expr) poly_assert((expr) != 0)
#define assert_not_zero2(expr, message) poly_assert((expr) != 0 || !message)

#define assert_null(...) \
  __POLY_MACRO_DISPATCH(assert_null, __VA_ARGS__)(__VA_ARGS__)
#define assert_null1(expr) poly_assert((expr) == nullptr)
#define assert_null2(expr, message) poly_assert((expr) == nullptr || !message)

#define assert_not_null(...) \
  __POLY_MACRO_DISPATCH(assert_not_null, __VA_ARGS__)(__VA_ARGS__)
#define assert_not_null1(expr) poly_assert((expr) != nullptr)
#define assert_not_null2(expr, message) \
  poly_assert((expr) != nullptr || !message)

#define assert_unhandled_case(variable) \
  assert_always("unhandled switch(" #variable ") case")

}  // namespace poly

#endif  // POLY_ASSERT_H_
