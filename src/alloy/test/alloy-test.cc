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

#include <tools/alloy-test/util.h>

namespace alloy {
namespace test {

using alloy::frontend::ppc::PPCContext;
using alloy::runtime::Runtime;

int main(std::vector<std::wstring>& args) {
  std::vector<std::string> narrow_args;
  auto narrow_argv = new char* [args.size()];
  for (size_t i = 0; i < args.size(); ++i) {
    auto narrow_arg = poly::to_string(args[i]);
    narrow_argv[i] = const_cast<char*>(narrow_arg.data());
    narrow_args.push_back(std::move(narrow_arg));
  }
  int ret = Catch::Session().run(int(args.size()), narrow_argv);
  if (ret) {
#if XE_LIKE_WIN32
    // Visual Studio kills the console on shutdown, so prevent that.
    if (poly::debugging::IsDebuggerAttached()) {
      poly::debugging::Break();
    }
#endif  // XE_LIKE_WIN32
  }
  return ret;
}

}  // namespace test
}  // namespace alloy

DEFINE_ENTRY_POINT(L"alloy-test", L"?", alloy::test::main);
