/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_RUNTIME_H_
#define XENIA_KERNEL_RUNTIME_H_

#include <xenia/common.h>
#include <xenia/core.h>
#include <xenia/cpu.h>

#include <xenia/kernel/export.h>
#include <xenia/kernel/xex2.h>
#include <xenia/kernel/fs/filesystem.h>


namespace xe {
namespace cpu {
  class Processor;
}
namespace kernel {
namespace xboxkrnl {
  class XboxkrnlModule;
}
namespace xam {
  class XamModule;
}
}
}


namespace xe {
namespace kernel {

class KernelModule;


class Runtime {
public:
  Runtime(xe_pal_ref pal, shared_ptr<cpu::Processor> processor,
          const xechar_t* command_line);
  ~Runtime();

  const xechar_t* command_line();

  xe_pal_ref pal();
  xe_memory_ref memory();
  shared_ptr<cpu::Processor> processor();
  shared_ptr<ExportResolver> export_resolver();
  shared_ptr<fs::FileSystem> filesystem();

  int LaunchXexFile(const xechar_t* path);
  int LaunchDiscImage(const xechar_t* path);

private:
  xechar_t        command_line_[XE_MAX_PATH];

  xe_pal_ref      pal_;
  xe_memory_ref   memory_;
  shared_ptr<cpu::Processor> processor_;
  shared_ptr<ExportResolver> export_resolver_;
  shared_ptr<fs::FileSystem> filesystem_;

  auto_ptr<xboxkrnl::XboxkrnlModule> xboxkrnl_;
  auto_ptr<xam::XamModule> xam_;
};


}  // namespace kernel
}  // namespace xe


#endif  // XENIA_KERNEL_RUNTIME_H_
