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

TEST_CASE("BYTE_SWAP_V128", "[instr]") {
  TestFunction([](HIRBuilder& b) {
    StoreVR(b, 3, b.ByteSwap(LoadVR(b, 4)));
    b.Return();
  })
      .Run(
          [](PPCContext* ctx) {
            ctx->v[4] =
                vec128b(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
          },
          [](PPCContext* ctx) {
            auto result = ctx->v[3];
            REQUIRE(result == vec128b(3, 2, 1, 0, 7, 6, 5, 4, 11, 10, 9, 8, 15,
                                      14, 13, 12));
          });
  TestFunction([](HIRBuilder& b) {
    StoreVR(b, 3, b.ByteSwap(LoadVR(b, 4)));
    b.Return();
  })
      .Run(
          [](PPCContext* ctx) {
            ctx->v[4] = vec128i(0x0C13100F, 0x0E0D0C0B, 0x0A000000, 0x00000000);
          },
          [](PPCContext* ctx) {
            auto result = ctx->v[3];
            REQUIRE(result ==
                    vec128i(0x0F10130C, 0x0B0C0D0E, 0x0000000A, 0x00000000));
          });
}
