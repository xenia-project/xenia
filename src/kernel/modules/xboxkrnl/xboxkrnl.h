/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_MODULES_XBOXKRNL_H_
#define XENIA_KERNEL_MODULES_XBOXKRNL_H_

#include <xenia/common.h>
#include <xenia/core.h>

#include <xenia/kernel/export.h>


struct xe_xboxkrnl;
typedef struct xe_xboxkrnl* xe_xboxkrnl_ref;


xe_xboxkrnl_ref xe_xboxkrnl_create(
    xe_pal_ref pal, xe_memory_ref memory,
    xe_kernel_export_resolver_ref export_resolver);

xe_xboxkrnl_ref xe_xboxkrnl_retain(xe_xboxkrnl_ref module);
void xe_xboxkrnl_release(xe_xboxkrnl_ref module);


#endif  // XENIA_KERNEL_MODULES_XBOXKRNL_H_
