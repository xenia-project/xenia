/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/testing/util.h"

#include <cfloat>

using namespace xe;
using namespace xe::cpu;
using namespace xe::cpu::hir;
using namespace xe::cpu::testing;
using xe::cpu::ppc::PPCContext;

TEST_CASE("INSERT_INT8", "[instr]") {
  for (int i = 0; i < 16; ++i) {
    TestFunction test([i](HIRBuilder& b) {
      StoreVR(b, 3,
              b.Insert(LoadVR(b, 4), b.LoadConstantInt32(i),
                       b.Truncate(LoadGPR(b, 5), INT8_TYPE)));
      b.Return();
    });
    test.Run(
        [i](PPCContext* ctx) {
          ctx->v[4] =
              vec128b(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
          ctx->r[4] = i;
          ctx->r[5] = 100 + i;
        },
        [i](PPCContext* ctx) {
          auto result = ctx->v[3];
          auto expected =
              vec128b(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
          expected.i8[i ^ 0x3] = 100 + i;
          REQUIRE(result == expected);
        });
  }
}

TEST_CASE("INSERT_INT16", "[instr]") {
  for (int i = 0; i < 8; ++i) {
    TestFunction test([i](HIRBuilder& b) {
      StoreVR(b, 3,
              b.Insert(LoadVR(b, 4), b.LoadConstantInt32(i),
                       b.Truncate(LoadGPR(b, 5), INT16_TYPE)));
      b.Return();
    });
    test.Run(
        [i](PPCContext* ctx) {
          ctx->v[4] = vec128s(0, 1, 2, 3, 4, 5, 6, 7);
          ctx->r[4] = i;
          ctx->r[5] = 100 + i;
        },
        [i](PPCContext* ctx) {
          auto result = ctx->v[3];
          auto expected = vec128s(0, 1, 2, 3, 4, 5, 6, 7);
          expected.i16[i ^ 0x1] = 100 + i;
          REQUIRE(result == expected);
        });
  }
}

TEST_CASE("INSERT_INT32", "[instr]") {
  for (int i = 0; i < 4; ++i) {
    TestFunction test([i](HIRBuilder& b) {
      StoreVR(b, 3,
              b.Insert(LoadVR(b, 4), b.LoadConstantInt32(i),
                       b.Truncate(LoadGPR(b, 5), INT32_TYPE)));
      b.Return();
    });
    test.Run(
        [i](PPCContext* ctx) {
          ctx->v[4] = vec128i(0, 1, 2, 3);
          ctx->r[4] = i;
          ctx->r[5] = 100 + i;
        },
        [i](PPCContext* ctx) {
          auto result = ctx->v[3];
          auto expected = vec128i(0, 1, 2, 3);
          expected.i32[i] = 100 + i;
          REQUIRE(result == expected);
        });
  }
}
