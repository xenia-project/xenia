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

TEST_CASE("ADD_I8", "[instr]") {
  TestFunction test([](HIRBuilder& b) {
    StoreGPR(b, 3,
             b.ZeroExtend(b.Add(b.Truncate(LoadGPR(b, 4), INT8_TYPE),
                                b.Truncate(LoadGPR(b, 5), INT8_TYPE)),
                          INT64_TYPE));
    b.Return();
  });
  test.Run(
      [](PPCContext* ctx) {
        ctx->r[4] = 0;
        ctx->r[5] = 0;
      },
      [](PPCContext* ctx) {
        auto result = static_cast<int8_t>(ctx->r[3]);
        REQUIRE(result == 0);
      });
  test.Run(
      [](PPCContext* ctx) {
        ctx->r[4] = 10;
        ctx->r[5] = 25;
      },
      [](PPCContext* ctx) {
        auto result = static_cast<int8_t>(ctx->r[3]);
        REQUIRE(result == 0x23);
      });
  test.Run(
      [](PPCContext* ctx) {
        ctx->r[4] = -10;
        ctx->r[5] = -5;
      },
      [](PPCContext* ctx) {
        auto result = static_cast<int8_t>(ctx->r[3]);
        REQUIRE(result == -15);
      });
  test.Run(
      [](PPCContext* ctx) {
        ctx->r[4] = INT8_MIN;
        ctx->r[5] = 0;
      },
      [](PPCContext* ctx) {
        auto result = static_cast<int8_t>(ctx->r[3]);
        REQUIRE(result == INT8_MIN);
      });
  test.Run(
      [](PPCContext* ctx) {
        ctx->r[4] = UINT8_MAX;
        ctx->r[5] = 0;
      },
      [](PPCContext* ctx) {
        auto result = static_cast<uint8_t>(ctx->r[3]);
        REQUIRE(result == UINT8_MAX);
      });
  test.Run(
      [](PPCContext* ctx) {
        ctx->r[4] = INT8_MIN;
        ctx->r[5] = -1;
      },
      [](PPCContext* ctx) {
        auto result = static_cast<int8_t>(ctx->r[3]);
        REQUIRE(result == INT8_MAX);
      });
  test.Run(
      [](PPCContext* ctx) {
        ctx->r[4] = UINT8_MAX;
        ctx->r[5] = 1;
      },
      [](PPCContext* ctx) {
        auto result = static_cast<int8_t>(ctx->r[3]);
        REQUIRE(result == 0);
      });
}

TEST_CASE("ADD_I16", "[instr]") {
  TestFunction test([](HIRBuilder& b) {
    StoreGPR(b, 3,
             b.ZeroExtend(b.Add(b.Truncate(LoadGPR(b, 4), INT16_TYPE),
                                b.Truncate(LoadGPR(b, 5), INT16_TYPE)),
                          INT64_TYPE));
    b.Return();
  });
  test.Run(
      [](PPCContext* ctx) {
        ctx->r[4] = 0;
        ctx->r[5] = 0;
      },
      [](PPCContext* ctx) {
        auto result = static_cast<int16_t>(ctx->r[3]);
        REQUIRE(result == 0);
      });
  test.Run(
      [](PPCContext* ctx) {
        ctx->r[4] = 10;
        ctx->r[5] = 25;
      },
      [](PPCContext* ctx) {
        auto result = static_cast<int16_t>(ctx->r[3]);
        REQUIRE(result == 0x23);
      });
  test.Run(
      [](PPCContext* ctx) {
        ctx->r[4] = -10;
        ctx->r[5] = -5;
      },
      [](PPCContext* ctx) {
        auto result = static_cast<int16_t>(ctx->r[3]);
        REQUIRE(result == -15);
      });
  test.Run(
      [](PPCContext* ctx) {
        ctx->r[4] = INT16_MIN;
        ctx->r[5] = 0;
      },
      [](PPCContext* ctx) {
        auto result = static_cast<int16_t>(ctx->r[3]);
        REQUIRE(result == INT16_MIN);
      });
  test.Run(
      [](PPCContext* ctx) {
        ctx->r[4] = UINT16_MAX;
        ctx->r[5] = 0;
      },
      [](PPCContext* ctx) {
        auto result = static_cast<uint16_t>(ctx->r[3]);
        REQUIRE(result == UINT16_MAX);
      });
  test.Run(
      [](PPCContext* ctx) {
        ctx->r[4] = INT16_MIN;
        ctx->r[5] = -1;
      },
      [](PPCContext* ctx) {
        auto result = static_cast<int16_t>(ctx->r[3]);
        REQUIRE(result == INT16_MAX);
      });
  test.Run(
      [](PPCContext* ctx) {
        ctx->r[4] = UINT16_MAX;
        ctx->r[5] = 1;
      },
      [](PPCContext* ctx) {
        auto result = static_cast<int16_t>(ctx->r[3]);
        REQUIRE(result == 0);
      });
}

TEST_CASE("ADD_I32", "[instr]") {
  TestFunction test([](HIRBuilder& b) {
    StoreGPR(b, 3,
             b.ZeroExtend(b.Add(b.Truncate(LoadGPR(b, 4), INT32_TYPE),
                                b.Truncate(LoadGPR(b, 5), INT32_TYPE)),
                          INT64_TYPE));
    b.Return();
  });
  test.Run(
      [](PPCContext* ctx) {
        ctx->r[4] = 0;
        ctx->r[5] = 0;
      },
      [](PPCContext* ctx) {
        auto result = static_cast<int32_t>(ctx->r[3]);
        REQUIRE(result == 0);
      });
  test.Run(
      [](PPCContext* ctx) {
        ctx->r[4] = 10;
        ctx->r[5] = 25;
      },
      [](PPCContext* ctx) {
        auto result = static_cast<int32_t>(ctx->r[3]);
        REQUIRE(result == 0x23);
      });
  test.Run(
      [](PPCContext* ctx) {
        ctx->r[4] = -10;
        ctx->r[5] = -5;
      },
      [](PPCContext* ctx) {
        auto result = static_cast<int32_t>(ctx->r[3]);
        REQUIRE(result == -15);
      });
  test.Run(
      [](PPCContext* ctx) {
        ctx->r[4] = INT32_MIN;
        ctx->r[5] = 0;
      },
      [](PPCContext* ctx) {
        auto result = static_cast<int32_t>(ctx->r[3]);
        REQUIRE(result == INT32_MIN);
      });
  test.Run(
      [](PPCContext* ctx) {
        ctx->r[4] = UINT32_MAX;
        ctx->r[5] = 0;
      },
      [](PPCContext* ctx) {
        auto result = static_cast<uint32_t>(ctx->r[3]);
        REQUIRE(result == UINT32_MAX);
      });
  test.Run(
      [](PPCContext* ctx) {
        ctx->r[4] = INT32_MIN;
        ctx->r[5] = -1;
      },
      [](PPCContext* ctx) {
        auto result = static_cast<int32_t>(ctx->r[3]);
        REQUIRE(result == INT32_MAX);
      });
  test.Run(
      [](PPCContext* ctx) {
        ctx->r[4] = UINT32_MAX;
        ctx->r[5] = 1;
      },
      [](PPCContext* ctx) {
        auto result = static_cast<int32_t>(ctx->r[3]);
        REQUIRE(result == 0);
      });
}

TEST_CASE("ADD_I64", "[instr]") {
  TestFunction test([](HIRBuilder& b) {
    StoreGPR(b, 3, b.Add(LoadGPR(b, 4), LoadGPR(b, 5)));
    b.Return();
  });
  test.Run(
      [](PPCContext* ctx) {
        ctx->r[4] = 0;
        ctx->r[5] = 0;
      },
      [](PPCContext* ctx) {
        auto result = ctx->r[3];
        REQUIRE(result == 0);
      });
  test.Run(
      [](PPCContext* ctx) {
        ctx->r[4] = 10;
        ctx->r[5] = 25;
      },
      [](PPCContext* ctx) {
        auto result = ctx->r[3];
        REQUIRE(result == 0x23);
      });
  test.Run(
      [](PPCContext* ctx) {
        ctx->r[4] = -10;
        ctx->r[5] = -5;
      },
      [](PPCContext* ctx) {
        auto result = ctx->r[3];
        REQUIRE(result == -15);
      });
  test.Run(
      [](PPCContext* ctx) {
        ctx->r[4] = INT64_MIN;
        ctx->r[5] = 0;
      },
      [](PPCContext* ctx) {
        auto result = ctx->r[3];
        REQUIRE(result == INT64_MIN);
      });
  test.Run(
      [](PPCContext* ctx) {
        ctx->r[4] = UINT64_MAX;
        ctx->r[5] = 0;
      },
      [](PPCContext* ctx) {
        auto result = ctx->r[3];
        REQUIRE(result == UINT64_MAX);
      });
  test.Run(
      [](PPCContext* ctx) {
        ctx->r[4] = INT64_MIN;
        ctx->r[5] = -1;
      },
      [](PPCContext* ctx) {
        auto result = ctx->r[3];
        REQUIRE(result == INT64_MAX);
      });
  test.Run(
      [](PPCContext* ctx) {
        ctx->r[4] = UINT64_MAX;
        ctx->r[5] = 1;
      },
      [](PPCContext* ctx) {
        auto result = ctx->r[3];
        REQUIRE(result == 0);
      });
}

TEST_CASE("ADD_F32", "[instr]") {
  TestFunction test([](HIRBuilder& b) {
    StoreFPR(b, 3,
             b.Convert(b.Add(b.Convert(LoadFPR(b, 4), FLOAT32_TYPE),
                             b.Convert(LoadFPR(b, 5), FLOAT32_TYPE)),
                       FLOAT64_TYPE));
    b.Return();
  });
  test.Run(
      [](PPCContext* ctx) {
        ctx->f[4] = 0.0;
        ctx->f[5] = 0.0;
      },
      [](PPCContext* ctx) {
        auto result = ctx->f[3];
        REQUIRE(result == 0.0);
      });
  test.Run(
      [](PPCContext* ctx) {
        ctx->f[4] = 5.0;
        ctx->f[5] = 7.0;
      },
      [](PPCContext* ctx) {
        auto result = ctx->f[3];
        REQUIRE(result == 12.0);
      });
  test.Run(
      [](PPCContext* ctx) {
        ctx->f[4] = FLT_MAX / 2.0;
        ctx->f[5] = FLT_MAX / 2.0;
      },
      [](PPCContext* ctx) {
        auto result = ctx->f[3];
        REQUIRE(result == FLT_MAX);
      });
  test.Run(
      [](PPCContext* ctx) {
        ctx->f[4] = -100.0;
        ctx->f[5] = -150.0;
      },
      [](PPCContext* ctx) {
        auto result = ctx->f[3];
        REQUIRE(result == -250.0);
      });
  test.Run(
      [](PPCContext* ctx) {
        ctx->f[4] = FLT_MIN;
        ctx->f[5] = 0.0;
      },
      [](PPCContext* ctx) {
        auto result = ctx->f[3];
        REQUIRE(result == FLT_MIN);
      });
  test.Run(
      [](PPCContext* ctx) {
        ctx->f[4] = FLT_MAX;
        ctx->f[5] = 0.0;
      },
      [](PPCContext* ctx) {
        auto result = ctx->f[3];
        REQUIRE(result == FLT_MAX);
      });
}

TEST_CASE("ADD_F64", "[instr]") {
  TestFunction test([](HIRBuilder& b) {
    StoreFPR(b, 3, b.Add(LoadFPR(b, 4), LoadFPR(b, 5)));
    b.Return();
  });
  test.Run(
      [](PPCContext* ctx) {
        ctx->f[4] = 0.0;
        ctx->f[5] = 0.0;
      },
      [](PPCContext* ctx) {
        auto result = ctx->f[3];
        REQUIRE(result == 0.0);
      });
  test.Run(
      [](PPCContext* ctx) {
        ctx->f[4] = 5.0;
        ctx->f[5] = 7.0;
      },
      [](PPCContext* ctx) {
        auto result = ctx->f[3];
        REQUIRE(result == 12.0);
      });
  test.Run(
      [](PPCContext* ctx) {
        ctx->f[4] = DBL_MAX / 2.0;
        ctx->f[5] = DBL_MAX / 2.0;
      },
      [](PPCContext* ctx) {
        auto result = ctx->f[3];
        REQUIRE(result == DBL_MAX);
      });
  test.Run(
      [](PPCContext* ctx) {
        ctx->f[4] = -100.0;
        ctx->f[5] = -150.0;
      },
      [](PPCContext* ctx) {
        auto result = ctx->f[3];
        REQUIRE(result == -250.0);
      });
  test.Run(
      [](PPCContext* ctx) {
        ctx->f[4] = DBL_MIN;
        ctx->f[5] = 0.0;
      },
      [](PPCContext* ctx) {
        auto result = ctx->f[3];
        REQUIRE(result == DBL_MIN);
      });
  test.Run(
      [](PPCContext* ctx) {
        ctx->f[4] = DBL_MAX;
        ctx->f[5] = 0.0;
      },
      [](PPCContext* ctx) {
        auto result = ctx->f[3];
        REQUIRE(result == DBL_MAX);
      });
}
