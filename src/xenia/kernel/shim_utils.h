/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_SHIM_UTILS_H_
#define XENIA_KERNEL_SHIM_UTILS_H_

#include <xenia/common.h>
#include <xenia/core.h>

#include <xenia/cpu/ppc.h>
#include <xenia/kernel/export.h>
#include <xenia/kernel/kernel_module.h>


namespace xe {
namespace kernel {


#define SHIM_CALL             void _cdecl
#define SHIM_SET_MAPPING(library_name, export_name, shim_data) \
  export_resolver->SetFunctionMapping( \
      library_name, ordinals::##export_name, \
      shim_data, \
      (xe_kernel_export_shim_fn)export_name##_shim, \
      NULL);

#define SHIM_MEM_ADDR(a)      (a==NULL?NULL:(ppc_state->membase + a))

#define SHIM_MEM_16(a)        (uint16_t)XEGETUINT16BE(SHIM_MEM_ADDR(a));
#define SHIM_MEM_32(a)        (uint32_t)XEGETUINT32BE(SHIM_MEM_ADDR(a));
#define SHIM_SET_MEM_16(a, v) (*(uint16_t*)SHIM_MEM_ADDR(a)) = XESWAP16(v)
#define SHIM_SET_MEM_32(a, v) (*(uint32_t*)SHIM_MEM_ADDR(a)) = XESWAP32(v)

#define SHIM_GPR_32(n)        (uint32_t)(ppc_state->r[n])
#define SHIM_SET_GPR_32(n, v) ppc_state->r[n] = (uint64_t)((v) & UINT32_MAX)
#define SHIM_GPR_64(n)        ppc_state->r[n]
#define SHIM_SET_GPR_64(n, v) ppc_state->r[n] = (uint64_t)(v)

#define SHIM_GET_ARG_32(n)    SHIM_GPR_32(3 + n)
#define SHIM_GET_ARG_64(n)    SHIM_GPR_64(3 + n)
#define SHIM_SET_RETURN(v)    SHIM_SET_GPR_64(3, v)


#define IMPL_MEM_ADDR(a)      (a==0?NULL:xe_memory_addr(state->memory(), a))

#define IMPL_MEM_16(a)        (uint16_t)XEGETUINT16BE(IMPL_MEM_ADDR(a));
#define IMPL_MEM_32(a)        (uint32_t)XEGETUINT32BE(IMPL_MEM_ADDR(a));
#define IMPL_SET_MEM_16(a, v) (*(uint16_t*)IMPL_MEM_ADDR(a)) = XESWAP16(v)
#define IMPL_SET_MEM_32(a, v) (*(uint32_t*)IMPL_MEM_ADDR(a)) = XESWAP32(v)


}  // namespace kernel
}  // namespace xe


#endif  // XENIA_KERNEL_SHIM_UTILS_H_
