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

#include <string>

#include "xenia/cpu/export_resolver.h"
#include "xenia/kernel/objects/xmodule.h"
#include "xenia/kernel/util/xex2.h"
#include "xenia/xbox.h"

namespace xe {
namespace kernel {

class XUserModule : public XModule {
 public:
  XUserModule(KernelState* kernel_state, const char* path);
  ~XUserModule() override;

  xe_xex2_ref xex();
  const xe_xex2_header_t* xex_header();

  X_STATUS LoadFromFile(std::string path);
  X_STATUS LoadFromMemory(const void* addr, const size_t length);

  uint32_t GetProcAddressByOrdinal(uint16_t ordinal) override;
  uint32_t GetProcAddressByName(const char* name) override;
  X_STATUS GetSection(const char* name, uint32_t* out_section_data,
                      uint32_t* out_section_size) override;
  X_STATUS GetOptHeader(xe_xex2_header_keys key,
                        uint32_t* out_header_guest_ptr);

  X_STATUS Launch(uint32_t flags);

  void Dump();

 private:
  xe_xex2_ref xex_;
  uint32_t xex_header_;
};

}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XBOXKRNL_XUSER_MODULE_H_
