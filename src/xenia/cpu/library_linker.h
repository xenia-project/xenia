/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_LIBRARY_LINKER_H_
#define XENIA_CPU_LIBRARY_LINKER_H_

#include <xenia/common.h>
#include <xenia/core.h>

#include <xenia/core/memory.h>
#include <xenia/kernel/export.h>


namespace xe {
namespace cpu {


class LibraryLinker {
public:
  virtual ~LibraryLinker() {
    xe_memory_release(memory_);
  }

protected:
  LibraryLinker(xe_memory_ref memory,
                kernel::ExportResolver* export_resolver) {
    memory_ = xe_memory_retain(memory);
    export_resolver_ = export_resolver;
  }

  xe_memory_ref           memory_;
  kernel::ExportResolver* export_resolver_;
};


}  // namespace cpu
}  // namespace xe


#endif  // XENIA_CPU_LIBRARY_LINKER_H_
