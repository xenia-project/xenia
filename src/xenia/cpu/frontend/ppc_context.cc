/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/frontend/ppc_context.h"

#include <cinttypes>
#include <cstdlib>

#include "xenia/base/assert.h"
#include "xenia/base/string_util.h"

namespace xe {
namespace cpu {
namespace frontend {

std::string PPCContext::GetRegisterName(PPCRegister reg) {
  switch (reg) {
    case PPCRegister::kLR:
      return "lr";
    case PPCRegister::kCTR:
      return "ctr";
    case PPCRegister::kXER:
      return "xer";
    case PPCRegister::kFPSCR:
      return "fpscr";
    case PPCRegister::kVSCR:
      return "vscr";
    case PPCRegister::kCR:
      return "cr";
    default:
      if (static_cast<int>(reg) >= static_cast<int>(PPCRegister::kR0) &&
          static_cast<int>(reg) <= static_cast<int>(PPCRegister::kR31)) {
        return std::string("r") +
               std::to_string(static_cast<int>(reg) -
                              static_cast<int>(PPCRegister::kR0));
      } else if (static_cast<int>(reg) >= static_cast<int>(PPCRegister::kFR0) &&
                 static_cast<int>(reg) <=
                     static_cast<int>(PPCRegister::kFR31)) {
        return std::string("fr") +
               std::to_string(static_cast<int>(reg) -
                              static_cast<int>(PPCRegister::kFR0));
      } else if (static_cast<int>(reg) >= static_cast<int>(PPCRegister::kVR0) &&
                 static_cast<int>(reg) <=
                     static_cast<int>(PPCRegister::kVR128)) {
        return std::string("vr") +
               std::to_string(static_cast<int>(reg) -
                              static_cast<int>(PPCRegister::kVR0));
      } else {
        assert_unhandled_case(reg);
        return "?";
      }
  }
}

std::string PPCContext::GetStringFromValue(PPCRegister reg) const {
  switch (reg) {
    case PPCRegister::kLR:
      return string_util::to_hex_string(lr);
    case PPCRegister::kCTR:
      return string_util::to_hex_string(ctr);
    case PPCRegister::kXER:
      // return Int64ToHex(xer_ca);
      return "?";
    case PPCRegister::kFPSCR:
      // return Int64ToHex(fpscr);
      return "?";
    case PPCRegister::kVSCR:
      // return Int64ToHex(vscr_sat);
      return "?";
    case PPCRegister::kCR:
      // return Int64ToHex(cr);
      return "?";
    default:
      if (static_cast<int>(reg) >= static_cast<int>(PPCRegister::kR0) &&
          static_cast<int>(reg) <= static_cast<int>(PPCRegister::kR31)) {
        return string_util::to_hex_string(
            r[static_cast<int>(reg) - static_cast<int>(PPCRegister::kR0)]);
      } else if (static_cast<int>(reg) >= static_cast<int>(PPCRegister::kFR0) &&
                 static_cast<int>(reg) <=
                     static_cast<int>(PPCRegister::kFR31)) {
        return string_util::to_hex_string(
            f[static_cast<int>(reg) - static_cast<int>(PPCRegister::kFR0)]);
      } else if (static_cast<int>(reg) >= static_cast<int>(PPCRegister::kVR0) &&
                 static_cast<int>(reg) <=
                     static_cast<int>(PPCRegister::kVR128)) {
        return string_util::to_hex_string(
            v[static_cast<int>(reg) - static_cast<int>(PPCRegister::kVR0)]);
      } else {
        assert_unhandled_case(reg);
        return "";
      }
  }
}

void PPCContext::SetValueFromString(PPCRegister reg, std::string value) {
  // TODO(benvanik): set value from string to replace SetRegFromString?
  assert_always(false);
}

void PPCContext::SetRegFromString(const char* name, const char* value) {
  int n;
  if (sscanf(name, "r%d", &n) == 1) {
    this->r[n] = string_util::from_string<uint64_t>(value);
  } else if (sscanf(name, "f%d", &n) == 1) {
    this->f[n] = string_util::from_string<double>(value);
  } else if (sscanf(name, "v%d", &n) == 1) {
    this->v[n] = string_util::from_string<vec128_t>(value);
  } else {
    printf("Unrecognized register name: %s\n", name);
  }
}

bool PPCContext::CompareRegWithString(const char* name, const char* value,
                                      char* out_value,
                                      size_t out_value_size) const {
  int n;
  if (sscanf(name, "r%d", &n) == 1) {
    uint64_t expected = string_util::from_string<uint64_t>(value);
    if (this->r[n] != expected) {
      std::snprintf(out_value, out_value_size, "%016" PRIX64, this->r[n]);
      return false;
    }
    return true;
  } else if (sscanf(name, "f%d", &n) == 1) {
    double expected = string_util::from_string<double>(value);
    // TODO(benvanik): epsilon
    if (this->f[n] != expected) {
      std::snprintf(out_value, out_value_size, "%f", this->f[n]);
      return false;
    }
    return true;
  } else if (sscanf(name, "v%d", &n) == 1) {
    vec128_t expected = string_util::from_string<vec128_t>(value);
    if (this->v[n] != expected) {
      std::snprintf(out_value, out_value_size, "[%.8X, %.8X, %.8X, %.8X]",
                    this->v[n].i32[0], this->v[n].i32[1], this->v[n].i32[2],
                    this->v[n].i32[3]);
      return false;
    }
    return true;
  } else {
    assert_always("Unrecognized register name: %s\n", name);
    return false;
  }
}

}  // namespace frontend
}  // namespace cpu
}  // namespace xe
