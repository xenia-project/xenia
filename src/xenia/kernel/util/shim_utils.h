/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_UTIL_SHIM_UTILS_H_
#define XENIA_KERNEL_UTIL_SHIM_UTILS_H_

#include "xenia/cpu/export_resolver.h"
#include "xenia/cpu/frontend/ppc_context.h"

namespace xe {
namespace kernel {

using PPCContext = xe::cpu::frontend::PPCContext;

#define SHIM_CALL void _cdecl
#define SHIM_SET_MAPPING(library_name, export_name, shim_data) \
  export_resolver->SetFunctionMapping(                         \
      library_name, ordinals::##export_name,                   \
      (xe::cpu::xe_kernel_export_shim_fn)export_name##_shim);

#define SHIM_MEM_BASE ppc_state->virtual_membase
#define SHIM_MEM_ADDR(a) (a ? (ppc_state->virtual_membase + a) : nullptr)

#define SHIM_MEM_8(a) xe::load_and_swap<uint8_t>(SHIM_MEM_ADDR(a))
#define SHIM_MEM_16(a) xe::load_and_swap<uint16_t>(SHIM_MEM_ADDR(a))
#define SHIM_MEM_32(a) xe::load_and_swap<uint32_t>(SHIM_MEM_ADDR(a))
#define SHIM_MEM_64(a) xe::load_and_swap<uint64_t>(SHIM_MEM_ADDR(a))
#define SHIM_SET_MEM_8(a, v) xe::store_and_swap<uint8_t>(SHIM_MEM_ADDR(a), v)
#define SHIM_SET_MEM_16(a, v) xe::store_and_swap<uint16_t>(SHIM_MEM_ADDR(a), v)
#define SHIM_SET_MEM_32(a, v) xe::store_and_swap<uint32_t>(SHIM_MEM_ADDR(a), v)
#define SHIM_SET_MEM_64(a, v) xe::store_and_swap<uint64_t>(SHIM_MEM_ADDR(a), v)

#define SHIM_GET_ARG_8(n) (uint8_t)(ppc_state->r[3 + n])
#define SHIM_GET_ARG_16(n) (uint16_t)(ppc_state->r[3 + n])
#define SHIM_GET_ARG_32(n) (uint32_t)(ppc_state->r[3 + n])
#define SHIM_GET_ARG_64(n) ppc_state->r[3 + n]
#define SHIM_SET_RETURN_32(v) ppc_state->r[3] = (uint64_t)((int32_t)v)

#define SHIM_STRUCT(type, address) \
  reinterpret_cast<type*>(SHIM_MEM_ADDR(address))

}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_UTIL_SHIM_UTILS_H_
