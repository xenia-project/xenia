/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XDB_MODULE_H_
#define XDB_MODULE_H_

#include <cstdint>

namespace xdb {

class Module {
 public:
   uint16_t module_id;
   // info
};

}  // namespace xdb

#endif  // XDB_MODULE_H_
