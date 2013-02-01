/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_MODULES_XBOXKRNL_XMODULE_H_
#define XENIA_KERNEL_MODULES_XBOXKRNL_XMODULE_H_

#include "kernel/modules/xboxkrnl/xobject.h"

#include <vector>

#include <xenia/kernel/export.h>
#include <xenia/kernel/xbox.h>
#include <xenia/kernel/xex2.h>


namespace xe {
namespace kernel {
namespace xboxkrnl {


class XModule : public XObject {
public:
  XModule(KernelState* kernel_state, const char* path);
  virtual ~XModule();

  const char* path();
  const char* name();
  xe_xex2_ref xex();
  const xe_xex2_header_t* xex_header();

  X_STATUS LoadFromFile(const char* path);
  X_STATUS LoadFromMemory(const void* addr, const size_t length);

  X_STATUS GetSection(const char* name, uint32_t* out_data, uint32_t* out_size);
  void* GetProcAddressByOrdinal(uint16_t ordinal);

  X_STATUS Launch(uint32_t flags);

  void Dump();

private:
  int LoadPE();

  char            name_[256];
  char            path_[XE_MAX_PATH];

  xe_xex2_ref     xex_;
};


}  // namespace xboxkrnl
}  // namespace kernel
}  // namespace xe


#endif  // XENIA_KERNEL_MODULES_XBOXKRNL_XMODULE_H_
