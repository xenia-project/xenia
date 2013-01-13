/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_EXPORT_H_
#define XENIA_KERNEL_EXPORT_H_

#include <xenia/core.h>


typedef enum {
  kXEKernelExportFlagFunction   = 1 << 0,
  kXEKernelExportFlagVariable   = 1 << 1,
} xe_kernel_export_flags;

typedef void (*xe_kernel_export_fn)();

typedef struct {
  uint32_t      ordinal;
  uint32_t      flags;
  char          signature[16];
  char          name[96];

  union {
    // Variable data. Only valid when kXEKernelExportFlagVariable is set.
    void          *variable_data;

    struct {
      // Real function implementation (if present).
      xe_kernel_export_fn impl;

      // Shimmed implementation (call if param structs are big endian).
      // This may be NULL if no shim is needed or present.
      xe_kernel_export_fn shim;
    } function_data;
  };
} xe_kernel_export_t;

#define XE_DECLARE_EXPORT(module, ordinal, name, signature, flags) \
  { \
    ordinal, \
    flags, \
    #signature, \
    #name, \
  }


bool xe_kernel_export_is_implemented(const xe_kernel_export_t *kernel_export);


struct xe_kernel_export_resolver;
typedef struct xe_kernel_export_resolver* xe_kernel_export_resolver_ref;


xe_kernel_export_resolver_ref xe_kernel_export_resolver_create();
xe_kernel_export_resolver_ref xe_kernel_export_resolver_retain(
    xe_kernel_export_resolver_ref resolver);
void xe_kernel_export_resolver_release(xe_kernel_export_resolver_ref resolver);

void xe_kernel_export_resolver_register_table(
    xe_kernel_export_resolver_ref resolver, const char *library_name,
    xe_kernel_export_t *exports, const size_t count);

xe_kernel_export_t *xe_kernel_export_resolver_get_by_ordinal(
    xe_kernel_export_resolver_ref resolver, const char *library_name,
    const uint32_t ordinal);
xe_kernel_export_t *xe_kernel_export_resolver_get_by_name(
    xe_kernel_export_resolver_ref resolver, const char *library_name,
    const char *name);

#endif  // XENIA_KERNEL_EXPORT_H_
