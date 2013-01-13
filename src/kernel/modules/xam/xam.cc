/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "kernel/modules/xam/xam.h"

#include "kernel/modules/xam/xam_table.h"


struct xe_xam {
  xe_ref_t ref;
};


xe_xam_ref xe_xam_create(
    xe_pal_ref pal, xe_memory_ref memory,
    xe_kernel_export_resolver_ref export_resolver) {
  xe_xam_ref module = (xe_xam_ref)xe_calloc(sizeof(xe_xam));
  xe_ref_init((xe_ref)module);

  xe_kernel_export_resolver_register_table(export_resolver, "xam.xex",
      xe_xam_export_table, XECOUNT(xe_xam_export_table));

  return module;
}

void xe_xam_dealloc(xe_xam_ref module) {
}

xe_xam_ref xe_xam_retain(xe_xam_ref module) {
  xe_ref_retain((xe_ref)module);
  return module;
}

void xe_xam_release(xe_xam_ref module) {
  xe_ref_release((xe_ref)module, (xe_ref_dealloc_t)xe_xam_dealloc);
}
