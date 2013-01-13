/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "kernel/modules/xboxkrnl/xboxkrnl.h"

#include "kernel/modules/xboxkrnl/xboxkrnl_table.h"


struct xe_xboxkrnl {
  xe_ref_t ref;
};


xe_xboxkrnl_ref xe_xboxkrnl_create(
    xe_pal_ref pal, xe_memory_ref memory,
    xe_kernel_export_resolver_ref export_resolver) {
  xe_xboxkrnl_ref module = (xe_xboxkrnl_ref)xe_calloc(sizeof(xe_xboxkrnl));
  xe_ref_init((xe_ref)module);

  xe_kernel_export_resolver_register_table(export_resolver, "xboxkrnl.exe",
      xe_xboxkrnl_export_table, XECOUNT(xe_xboxkrnl_export_table));

  return module;
}

void xe_xboxkrnl_dealloc(xe_xboxkrnl_ref module) {
}

xe_xboxkrnl_ref xe_xboxkrnl_retain(xe_xboxkrnl_ref module) {
  xe_ref_retain((xe_ref)module);
  return module;
}

void xe_xboxkrnl_release(xe_xboxkrnl_ref module) {
  xe_ref_release((xe_ref)module, (xe_ref_dealloc_t)xe_xboxkrnl_dealloc);
}
