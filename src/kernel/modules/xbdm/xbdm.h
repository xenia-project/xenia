/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_MODULES_XBDM_H_
#define XENIA_KERNEL_MODULES_XBDM_H_

#include <xenia/common.h>
#include <xenia/core.h>

#include <xenia/kernel/export.h>


struct xe_xbdm;
typedef struct xe_xbdm* xe_xbdm_ref;


xe_xbdm_ref xe_xbdm_create(
    xe_pal_ref pal, xe_memory_ref memory,
    xe_kernel_export_resolver_ref export_resolver);

xe_xbdm_ref xe_xbdm_retain(xe_xbdm_ref module);
void xe_xbdm_release(xe_xbdm_ref module);


#endif  // XENIA_KERNEL_MODULES_XBDM_H_
