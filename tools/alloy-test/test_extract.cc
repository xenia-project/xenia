/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <tools/alloy-test/util.h>

#include <cfloat>

using namespace alloy;
using namespace alloy::hir;
using namespace alloy::runtime;
using namespace alloy::test;
using alloy::frontend::ppc::PPCContext;

TEST_CASE("EXTRACT_INT8", "[instr]") {
  TestFunction test([](hir::HIRBuilder& b) {
    StoreGPR(b, 3, b.ZeroExtend(b.Extract(LoadVR(b, 4),
                                          b.Truncate(LoadGPR(b, 4), INT8_TYPE),
                                          INT8_TYPE),
                                INT64_TYPE));
    b.Return();
  });
  for (int i = 0; i < 16; ++i) {
    test.Run([i](PPCContext* ctx) {
               ctx->r[4] = i;
               ctx->v[4] = vec128b(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13,
                                   14, 15);
             },
             [i](PPCContext* ctx) {
               auto result = ctx->r[3];
               REQUIRE(result == i);
             });
  }
}

TEST_CASE("EXTRACT_INT8_CONSTANT", "[instr]") {
  for (int i = 0; i < 16; ++i) {
    TestFunction([i](hir::HIRBuilder& b) {
                   StoreGPR(b, 3,
                            b.ZeroExtend(
                                b.Extract(LoadVR(b, 4),
                                          b.LoadConstant(int8_t(i)), INT8_TYPE),
                                INT64_TYPE));
                   b.Return();
                 }).Run([i](PPCContext* ctx) {
                          ctx->r[4] = i;
                          ctx->v[4] = vec128b(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10,
                                              11, 12, 13, 14, 15);
                        },
                        [i](PPCContext* ctx) {
                          auto result = ctx->r[3];
                          REQUIRE(result == i);
                        });
  }
}

TEST_CASE("EXTRACT_INT16", "[instr]") {
  TestFunction test([](hir::HIRBuilder& b) {
    StoreGPR(b, 3, b.ZeroExtend(b.Extract(LoadVR(b, 4),
                                          b.Truncate(LoadGPR(b, 4), INT8_TYPE),
                                          INT16_TYPE),
                                INT64_TYPE));
    b.Return();
  });
  for (int i = 0; i < 8; ++i) {
    test.Run([i](PPCContext* ctx) {
               ctx->r[4] = i;
               ctx->v[4] = vec128s(0x0000, 0x1001, 0x2002, 0x3003, 0x4004,
                                   0x5005, 0x6006, 0x7007);
             },
             [i](PPCContext* ctx) {
               auto result = ctx->r[3];
               REQUIRE(result == (i | (i << 12)));
             });
  }
}

TEST_CASE("EXTRACT_INT16_CONSTANT", "[instr]") {
  for (int i = 0; i < 8; ++i) {
    TestFunction([i](hir::HIRBuilder& b) {
                   StoreGPR(b, 3,
                            b.ZeroExtend(b.Extract(LoadVR(b, 4),
                                                   b.LoadConstant(int8_t(i)),
                                                   INT16_TYPE),
                                         INT64_TYPE));
                   b.Return();
                 }).Run([i](PPCContext* ctx) {
                          ctx->r[4] = i;
                          ctx->v[4] = vec128s(0, 1, 2, 3, 4, 5, 6, 7);
                        },
                        [i](PPCContext* ctx) {
                          auto result = ctx->r[3];
                          REQUIRE(result == i);
                        });
  }
}

TEST_CASE("EXTRACT_INT32", "[instr]") {
  TestFunction test([](hir::HIRBuilder& b) {
    StoreGPR(b, 3, b.ZeroExtend(b.Extract(LoadVR(b, 4),
                                          b.Truncate(LoadGPR(b, 4), INT8_TYPE),
                                          INT32_TYPE),
                                INT64_TYPE));
    b.Return();
  });
  for (int i = 0; i < 4; ++i) {
    test.Run([i](PPCContext* ctx) {
               ctx->r[4] = i;
               ctx->v[4] = vec128i(0, 1, 2, 3);
             },
             [i](PPCContext* ctx) {
               auto result = ctx->r[3];
               REQUIRE(result == i);
             });
  }
}

TEST_CASE("EXTRACT_INT32_CONSTANT", "[instr]") {
  for (int i = 0; i < 4; ++i) {
    TestFunction([i](hir::HIRBuilder& b) {
                   StoreGPR(b, 3,
                            b.ZeroExtend(b.Extract(LoadVR(b, 4),
                                                   b.LoadConstant(int8_t(i)),
                                                   INT32_TYPE),
                                         INT64_TYPE));
                   b.Return();
                 }).Run([i](PPCContext* ctx) {
                          ctx->r[4] = i;
                          ctx->v[4] = vec128i(0, 1, 2, 3);
                        },
                        [i](PPCContext* ctx) {
                          auto result = ctx->r[3];
                          REQUIRE(result == i);
                        });
  }
}
