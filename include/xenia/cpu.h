/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_H_
#define XENIA_CPU_H_

#include <xenia/common.h>
#include <xenia/core.h>

#include <xenia/kernel/module.h>


typedef struct {
  int reserved;
} xe_cpu_options_t;


struct xe_cpu;
typedef struct xe_cpu* xe_cpu_ref;


xe_cpu_ref xe_cpu_create(xe_pal_ref pal, xe_memory_ref memory,
                         xe_cpu_options_t options);
xe_cpu_ref xe_cpu_retain(xe_cpu_ref cpu);
void xe_cpu_release(xe_cpu_ref cpu);

xe_pal_ref xe_cpu_get_pal(xe_cpu_ref cpu);
xe_memory_ref xe_cpu_get_memory(xe_cpu_ref cpu);

int xe_cpu_prepare_module(xe_cpu_ref cpu, xe_module_ref module);

int xe_cpu_execute(xe_cpu_ref cpu, uint32_t address);

uint32_t xe_cpu_create_callback(xe_cpu_ref cpu,
                                void (*callback)(void*), void *data);


#endif  // XENIA_CPU_H_
