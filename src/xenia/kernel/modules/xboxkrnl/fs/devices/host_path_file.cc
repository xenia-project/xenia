/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/kernel/modules/xboxkrnl/fs/devices/host_path_file.h>

#include <xenia/kernel/modules/xboxkrnl/fs/devices/host_path_entry.h>


using namespace xe;
using namespace xe::kernel;
using namespace xe::kernel::xboxkrnl;
using namespace xe::kernel::xboxkrnl::fs;


HostPathFile::HostPathFile(KernelState* kernel_state, HostPathEntry* entry) :
    entry_(entry),
    XFile(kernel_state) {
}

HostPathFile::~HostPathFile() {
}
