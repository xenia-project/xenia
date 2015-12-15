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

TEST_CASE("LOAD_VECTOR_SHL", "[instr]") {
  TestFunction test([](HIRBuilder& b) {
    StoreVR(b, 3, b.LoadVectorShl(b.Truncate(LoadGPR(b, 4), INT8_TYPE)));
    b.Return();
  });
  test.Run([](PPCContext* ctx) { ctx->r[4] = 0; },
           [](PPCContext* ctx) {
             auto result = ctx->v[3];
             REQUIRE(result == vec128b(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12,
                                       13, 14, 15));
           });
  test.Run([](PPCContext* ctx) { ctx->r[4] = 7; },
           [](PPCContext* ctx) {
             auto result = ctx->v[3];
             REQUIRE(result == vec128b(7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17,
                                       18, 19, 20, 21, 22));
           });
  test.Run([](PPCContext* ctx) { ctx->r[4] = 15; },
           [](PPCContext* ctx) {
             auto result = ctx->v[3];
             REQUIRE(result == vec128b(15, 16, 17, 18, 19, 20, 21, 22, 23, 24,
                                       25, 26, 27, 28, 29, 30));
           });
  test.Run([](PPCContext* ctx) { ctx->r[4] = 16; },
           [](PPCContext* ctx) {
             auto result = ctx->v[3];
             REQUIRE(result == vec128b(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12,
                                       13, 14, 15));
           });
}

TEST_CASE("LOAD_VECTOR_SHR", "[instr]") {
  TestFunction test([](HIRBuilder& b) {
    StoreVR(b, 3, b.LoadVectorShr(b.Truncate(LoadGPR(b, 4), INT8_TYPE)));
    b.Return();
  });
  test.Run([](PPCContext* ctx) { ctx->r[4] = 0; },
           [](PPCContext* ctx) {
             auto result = ctx->v[3];
             REQUIRE(result == vec128b(16, 17, 18, 19, 20, 21, 22, 23, 24, 25,
                                       26, 27, 28, 29, 30, 31));
           });
  test.Run([](PPCContext* ctx) { ctx->r[4] = 7; },
           [](PPCContext* ctx) {
             auto result = ctx->v[3];
             REQUIRE(result == vec128b(9, 10, 11, 12, 13, 14, 15, 16, 17, 18,
                                       19, 20, 21, 22, 23, 24));
           });
  test.Run([](PPCContext* ctx) { ctx->r[4] = 15; },
           [](PPCContext* ctx) {
             auto result = ctx->v[3];
             REQUIRE(result == vec128b(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12,
                                       13, 14, 15, 16));
           });
  test.Run([](PPCContext* ctx) { ctx->r[4] = 16; },
           [](PPCContext* ctx) {
             auto result = ctx->v[3];
             REQUIRE(result == vec128b(16, 17, 18, 19, 20, 21, 22, 23, 24, 25,
                                       26, 27, 28, 29, 30, 31));
           });
}
