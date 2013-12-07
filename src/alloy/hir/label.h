/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef ALLOY_HIR_LABEL_H_
#define ALLOY_HIR_LABEL_H_

#include <alloy/core.h>


namespace alloy {
namespace hir {

class Block;


class Label {
public:
  Block* block;
  Label* next;
  Label* prev;

  uint32_t  id;
  char*     name;

  void*     tag;
};


}  // namespace hir
}  // namespace alloy


#endif  // ALLOY_HIR_LABEL_H_
