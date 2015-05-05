/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_DEBUG_BREAKPOINT_H_
#define XENIA_DEBUG_BREAKPOINT_H_

#include <string>

namespace xe {
namespace debug {

class Breakpoint {
 public:
  enum Type {
    TEMP_TYPE,
    CODE_TYPE,
  };

 public:
  Breakpoint(Type type, uint32_t address);
  ~Breakpoint();

  Type type() const { return type_; }
  uint32_t address() const { return address_; }

  const char* id() const { return id_.c_str(); }
  void set_id(const char* id) { id_ = std::string(id); }

 private:
  Type type_;
  uint32_t address_;

  std::string id_;
};

}  // namespace debug
}  // namespace xe

#endif  // XENIA_DEBUG_BREAKPOINT_H_
