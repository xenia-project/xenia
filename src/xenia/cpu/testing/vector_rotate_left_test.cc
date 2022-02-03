/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/testing/util.h"

using namespace xe;
using namespace xe::cpu;
using namespace xe::cpu::hir;
using namespace xe::cpu::testing;
using xe::cpu::ppc::PPCContext;

TEST_CASE("VECTOR_ROTATE_LEFT_I8", "[instr]") {
  TestFunction test([](HIRBuilder& b) {
    StoreVR(b, 3, b.VectorRotateLeft(LoadVR(b, 4), LoadVR(b, 5), INT8_TYPE));
    b.Return();
  });
  test.Run(
      [](PPCContext* ctx) {
        ctx->v[4] = vec128b(0b00000001);
        ctx->v[5] =
            vec128b(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
      },
      [](PPCContext* ctx) {
        auto result = ctx->v[3];
        REQUIRE(result ==
                vec128b(0b00000001, 0b00000010, 0b00000100, 0b00001000,
                        0b00010000, 0b00100000, 0b01000000, 0b10000000,
                        0b00000001, 0b00000010, 0b00000100, 0b00001000,
                        0b00010000, 0b00100000, 0b01000000, 0b10000000));
      });
}

TEST_CASE("VECTOR_ROTATE_LEFT_I16", "[instr]") {
  TestFunction test([](HIRBuilder& b) {
    StoreVR(b, 3, b.VectorRotateLeft(LoadVR(b, 4), LoadVR(b, 5), INT16_TYPE));
    b.Return();
  });
  test.Run(
      [](PPCContext* ctx) {
        ctx->v[4] = vec128s(0x0001, 0x0001, 0x0001, 0x0001, 0x1000, 0x1000,
                            0x1000, 0x1000);
        ctx->v[5] = vec128s(0, 1, 2, 3, 14, 15, 16, 17);
      },
      [](PPCContext* ctx) {
        auto result = ctx->v[3];
        REQUIRE(result == vec128s(0x0001, 0x0002, 0x0004, 0x0008, 0x0400,
                                  0x0800, 0x1000, 0x2000));
      });
}

TEST_CASE("VECTOR_ROTATE_LEFT_I32", "[instr]") {
  TestFunction test([](HIRBuilder& b) {
    StoreVR(b, 3, b.VectorRotateLeft(LoadVR(b, 4), LoadVR(b, 5), INT32_TYPE));
    b.Return();
  });
  test.Run(
      [](PPCContext* ctx) {
        ctx->v[4] = vec128i(0x00000001, 0x00000001, 0x80000000, 0x80000000);
        ctx->v[5] = vec128i(0, 1, 33, 2);
      },
      [](PPCContext* ctx) {
        auto result = ctx->v[3];
        REQUIRE(result ==
                vec128i(0x00000001, 0x00000002, 0x00000001, 0x00000002));
      });
}
