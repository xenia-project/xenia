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

using namespace xe::cpu::hir;
using namespace xe::cpu;
using namespace xe::cpu::testing;
using xe::cpu::ppc::PPCContext;

TEST_CASE("DIV_I8", "[instr]") {
  TestFunction test([](HIRBuilder& b) {
    StoreGPR(b, 3,
             b.ZeroExtend(b.Div(b.Truncate(LoadGPR(b, 4), INT8_TYPE),
                                b.Truncate(LoadGPR(b, 5), INT8_TYPE)),
                          INT64_TYPE));
    b.Return();
  });
  test.Run(
      [](PPCContext* ctx) {
        ctx->r[4] = INT8_MIN;
        ctx->r[5] = -1;
      },
      [](PPCContext* ctx) {
        auto result = static_cast<int8_t>(ctx->r[3]);
        REQUIRE(result == 0);
      });

  test.Run(
      [](PPCContext* ctx) {
        ctx->r[4] = 15;
        ctx->r[5] = -1;
      },
      [](PPCContext* ctx) {
        auto result = static_cast<int8_t>(ctx->r[3]);
        REQUIRE(result == 15 / -1);
      });

  test.Run(
      [](PPCContext* ctx) {
        ctx->r[4] = 30;
        ctx->r[5] = 7;
      },
      [](PPCContext* ctx) {
        auto result = static_cast<int8_t>(ctx->r[3]);
        REQUIRE(result == 30 / 7);
      });
}

TEST_CASE("DIV_I16", "[instr]") {
  TestFunction test([](HIRBuilder& b) {
    StoreGPR(b, 3,
             b.ZeroExtend(b.Div(b.Truncate(LoadGPR(b, 4), INT16_TYPE),
                                b.Truncate(LoadGPR(b, 5), INT16_TYPE)),
                          INT64_TYPE));
    b.Return();
  });
  test.Run(
      [](PPCContext* ctx) {
        ctx->r[4] = INT16_MIN;
        ctx->r[5] = -1;
      },
      [](PPCContext* ctx) {
        auto result = static_cast<int16_t>(ctx->r[3]);
        REQUIRE(result == 0);
      });

  test.Run(
      [](PPCContext* ctx) {
        ctx->r[4] = 15;
        ctx->r[5] = -1;
      },
      [](PPCContext* ctx) {
        auto result = static_cast<int8_t>(ctx->r[3]);
        REQUIRE(result == 15 / -1);
      });

  test.Run(
      [](PPCContext* ctx) {
        ctx->r[4] = 30;
        ctx->r[5] = 7;
      },
      [](PPCContext* ctx) {
        auto result = static_cast<int8_t>(ctx->r[3]);
        REQUIRE(result == 30 / 7);
      });
}

TEST_CASE("DIV_I32", "[instr]") {
  TestFunction test([](HIRBuilder& b) {
    StoreGPR(b, 3,
             b.ZeroExtend(b.Div(b.Truncate(LoadGPR(b, 4), INT32_TYPE),
                                b.Truncate(LoadGPR(b, 5), INT32_TYPE)),
                          INT64_TYPE));
    b.Return();
  });
  test.Run(
      [](PPCContext* ctx) {
        ctx->r[4] = INT32_MIN;
        ctx->r[5] = -1;
      },
      [](PPCContext* ctx) {
        auto result = static_cast<int32_t>(ctx->r[3]);
        REQUIRE(result == 0);
      });

  test.Run(
      [](PPCContext* ctx) {
        ctx->r[4] = 15;
        ctx->r[5] = -1;
      },
      [](PPCContext* ctx) {
        auto result = static_cast<int8_t>(ctx->r[3]);
        REQUIRE(result == 15 / -1);
      });

  test.Run(
      [](PPCContext* ctx) {
        ctx->r[4] = 30;
        ctx->r[5] = 7;
      },
      [](PPCContext* ctx) {
        auto result = static_cast<int8_t>(ctx->r[3]);
        REQUIRE(result == 30 / 7);
      });
}

TEST_CASE("DIV_I64", "[instr]") {
  TestFunction test([](HIRBuilder& b) {
    StoreGPR(b, 3, b.Div(LoadGPR(b, 4), LoadGPR(b, 5)));
    b.Return();
  });
  test.Run(
      [](PPCContext* ctx) {
        ctx->r[4] = INT64_MIN;
        ctx->r[5] = -1;
      },
      [](PPCContext* ctx) {
        auto result = ctx->r[3];
        REQUIRE(result == 0);
      });

  test.Run(
      [](PPCContext* ctx) {
        ctx->r[4] = 15;
        ctx->r[5] = -1;
      },
      [](PPCContext* ctx) {
        auto result = static_cast<int8_t>(ctx->r[3]);
        REQUIRE(result == 15 / -1);
      });

  test.Run(
      [](PPCContext* ctx) {
        ctx->r[4] = 30;
        ctx->r[5] = 7;
      },
      [](PPCContext* ctx) {
        auto result = static_cast<int8_t>(ctx->r[3]);
        REQUIRE(result == 30 / 7);
      });
}
