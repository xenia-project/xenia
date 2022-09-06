/**
******************************************************************************
* Xenia : Xbox 360 Emulator Research Project                                 *
******************************************************************************
* Copyright 2022 Ben Vanik. All rights reserved.                             *
* Released under the BSD license - see LICENSE in the root for more details. *
******************************************************************************
*/

#include "xenia/base/string.h"
#include "xenia/base/string_buffer.h"

#include "third_party/catch/include/catch.hpp"

namespace xe {
namespace base {
namespace test {

TEST_CASE("StringBuffer") {
  StringBuffer sb;
  uint32_t module_flags = 0x1000000;

  std::string path_(R"(\Device\Cdrom0\default.xex)");
  sb.AppendFormat("Module {}:\n", path_.c_str());
  REQUIRE(sb.to_string() == "Module \\Device\\Cdrom0\\default.xex:\n");
  sb.AppendFormat("    Module Flags: {:08X}\n", module_flags);
  REQUIRE(
      sb.to_string() ==
      "Module \\Device\\Cdrom0\\default.xex:\n    Module Flags: 01000000\n");
}

}  // namespace test
}  // namespace base
}  // namespace xe
