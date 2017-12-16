/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/testing/util.h"

using namespace xe;
using namespace xe::cpu;
using namespace xe::cpu::hir;
using namespace xe::cpu::testing;
using xe::cpu::ppc::PPCContext;

TEST_CASE("PERMUTE_V128_BY_INT32_CONSTANT", "[instr]") {
  {
    uint32_t mask = MakePermuteMask(0, 0, 0, 1, 0, 2, 0, 3);
    TestFunction([mask](HIRBuilder& b) {
      StoreVR(b, 3,
              b.Permute(b.LoadConstantUint32(mask), LoadVR(b, 4), LoadVR(b, 5),
                        INT32_TYPE));
      b.Return();
    })
        .Run(
            [](PPCContext* ctx) {
              ctx->v[4] = vec128i(0, 1, 2, 3);
              ctx->v[5] = vec128i(4, 5, 6, 7);
            },
            [](PPCContext* ctx) {
              auto result = ctx->v[3];
              REQUIRE(result == vec128i(0, 1, 2, 3));
            });
  }
  {
    uint32_t mask = MakePermuteMask(1, 0, 1, 1, 1, 2, 1, 3);
    TestFunction([mask](HIRBuilder& b) {
      StoreVR(b, 3,
              b.Permute(b.LoadConstantUint32(mask), LoadVR(b, 4), LoadVR(b, 5),
                        INT32_TYPE));
      b.Return();
    })
        .Run(
            [](PPCContext* ctx) {
              ctx->v[4] = vec128i(0, 1, 2, 3);
              ctx->v[5] = vec128i(4, 5, 6, 7);
            },
            [](PPCContext* ctx) {
              auto result = ctx->v[3];
              REQUIRE(result == vec128i(4, 5, 6, 7));
            });
  }
  {
    uint32_t mask = MakePermuteMask(0, 3, 0, 2, 0, 1, 0, 0);
    TestFunction([mask](HIRBuilder& b) {
      StoreVR(b, 3,
              b.Permute(b.LoadConstantUint32(mask), LoadVR(b, 4), LoadVR(b, 5),
                        INT32_TYPE));
      b.Return();
    })
        .Run(
            [](PPCContext* ctx) {
              ctx->v[4] = vec128i(0, 1, 2, 3);
              ctx->v[5] = vec128i(4, 5, 6, 7);
            },
            [](PPCContext* ctx) {
              auto result = ctx->v[3];
              REQUIRE(result == vec128i(3, 2, 1, 0));
            });
  }
  {
    uint32_t mask = MakePermuteMask(1, 3, 1, 2, 1, 1, 1, 0);
    TestFunction([mask](HIRBuilder& b) {
      StoreVR(b, 3,
              b.Permute(b.LoadConstantUint32(mask), LoadVR(b, 4), LoadVR(b, 5),
                        INT32_TYPE));
      b.Return();
    })
        .Run(
            [](PPCContext* ctx) {
              ctx->v[4] = vec128i(0, 1, 2, 3);
              ctx->v[5] = vec128i(4, 5, 6, 7);
            },
            [](PPCContext* ctx) {
              auto result = ctx->v[3];
              REQUIRE(result == vec128i(7, 6, 5, 4));
            });
  }
}

TEST_CASE("PERMUTE_V128_BY_V128", "[instr]") {
  TestFunction test([](HIRBuilder& b) {
    StoreVR(b, 3,
            b.Permute(LoadVR(b, 3), LoadVR(b, 4), LoadVR(b, 5), INT8_TYPE));
    b.Return();
  });
  test.Run(
      [](PPCContext* ctx) {
        ctx->v[3] =
            vec128b(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
        ctx->v[4] =
            vec128b(100, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
        ctx->v[5] = vec128b(16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28,
                            29, 30, 31);
      },
      [](PPCContext* ctx) {
        auto result = ctx->v[3];
        REQUIRE(result == vec128b(100, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12,
                                  13, 14, 15));
      });
  test.Run(
      [](PPCContext* ctx) {
        ctx->v[3] = vec128b(16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28,
                            29, 30, 31);
        ctx->v[4] =
            vec128b(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
        ctx->v[5] = vec128b(116, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28,
                            29, 30, 31);
      },
      [](PPCContext* ctx) {
        auto result = ctx->v[3];
        REQUIRE(result == vec128b(116, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26,
                                  27, 28, 29, 30, 31));
      });
  test.Run(
      [](PPCContext* ctx) {
        ctx->v[3] =
            vec128b(15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0);
        ctx->v[4] =
            vec128b(100, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
        ctx->v[5] = vec128b(16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28,
                            29, 30, 31);
      },
      [](PPCContext* ctx) {
        auto result = ctx->v[3];
        REQUIRE(result == vec128b(15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3,
                                  2, 1, 100));
      });
  test.Run(
      [](PPCContext* ctx) {
        ctx->v[3] = vec128b(31, 30, 29, 28, 27, 26, 25, 24, 23, 22, 21, 20, 19,
                            18, 17, 16);
        ctx->v[4] =
            vec128b(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
        ctx->v[5] = vec128b(16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28,
                            29, 30, 131);
      },
      [](PPCContext* ctx) {
        auto result = ctx->v[3];
        REQUIRE(result == vec128b(131, 30, 29, 28, 27, 26, 25, 24, 23, 22, 21,
                                  20, 19, 18, 17, 16));
      });
}
