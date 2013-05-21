/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_LIBJIT_LIBJIT_LIBRARY_LOADER_H_
#define XENIA_CPU_LIBJIT_LIBJIT_LIBRARY_LOADER_H_

#include <xenia/common.h>
#include <xenia/core.h>

#include <xenia/cpu/library_loader.h>


namespace xe {
namespace cpu {
namespace libjit {


class LibjitLibraryLoader : public LibraryLoader {
public:
  LibjitLibraryLoader(xe_memory_ref memory,
                    kernel::ExportResolver* export_resolver);
  virtual ~LibjitLibraryLoader();
};


}  // namespace libjit
}  // namespace cpu
}  // namespace xe


#endif  // XENIA_CPU_LIBJIT_LIBJIT_LIBRARY_LOADER_H_
