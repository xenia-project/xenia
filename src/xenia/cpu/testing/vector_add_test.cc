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

TEST_CASE("VECTOR_ADD_I8", "[instr]") {
  TestFunction test([](HIRBuilder& b) {
    StoreVR(b, 3, b.VectorAdd(LoadVR(b, 4), LoadVR(b, 5), INT8_TYPE));
    b.Return();
  });
  test.Run(
      [](PPCContext* ctx) {
        ctx->v[4] =
            vec128b(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
        ctx->v[5] =
            vec128b(100, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
      },
      [](PPCContext* ctx) {
        auto result = ctx->v[3];
        REQUIRE(result == vec128b(100, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22,
                                  24, 26, 28, 30));
      });
  test.Run(
      [](PPCContext* ctx) {
        ctx->v[4] = vec128b(UINT8_MAX);
        ctx->v[5] = vec128b(1);
      },
      [](PPCContext* ctx) {
        auto result = ctx->v[3];
        REQUIRE(result == vec128b(0));
      });
}

TEST_CASE("VECTOR_ADD_I8_SAT_SIGNED", "[instr]") {
  TestFunction test([](HIRBuilder& b) {
    StoreVR(b, 3,
            b.VectorAdd(LoadVR(b, 4), LoadVR(b, 5), INT8_TYPE,
                        ARITHMETIC_SATURATE));
    b.Return();
  });
  test.Run(
      [](PPCContext* ctx) {
        ctx->v[4] = vec128b(INT8_MAX);
        ctx->v[5] = vec128b(1);
      },
      [](PPCContext* ctx) {
        auto result = ctx->v[3];
        REQUIRE(result == vec128b(INT8_MAX));
      });
  test.Run(
      [](PPCContext* ctx) {
        ctx->v[4] = vec128b(INT8_MIN);
        ctx->v[5] = vec128b(-1);
      },
      [](PPCContext* ctx) {
        auto result = ctx->v[3];
        REQUIRE(result == vec128b(INT8_MIN));
      });
}

TEST_CASE("VECTOR_ADD_I8_SAT_UNSIGNED", "[instr]") {
  TestFunction test([](HIRBuilder& b) {
    StoreVR(b, 3,
            b.VectorAdd(LoadVR(b, 4), LoadVR(b, 5), INT8_TYPE,
                        ARITHMETIC_SATURATE | ARITHMETIC_UNSIGNED));
    b.Return();
  });
  test.Run(
      [](PPCContext* ctx) {
        ctx->v[4] = vec128b(UINT8_MAX);
        ctx->v[5] = vec128b(1);
      },
      [](PPCContext* ctx) {
        auto result = ctx->v[3];
        REQUIRE(result == vec128b(UINT8_MAX));
      });
}

TEST_CASE("VECTOR_ADD_I16", "[instr]") {
  TestFunction test([](HIRBuilder& b) {
    StoreVR(b, 3, b.VectorAdd(LoadVR(b, 4), LoadVR(b, 5), INT16_TYPE));
    b.Return();
  });
  test.Run(
      [](PPCContext* ctx) {
        ctx->v[4] = vec128s(0, 1, 2, 3, 4, 5, 6, 7);
        ctx->v[5] = vec128s(100, 1, 2, 3, 4, 5, 6, 7);
      },
      [](PPCContext* ctx) {
        auto result = ctx->v[3];
        REQUIRE(result == vec128s(100, 2, 4, 6, 8, 10, 12, 14));
      });
  test.Run(
      [](PPCContext* ctx) {
        ctx->v[4] = vec128s(UINT16_MAX);
        ctx->v[5] = vec128s(1);
      },
      [](PPCContext* ctx) {
        auto result = ctx->v[3];
        REQUIRE(result == vec128s(0));
      });
  test.Run(
      [](PPCContext* ctx) {
        ctx->v[4] = vec128s(0);
        ctx->v[5] = vec128s(-1);
      },
      [](PPCContext* ctx) {
        auto result = ctx->v[3];
        REQUIRE(result == vec128s(UINT16_MAX));
      });
}

TEST_CASE("VECTOR_ADD_I16_SAT_SIGNED", "[instr]") {
  TestFunction test([](HIRBuilder& b) {
    StoreVR(b, 3,
            b.VectorAdd(LoadVR(b, 4), LoadVR(b, 5), INT16_TYPE,
                        ARITHMETIC_SATURATE));
    b.Return();
  });
  test.Run(
      [](PPCContext* ctx) {
        ctx->v[4] = vec128s(INT16_MAX);
        ctx->v[5] = vec128s(1);
      },
      [](PPCContext* ctx) {
        auto result = ctx->v[3];
        REQUIRE(result == vec128s(INT16_MAX));
      });
  test.Run(
      [](PPCContext* ctx) {
        ctx->v[4] = vec128s(INT16_MIN);
        ctx->v[5] = vec128s(-1);
      },
      [](PPCContext* ctx) {
        auto result = ctx->v[3];
        REQUIRE(result == vec128s(INT16_MIN));
      });
}

TEST_CASE("VECTOR_ADD_I16_SAT_UNSIGNED", "[instr]") {
  TestFunction test([](HIRBuilder& b) {
    StoreVR(b, 3,
            b.VectorAdd(LoadVR(b, 4), LoadVR(b, 5), INT16_TYPE,
                        ARITHMETIC_SATURATE | ARITHMETIC_UNSIGNED));
    b.Return();
  });
  test.Run(
      [](PPCContext* ctx) {
        ctx->v[4] = vec128s(UINT16_MAX);
        ctx->v[5] = vec128s(1);
      },
      [](PPCContext* ctx) {
        auto result = ctx->v[3];
        REQUIRE(result == vec128s(UINT16_MAX));
      });
}

TEST_CASE("VECTOR_ADD_I32", "[instr]") {
  TestFunction test([](HIRBuilder& b) {
    StoreVR(b, 3, b.VectorAdd(LoadVR(b, 4), LoadVR(b, 5), INT32_TYPE));
    b.Return();
  });
  test.Run(
      [](PPCContext* ctx) {
        ctx->v[4] = vec128i(0, 1, 2, 3);
        ctx->v[5] = vec128i(100, 1, 2, 3);
      },
      [](PPCContext* ctx) {
        auto result = ctx->v[3];
        REQUIRE(result == vec128i(100, 2, 4, 6));
      });
  test.Run(
      [](PPCContext* ctx) {
        ctx->v[4] = vec128i(UINT32_MAX);
        ctx->v[5] = vec128i(1);
      },
      [](PPCContext* ctx) {
        auto result = ctx->v[3];
        REQUIRE(result == vec128i(0));
      });
  test.Run(
      [](PPCContext* ctx) {
        ctx->v[4] = vec128i(0);
        ctx->v[5] = vec128i(-1);
      },
      [](PPCContext* ctx) {
        auto result = ctx->v[3];
        REQUIRE(result == vec128i(UINT32_MAX));
      });
}

TEST_CASE("VECTOR_ADD_I32_SAT_SIGNED", "[instr]") {
  TestFunction test([](HIRBuilder& b) {
    StoreVR(b, 3,
            b.VectorAdd(LoadVR(b, 4), LoadVR(b, 5), INT32_TYPE,
                        ARITHMETIC_SATURATE));
    b.Return();
  });
  test.Run(
      [](PPCContext* ctx) {
        ctx->v[4] = vec128i(5);
        ctx->v[5] = vec128i(5);
      },
      [](PPCContext* ctx) {
        auto result = ctx->v[3];
        REQUIRE(result == vec128i(10));
      });
  test.Run(
      [](PPCContext* ctx) {
        ctx->v[4] = vec128i(INT32_MAX);
        ctx->v[5] = vec128i(1);
      },
      [](PPCContext* ctx) {
        auto result = ctx->v[3];
        REQUIRE(result == vec128i(INT32_MAX));
      });
  test.Run(
      [](PPCContext* ctx) {
        ctx->v[4] = vec128i(INT32_MIN);
        ctx->v[5] = vec128i(-1);
      },
      [](PPCContext* ctx) {
        auto result = ctx->v[3];
        REQUIRE(result == vec128i(INT32_MIN));
      });
}

TEST_CASE("VECTOR_ADD_I32_SAT_UNSIGNED", "[instr]") {
  TestFunction test([](HIRBuilder& b) {
    StoreVR(b, 3,
            b.VectorAdd(LoadVR(b, 4), LoadVR(b, 5), INT32_TYPE,
                        ARITHMETIC_SATURATE | ARITHMETIC_UNSIGNED));
    b.Return();
  });
  test.Run(
      [](PPCContext* ctx) {
        ctx->v[4] = vec128i(5);
        ctx->v[5] = vec128i(5);
      },
      [](PPCContext* ctx) {
        auto result = ctx->v[3];
        REQUIRE(result == vec128i(10));
      });
  test.Run(
      [](PPCContext* ctx) {
        ctx->v[4] = vec128i(UINT32_MAX);
        ctx->v[5] = vec128i(1);
      },
      [](PPCContext* ctx) {
        auto result = ctx->v[3];
        REQUIRE(result == vec128i(UINT32_MAX));
      });
}

TEST_CASE("VECTOR_ADD_F32", "[instr]") {
  TestFunction test([](HIRBuilder& b) {
    StoreVR(b, 3, b.VectorAdd(LoadVR(b, 4), LoadVR(b, 5), FLOAT32_TYPE));
    b.Return();
  });
  test.Run(
      [](PPCContext* ctx) {
        ctx->v[4] = vec128f(0.12f, 0.34f, 0.56f, 0.78f);
        ctx->v[5] = vec128f(0.12f, 0.34f, 0.56f, 0.78f);
      },
      [](PPCContext* ctx) {
        auto result = ctx->v[3];
        REQUIRE(result ==
                vec128i(0x3E75C28F, 0x3F2E147B, 0x3F8F5C29, 0x3FC7AE14));
      });
  test.Run(
      [](PPCContext* ctx) {
        ctx->v[4] = vec128f(FLT_MAX);
        ctx->v[5] = vec128f(FLT_MAX);
      },
      [](PPCContext* ctx) {
        auto result = ctx->v[3];
        REQUIRE(result == vec128i(0x7F800000));
      });
  test.Run(
      [](PPCContext* ctx) {
        ctx->v[4] = vec128f(-FLT_MIN);
        ctx->v[5] = vec128f(-1.0f);
      },
      [](PPCContext* ctx) {
        auto result = ctx->v[3];
        REQUIRE(result == vec128i(0xBF800000));
      });
  test.Run(
      [](PPCContext* ctx) {
        ctx->v[4] = vec128f(FLT_MAX);
        ctx->v[5] = vec128f(1.0f);
      },
      [](PPCContext* ctx) {
        auto result = ctx->v[3];
        REQUIRE(result == vec128i(0x7F7FFFFF));
      });
}
