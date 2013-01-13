/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/kernel.h>

#include "kernel/modules/modules.h"


typedef struct xe_kernel {
  xe_ref_t ref;

  xe_kernel_options_t options;

  xe_pal_ref      pal;
  xe_memory_ref   memory;
  xe_cpu_ref      cpu;
  xe_kernel_export_resolver_ref export_resolver;

  struct {
    xe_xam_ref        xam;
    xe_xbdm_ref       xbdm;
    xe_xboxkrnl_ref   xboxkrnl;
  } modules;
} xe_kernel_t;


xe_kernel_ref xe_kernel_create(xe_pal_ref pal, xe_cpu_ref cpu,
                               xe_kernel_options_t options) {
  xe_kernel_ref kernel = (xe_kernel_ref)xe_calloc(sizeof(xe_kernel));
  xe_ref_init((xe_ref)kernel);

  xe_copy_struct(&kernel->options, &options, sizeof(xe_kernel_options_t));

  kernel->pal = xe_pal_retain(pal);
  kernel->memory = xe_cpu_get_memory(cpu);
  kernel->cpu = xe_cpu_retain(cpu);
  kernel->export_resolver = xe_kernel_export_resolver_create();

  kernel->modules.xam =
      xe_xam_create(kernel->pal, kernel->memory, kernel->export_resolver);
  kernel->modules.xbdm =
      xe_xbdm_create(kernel->pal, kernel->memory, kernel->export_resolver);
  kernel->modules.xboxkrnl =
      xe_xboxkrnl_create(kernel->pal, kernel->memory, kernel->export_resolver);

  return kernel;
}

void xe_kernel_dealloc(xe_kernel_ref kernel) {
  xe_xboxkrnl_release(kernel->modules.xboxkrnl);
  xe_xbdm_release(kernel->modules.xbdm);
  xe_xam_release(kernel->modules.xam);

  xe_kernel_export_resolver_release(kernel->export_resolver);
  xe_cpu_release(kernel->cpu);
  xe_memory_release(kernel->memory);
  xe_pal_release(kernel->pal);
}

xe_kernel_ref xe_kernel_retain(xe_kernel_ref kernel) {
  xe_ref_retain((xe_ref)kernel);
  return kernel;
}

void xe_kernel_release(xe_kernel_ref kernel) {
  xe_ref_release((xe_ref)kernel, (xe_ref_dealloc_t)xe_kernel_dealloc);
}

xe_pal_ref xe_kernel_get_pal(xe_kernel_ref kernel) {
  return xe_pal_retain(kernel->pal);
}

xe_memory_ref xe_kernel_get_memory(xe_kernel_ref kernel) {
  return xe_memory_retain(kernel->memory);
}

xe_cpu_ref xe_kernel_get_cpu(xe_kernel_ref kernel) {
  return xe_cpu_retain(kernel->cpu);
}

const xechar_t *xe_kernel_get_command_line(xe_kernel_ref kernel) {
  return kernel->options.command_line;
}

xe_kernel_export_resolver_ref xe_kernel_get_export_resolver(
    xe_kernel_ref kernel) {
  return xe_kernel_export_resolver_retain(kernel->export_resolver);
}

xe_module_ref xe_kernel_load_module(xe_kernel_ref kernel,
                                    const xechar_t *path) {
  xe_module_ref module = xe_kernel_get_module(kernel, path);
  if (module) {
    return module;
  }

  // TODO(benvanik): map file from filesystem
  xe_mmap_ref mmap = xe_mmap_open(kernel->pal, kXEFileModeRead, path, 0, 0);
  if (!mmap) {
    return NULL;
  }
  void *addr = xe_mmap_get_addr(mmap);
  size_t length = xe_mmap_get_length(mmap);

  xe_module_options_t options;
  xe_zero_struct(&options, sizeof(xe_module_options_t));
  XEIGNORE(xestrcpy(options.path, XECOUNT(options.path), path));
  module = xe_module_load(kernel->memory, kernel->export_resolver,
                          addr, length, options);

  // TODO(benvanik): retain memory somehow? is it needed?
  xe_mmap_release(mmap);

  if (!module) {
    return NULL;
  }

  // Prepare the module.
  XEEXPECTZERO(xe_cpu_prepare_module(kernel->cpu, module,
                                     kernel->export_resolver));

  // Stash in modules list (takes reference).
  // TODO(benvanik): stash in list.
  return xe_module_retain(module);

XECLEANUP:
  xe_module_release(module);
  return NULL;
}

void xe_kernel_launch_module(xe_kernel_ref kernel, xe_module_ref module) {
  //const xe_xex2_header_t *xex_header = xe_module_get_xex_header(module);

  // TODO(benvanik): set as main module/etc
  // xekXexExecutableModuleHandle = xe_module_get_handle(module);

  // XEEXPECTTRUE(XECPUPrepareModule(XEGetCPU(), module->xex, module->pe, module->address_space, module->address_space_size));

  // Setup the heap (and TLS?).
  // xex_header->exe_heap_size;

  // Launch thread.
  // XHANDLE thread_handle;
  // XDWORD thread_id;
  // XBOOL result = xekExCreateThread(&thread_handle, xex_header->exe_stack_size, &thread_id, NULL, (void*)xex_header->exe_entry_point, NULL, 0);

  // Wait until thread completes.
  // XLARGE_INTEGER timeout = XINFINITE;
  // xekNtWaitForSingleObjectEx(thread_handle, TRUE, &timeout);
}

xe_module_ref xe_kernel_get_module(xe_kernel_ref kernel, const xechar_t *path) {
  return NULL;
}

void xe_kernel_unload_module(xe_kernel_ref kernel, xe_module_ref module) {
  //
}
