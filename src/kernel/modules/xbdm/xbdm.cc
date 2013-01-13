/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "kernel/modules/xbdm/xbdm.h"

#include "kernel/modules/xbdm/xbdm_table.h"


struct xe_xbdm {
  xe_ref_t ref;
};


xe_xbdm_ref xe_xbdm_create(
    xe_pal_ref pal, xe_memory_ref memory,
    xe_kernel_export_resolver_ref export_resolver) {
  xe_xbdm_ref module = (xe_xbdm_ref)xe_calloc(sizeof(xe_xbdm));
  xe_ref_init((xe_ref)module);

  xe_kernel_export_resolver_register_table(export_resolver, "xbdm.exe",
      xe_xbdm_export_table, XECOUNT(xe_xbdm_export_table));

  return module;
}

void xe_xbdm_dealloc(xe_xbdm_ref module) {
}

xe_xbdm_ref xe_xbdm_retain(xe_xbdm_ref module) {
  xe_ref_retain((xe_ref)module);
  return module;
}

void xe_xbdm_release(xe_xbdm_ref module) {
  xe_ref_release((xe_ref)module, (xe_ref_dealloc_t)xe_xbdm_dealloc);
}
