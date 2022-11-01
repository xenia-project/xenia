/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_HIR_LABEL_H_
#define XENIA_CPU_HIR_LABEL_H_

namespace xe {
namespace cpu {
namespace hir {

class Block;

class Label {
 public:
  Block* block;
  Label* next;
  Label* prev;

  uint32_t id;
  char* name;

  void* tag;
  // just use stringification of label id
  // this will later be used as an input to xbyak. xbyak only accepts
  // std::string as a value, not passed by reference, so precomputing the
  // stringification does not help
  std::string GetIdString() {
	  return std::to_string(id);
  }
};

}  // namespace hir
}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_HIR_LABEL_H_
