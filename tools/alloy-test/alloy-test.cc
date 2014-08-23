/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#define CATCH_CONFIG_RUNNER
#include <third_party/catch/single_include/catch.hpp>

#include <tools/alloy-test/test_util.h>

using namespace alloy;
using namespace alloy::hir;
using namespace alloy::runtime;
using alloy::frontend::ppc::PPCContext;

Value* LoadGPR(hir::HIRBuilder& b, int reg) {
  return b.LoadContext(offsetof(PPCContext, r) + reg * 8, INT64_TYPE);
}

void StoreGPR(hir::HIRBuilder& b, int reg, Value* value) {
  b.StoreContext(offsetof(PPCContext, r) + reg * 8, value);
}

TEST_CASE("ADD", "[instr]") {
  alloy::test::TestFunction test([](hir::HIRBuilder& b) {
    auto v = b.Add(LoadGPR(b, 4), LoadGPR(b, 5));
    StoreGPR(b, 3, v);
    b.Return();
  });

  test.Run([](PPCContext* ctx) {
             ctx->r[4] = 10;
             ctx->r[5] = 25;
           },
           [](PPCContext* ctx) {
             auto result = ctx->r[3];
             REQUIRE(result == 0x23);
           });
  test.Run([](PPCContext* ctx) {
             ctx->r[4] = 10;
             ctx->r[5] = 25;
           },
           [](PPCContext* ctx) {
             auto result = ctx->r[3];
             REQUIRE(result == 0x24);
           });
}

DEFINE_ENTRY_POINT(L"alloy-test", L"?", alloy::test::main);
