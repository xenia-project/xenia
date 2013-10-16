/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/kernel/modules/xboxkrnl/objects/xfile.h>

#include <xenia/kernel/modules/xboxkrnl/objects/xevent.h>


using namespace xe;
using namespace xe::kernel;
using namespace xe::kernel::fs;
using namespace xe::kernel::xboxkrnl;


XFile::XFile(KernelState* kernel_state, FileEntry* entry) :
    entry_(entry),
    XObject(kernel_state, kTypeFile) {
}

XFile::~XFile() {
}
