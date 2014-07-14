/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XBOXKRNL_XMODULE_H_
#define XENIA_KERNEL_XBOXKRNL_XMODULE_H_

#include <xenia/kernel/xobject.h>

#include <xenia/xbox.h>


namespace xe {
namespace kernel {


class XModule : public XObject {
public:
  XModule(KernelState* kernel_state, const char* path);
  virtual ~XModule();

  const char* path() const { return path_; }
  const char* name() const { return name_; }

  virtual void* GetProcAddressByOrdinal(uint16_t ordinal) = 0;
  virtual X_STATUS GetSection(
      const char* name,
      uint32_t* out_section_data, uint32_t* out_section_size);

protected:
  char            name_[256];
  char            path_[poly::max_path];
};


}  // namespace kernel
}  // namespace xe


#endif  // XENIA_KERNEL_XBOXKRNL_XMODULE_H_
