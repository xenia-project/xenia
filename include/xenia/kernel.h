/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_H_
#define XENIA_KERNEL_H_

#include <xenia/common.h>
#include <xenia/core.h>

#include <xenia/kernel/export.h>
#include <xenia/kernel/module.h>
#include <xenia/kernel/xex2.h>


typedef struct {
  xechar_t    command_line[2048];
} xe_kernel_options_t;


struct xe_kernel;
typedef struct xe_kernel* xe_kernel_ref;


xe_kernel_ref xe_kernel_create(xe_pal_ref pal, xe_memory_ref memory,
                               xe_kernel_options_t options);
xe_kernel_ref xe_kernel_retain(xe_kernel_ref kernel);
void xe_kernel_release(xe_kernel_ref kernel);

xe_pal_ref xe_kernel_get_pal(xe_kernel_ref kernel);
xe_memory_ref xe_kernel_get_memory(xe_kernel_ref kernel);

const xechar_t *xe_kernel_get_command_line(xe_kernel_ref kernel);

xe_kernel_export_resolver_ref xe_kernel_get_export_resolver(
    xe_kernel_ref kernel);

xe_module_ref xe_kernel_load_module(xe_kernel_ref kernel, const xechar_t *path);
void xe_kernel_launch_module(xe_kernel_ref kernel, xe_module_ref module);
xe_module_ref xe_kernel_get_module(xe_kernel_ref kernel, const xechar_t *path);
void xe_kernel_unload_module(xe_kernel_ref kernel, xe_module_ref module);


#endif  // XENIA_KERNEL_H_
