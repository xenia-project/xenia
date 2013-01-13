/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_MODULES_XAM_H_
#define XENIA_KERNEL_MODULES_XAM_H_

#include <xenia/common.h>
#include <xenia/core.h>

#include <xenia/kernel/export.h>


struct xe_xam;
typedef struct xe_xam* xe_xam_ref;


xe_xam_ref xe_xam_create(
    xe_pal_ref pal, xe_memory_ref memory,
    xe_kernel_export_resolver_ref export_resolver);

xe_xam_ref xe_xam_retain(xe_xam_ref module);
void xe_xam_release(xe_xam_ref module);


#endif  // XENIA_KERNEL_MODULES_XAM_H_
