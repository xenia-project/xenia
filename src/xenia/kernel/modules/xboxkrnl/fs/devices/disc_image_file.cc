/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/kernel/modules/xboxkrnl/fs/devices/disc_image_file.h>

#include <xenia/kernel/modules/xboxkrnl/fs/devices/disc_image_entry.h>


using namespace xe;
using namespace xe::kernel;
using namespace xe::kernel::xboxkrnl;
using namespace xe::kernel::xboxkrnl::fs;


DiscImageFile::DiscImageFile(KernelState* kernel_state, DiscImageEntry* entry) :
    entry_(entry),
    XFile(kernel_state) {
}

DiscImageFile::~DiscImageFile() {
}
