/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/kernel/export.h>


bool xe_kernel_export_is_implemented(const xe_kernel_export_t *kernel_export) {
  if (kernel_export->flags & kXEKernelExportFlagFunction) {
    if (kernel_export->function_data.impl) {
      return true;
    }
  } else if (kernel_export->flags & kXEKernelExportFlagVariable) {
    if (kernel_export->variable_data) {
      return true;
    }
  }
  return false;
}

typedef struct {
  char                name[32];
  xe_kernel_export_t  *exports;
  size_t              count;
} xe_kernel_export_table_t;
#define kXEKernelExportResolverTableMaxCount 8


struct xe_kernel_export_resolver {
  xe_ref_t ref;

  xe_kernel_export_table_t tables[kXEKernelExportResolverTableMaxCount];
  size_t table_count;
};


xe_kernel_export_resolver_ref xe_kernel_export_resolver_create() {
  xe_kernel_export_resolver_ref resolver = (xe_kernel_export_resolver_ref)
      xe_calloc(sizeof(xe_kernel_export_resolver));
  xe_ref_init((xe_ref)resolver);

  return resolver;
}

void xe_kernel_export_resolver_dealloc(xe_kernel_export_resolver_ref resolver) {
}

xe_kernel_export_resolver_ref xe_kernel_export_resolver_retain(
    xe_kernel_export_resolver_ref resolver) {
  xe_ref_retain((xe_ref)resolver);
  return resolver;
}

void xe_kernel_export_resolver_release(xe_kernel_export_resolver_ref resolver) {
  xe_ref_release((xe_ref)resolver,
                 (xe_ref_dealloc_t)xe_kernel_export_resolver_dealloc);
}

void xe_kernel_export_resolver_register_table(
    xe_kernel_export_resolver_ref resolver, const char *library_name,
    xe_kernel_export_t *exports, const size_t count) {
  XEASSERT(resolver->table_count + 1 < kXEKernelExportResolverTableMaxCount);
  xe_kernel_export_table_t *table = &resolver->tables[resolver->table_count++];
    XEIGNORE(xestrcpya(table->name, XECOUNT(table->name), library_name));
  table->exports = exports;
  table->count = count;
}

xe_kernel_export_t *xe_kernel_export_resolver_get_by_ordinal(
    xe_kernel_export_resolver_ref resolver, const char *library_name,
    const uint32_t ordinal) {
  // TODO(benvanik): binary search everything.
  for (size_t n = 0; n < resolver->table_count; n++) {
    const xe_kernel_export_table_t *table = &resolver->tables[n];
    if (!xestrcmpa(library_name, table->name)) {
      // TODO(benvanik): binary search table.
      for (size_t m = 0; m < table->count; m++) {
        if (table->exports[m].ordinal == ordinal) {
          return &table->exports[m];
        }
      }
    }
  }
  return NULL;
}

xe_kernel_export_t *xe_kernel_export_resolver_get_by_name(
    xe_kernel_export_resolver_ref resolver, const char *library_name,
    const char *name) {
  return NULL;
}
