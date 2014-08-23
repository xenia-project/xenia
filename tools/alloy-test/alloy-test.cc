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

TEST_CASE("Meta test", "[test]") {
  alloy::test::TestFunction test([](hir::HIRBuilder& b) {
    auto r = b.Add(b.LoadContext(offsetof(PPCContext, r) + 5 * 8, INT64_TYPE),
                   b.LoadContext(offsetof(PPCContext, r) + 25 * 8, INT64_TYPE));
    b.StoreContext(offsetof(PPCContext, r) + 11 * 8, r);
    b.Return();
    return true;
  });

  test.Run([](PPCContext* ctx) {
             ctx->r[5] = 10;
             ctx->r[25] = 25;
           },
           [](PPCContext* ctx) {
             auto result = ctx->r[11];
             REQUIRE(result == 0x23);
           });
  test.Run([](PPCContext* ctx) {
             ctx->r[5] = 10;
             ctx->r[25] = 25;
           },
           [](PPCContext* ctx) {
             auto result = ctx->r[11];
             REQUIRE(result == 0x24);
           });
}

DEFINE_ENTRY_POINT(L"alloy-test", L"?", alloy::test::main);
