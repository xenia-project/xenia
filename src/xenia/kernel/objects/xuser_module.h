/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XBOXKRNL_XUSER_MODULE_H_
#define XENIA_KERNEL_XBOXKRNL_XUSER_MODULE_H_

#include <xenia/kernel/objects/xmodule.h>

#include <xenia/export_resolver.h>
#include <xenia/xbox.h>
#include <xenia/kernel/util/xex2.h>


namespace xe {
namespace kernel {


class XUserModule : public XModule {
public:
  XUserModule(KernelState* kernel_state, const char* path);
  virtual ~XUserModule();

  xe_xex2_ref xex();
  const xe_xex2_header_t* xex_header();

  X_STATUS LoadFromFile(const char* path);
  X_STATUS LoadFromMemory(const void* addr, const size_t length);

  virtual void* GetProcAddressByOrdinal(uint16_t ordinal);
  virtual X_STATUS GetSection(
      const char* name,
      uint32_t* out_section_data, uint32_t* out_section_size);

  X_STATUS Launch(uint32_t flags);

  void Dump();

private:
  int LoadPE();

  xe_xex2_ref     xex_;
};


}  // namespace kernel
}  // namespace xe


#endif  // XENIA_KERNEL_XBOXKRNL_XUSER_MODULE_H_
