/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_MODULE_H_
#define XENIA_KERNEL_MODULE_H_

#include <xenia/core.h>

#include <xenia/kernel/export.h>
#include <xenia/kernel/xex2.h>

typedef struct {
  xechar_t    path[2048];
} xe_module_options_t;

struct xe_module;
typedef struct xe_module* xe_module_ref;


#define kXEPESectionContainsCode          0x00000020
#define kXEPESectionContainsDataInit      0x00000040
#define kXEPESectionContainsDataUninit    0x00000080
#define kXEPESectionMemoryExecute         0x20000000
#define kXEPESectionMemoryRead            0x40000000
#define kXEPESectionMemoryWrite           0x80000000
typedef struct {
  char        name[9];        // 8 + 1 for \0
  uint32_t    raw_address;
  size_t      raw_size;
  uint32_t    address;
  size_t      size;
  uint32_t    flags;          // kXEPESection*
} xe_module_pe_section_t;

typedef struct {
  uint32_t    address;
  size_t      total_length;   // in bytes
  size_t      prolog_length;  // in bytes
} xe_module_pe_method_info_t;


xe_module_ref xe_module_load(xe_memory_ref memory,
                             xe_kernel_export_resolver_ref export_resolver,
                             const void* addr, const size_t length,
                             xe_module_options_t options);
xe_module_ref xe_module_retain(xe_module_ref module);
void xe_module_release(xe_module_ref module);

const xechar_t *xe_module_get_path(xe_module_ref module);
const xechar_t *xe_module_get_name(xe_module_ref module);
uint32_t xe_module_get_handle(xe_module_ref module);
xe_xex2_ref xe_module_get_xex(xe_module_ref module);
const xe_xex2_header_t *xe_module_get_xex_header(xe_module_ref module);

void *xe_module_get_proc_address(xe_module_ref module, const uint32_t ordinal);

xe_module_pe_section_t *xe_module_get_section(xe_module_ref module,
                                              const char *name);
int xe_module_get_method_hints(xe_module_ref module,
                               xe_module_pe_method_info_t **out_method_infos,
                               size_t *out_method_info_count);

void xe_module_dump(xe_module_ref module);


#endif  // XENIA_KERNEL_MODULE_H_
