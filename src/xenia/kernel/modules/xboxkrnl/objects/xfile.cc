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
#include <xenia/kernel/modules/xboxkrnl/objects/xmodule.h>


using namespace xe;
using namespace xe::kernel;
using namespace xe::kernel::xboxkrnl;


XFile::XFile(KernelState* kernel_state) :
    XObject(kernel_state, kTypeFile) {
}

XFile::~XFile() {
}
