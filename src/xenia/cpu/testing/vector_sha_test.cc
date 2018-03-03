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

TEST_CASE("VECTOR_SHA_I8", "[instr]") {
  TestFunction test([](HIRBuilder& b) {
    StoreVR(b, 3, b.VectorSha(LoadVR(b, 4), LoadVR(b, 5), INT8_TYPE));
    b.Return();
  });
  test.Run(
      [](PPCContext* ctx) {
        ctx->v[4] = vec128b(0x7E, 0x7E, 0x7E, 0x7F, 0x80, 0xFF, 0x01, 0x12,
                            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00);
        ctx->v[5] =
            vec128b(0, 1, 2, 8, 4, 4, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
      },
      [](PPCContext* ctx) {
        auto result = ctx->v[3];
        REQUIRE(result == vec128b(0x7E, 0x3F, 0x1F, 0x7F, 0xF8, 0xFF, 0x00,
                                  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                  0x00, 0x00));
      });
}

TEST_CASE("VECTOR_SHA_I8_CONSTANT", "[instr]") {
  TestFunction test([](HIRBuilder& b) {
    StoreVR(
        b, 3,
        b.VectorSha(LoadVR(b, 4),
                    b.LoadConstantVec128(vec128b(0, 1, 2, 8, 4, 4, 6, 7, 8, 9,
                                                 10, 11, 12, 13, 14, 15)),
                    INT8_TYPE));
    b.Return();
  });
  test.Run(
      [](PPCContext* ctx) {
        ctx->v[4] = vec128b(0x7E, 0x7E, 0x7E, 0x7F, 0x80, 0xFF, 0x01, 0x12,
                            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00);
      },
      [](PPCContext* ctx) {
        auto result = ctx->v[3];
        REQUIRE(result == vec128b(0x7E, 0x3F, 0x1F, 0x7F, 0xF8, 0xFF, 0x00,
                                  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                  0x00, 0x00));
      });
}

TEST_CASE("VECTOR_SHA_I16", "[instr]") {
  TestFunction test([](HIRBuilder& b) {
    StoreVR(b, 3, b.VectorSha(LoadVR(b, 4), LoadVR(b, 5), INT16_TYPE));
    b.Return();
  });
  test.Run(
      [](PPCContext* ctx) {
        ctx->v[4] = vec128s(0x7FFE, 0x7FFE, 0x7FFE, 0x7FFF, 0x8000, 0xFFFF,
                            0x0001, 0x1234);
        ctx->v[5] = vec128s(0, 1, 8, 15, 15, 8, 1, 16);
      },
      [](PPCContext* ctx) {
        auto result = ctx->v[3];
        REQUIRE(result == vec128s(0x7FFE, 0x3FFF, 0x007F, 0x0000, 0xFFFF,
                                  0xFFFF, 0x0000, 0x1234));
      });
}

TEST_CASE("VECTOR_SHA_I16_CONSTANT", "[instr]") {
  TestFunction test([](HIRBuilder& b) {
    StoreVR(
        b, 3,
        b.VectorSha(LoadVR(b, 4),
                    b.LoadConstantVec128(vec128s(0, 1, 8, 15, 15, 8, 1, 16)),
                    INT16_TYPE));
    b.Return();
  });
  test.Run(
      [](PPCContext* ctx) {
        ctx->v[4] = vec128s(0x7FFE, 0x7FFE, 0x7FFE, 0x7FFF, 0x8000, 0xFFFF,
                            0x0001, 0x1234);
      },
      [](PPCContext* ctx) {
        auto result = ctx->v[3];
        REQUIRE(result == vec128s(0x7FFE, 0x3FFF, 0x007F, 0x0000, 0xFFFF,
                                  0xFFFF, 0x0000, 0x1234));
      });
}

TEST_CASE("VECTOR_SHA_I32", "[instr]") {
  TestFunction test([](HIRBuilder& b) {
    StoreVR(b, 3, b.VectorSha(LoadVR(b, 4), LoadVR(b, 5), INT32_TYPE));
    b.Return();
  });
  test.Run(
      [](PPCContext* ctx) {
        ctx->v[4] = vec128i(0x7FFFFFFE, 0x7FFFFFFE, 0x7FFFFFFE, 0x7FFFFFFF);
        ctx->v[5] = vec128i(0, 1, 16, 31);
      },
      [](PPCContext* ctx) {
        auto result = ctx->v[3];
        REQUIRE(result ==
                vec128i(0x7FFFFFFE, 0x3FFFFFFF, 0x00007FFF, 0x00000000));
      });
  test.Run(
      [](PPCContext* ctx) {
        ctx->v[4] = vec128i(0x80000000, 0xFFFFFFFF, 0x00000001, 0x12345678);
        ctx->v[5] = vec128i(31, 16, 1, 32);
      },
      [](PPCContext* ctx) {
        auto result = ctx->v[3];
        REQUIRE(result ==
                vec128i(0xFFFFFFFF, 0xFFFFFFFF, 0x00000000, 0x12345678));
      });
}

TEST_CASE("VECTOR_SHA_I32_CONSTANT", "[instr]") {
  TestFunction test([](HIRBuilder& b) {
    StoreVR(
        b, 3,
        b.VectorSha(LoadVR(b, 4), b.LoadConstantVec128(vec128i(0, 1, 16, 31)),
                    INT32_TYPE));
    StoreVR(
        b, 4,
        b.VectorSha(LoadVR(b, 5), b.LoadConstantVec128(vec128i(31, 16, 1, 32)),
                    INT32_TYPE));
    b.Return();
  });
  test.Run(
      [](PPCContext* ctx) {
        ctx->v[4] = vec128i(0x7FFFFFFE, 0x7FFFFFFE, 0x7FFFFFFE, 0x7FFFFFFF);
        ctx->v[5] = vec128i(0x80000000, 0xFFFFFFFF, 0x00000001, 0x12345678);
      },
      [](PPCContext* ctx) {
        auto result1 = ctx->v[3];
        REQUIRE(result1 ==
                vec128i(0x7FFFFFFE, 0x3FFFFFFF, 0x00007FFF, 0x00000000));
        auto result2 = ctx->v[4];
        REQUIRE(result2 ==
                vec128i(0xFFFFFFFF, 0xFFFFFFFF, 0x00000000, 0x12345678));
      });
}
