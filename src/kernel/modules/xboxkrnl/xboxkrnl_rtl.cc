/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "kernel/modules/xboxkrnl/xboxkrnl_rtl.h"

#include <xenia/kernel/xbox.h>
#include <xenia/kernel/xex2.h>

#include "kernel/shim_utils.h"


using namespace xe;
using namespace xe::kernel;
using namespace xe::kernel::xboxkrnl;


namespace {


void RtlImageXexHeaderField_shim(
    xe_ppc_state_t* ppc_state, KernelState* state) {
  // PVOID
  // PVOID XexHeaderBase
  // DWORD ImageField

  uint32_t xex_header_base    = SHIM_GET_ARG_32(0);
  uint32_t image_field        = SHIM_GET_ARG_32(1);

  // NOTE: this is totally faked!
  // We set the XexExecutableModuleHandle pointer to a block that has at offset
  // 0x58 a pointer to our XexHeaderBase. If the value passed doesn't match
  // then die.
  // The only ImageField I've seen in the wild is
  // 0x20401 (XEX_HEADER_DEFAULT_HEAP_SIZE), so that's all we'll support.

  XELOGD(
      XT("RtlImageXexHeaderField(%.8X, %.8X)"),
      xex_header_base, image_field);

  if (xex_header_base != 0x80101100) {
    XELOGE(XT("RtlImageXexHeaderField with non-magic base NOT IMPLEMENTED"));
    SHIM_SET_RETURN(0);
    return;
  }

  // TODO(benvanik): pull from xex header
  // module = GetExecutableModule() || (user defined one)
  // header = module->xex_header()
  // for (n = 0; n < header->header_count; n++) {
  //   if (header->headers[n].key == ImageField) {
  //     return value? or offset?
  //   }
  // }

  uint32_t return_value = 0;
  switch (image_field) {
    case XEX_HEADER_DEFAULT_HEAP_SIZE:
      // TODO(benvanik): pull from running module
      // This is header->exe_heap_size.
      //SHIM_SET_MEM_32(0x80101104, [some value]);
      //return_value = 0x80101104;
      return_value = 0;
      break;
    default:
      XELOGE(XT("RtlImageXexHeaderField header field %.8X NOT IMPLEMENTED"),
             image_field);
      SHIM_SET_RETURN(0);
      return;
  }

  SHIM_SET_RETURN(return_value);
}

// http://msdn.microsoft.com/en-us/library/ff561778
void RtlCompareMemory_shim(
    xe_ppc_state_t* ppc_state, KernelState* state) {
  // SIZE_T
  // _In_  const VOID *Source1,
  // _In_  const VOID *Source2,
  // _In_  SIZE_T Length

  uint32_t source1 = SHIM_GET_ARG_32(0);
  uint32_t source2 = SHIM_GET_ARG_32(1);
  uint32_t length = SHIM_GET_ARG_32(2);

  XELOGD(
      XT("RtlCompareMemory(%.8X, %.8X, %d)"),
      source1, source2, length);

  uint8_t* p1 = SHIM_MEM_ADDR(source1);
  uint8_t* p2 = SHIM_MEM_ADDR(source2);

  // Note that the return value is the number of bytes that match, so it's best
  // we just do this ourselves vs. using memcmp.
  // On Windows we could use the builtin function.

  uint32_t c = 0;
  for (uint32_t n = 0; n < length; n++, p1++, p2++) {
    if (*p1 == *p2) {
      c++;
    }
  }

  SHIM_SET_RETURN(c);
}

// http://msdn.microsoft.com/en-us/library/ff552123
void RtlCompareMemoryUlong_shim(
    xe_ppc_state_t* ppc_state, KernelState* state) {
  // SIZE_T
  // _In_  PVOID Source,
  // _In_  SIZE_T Length,
  // _In_  ULONG Pattern

  uint32_t source = SHIM_GET_ARG_32(0);
  uint32_t length = SHIM_GET_ARG_32(1);
  uint32_t pattern = SHIM_GET_ARG_32(2);

  XELOGD(
      XT("RtlCompareMemoryUlong(%.8X, %d, %.8X)"),
      source, length, pattern);

  if ((source % 4) || (length % 4)) {
    SHIM_SET_RETURN(0);
    return;
  }

  uint8_t* p = SHIM_MEM_ADDR(source);

  // Swap pattern.
  // TODO(benvanik): ensure byte order of pattern is correct.
  // Since we are doing byte-by-byte comparison we may not want to swap.
  // GET_ARG swaps, so this is a swap back. Ugly.
  const uint32_t pb32 = XESWAP32BE(pattern);
  const uint8_t* pb = (uint8_t*)&pb32;

  uint32_t c = 0;
  for (uint32_t n = 0; n < length; n++, p++) {
    if (*p == pb[n % 4]) {
      c++;
    }
  }

  SHIM_SET_RETURN(c);
}

// http://msdn.microsoft.com/en-us/library/ff552263
void RtlFillMemoryUlong_shim(
    xe_ppc_state_t* ppc_state, KernelState* state) {
  // VOID
  // _Out_  PVOID Destination,
  // _In_   SIZE_T Length,
  // _In_   ULONG Pattern

  uint32_t destination = SHIM_GET_ARG_32(0);
  uint32_t length = SHIM_GET_ARG_32(1);
  uint32_t pattern = SHIM_GET_ARG_32(2);

  XELOGD(
      XT("RtlFillMemoryUlong(%.8X, %d, %.8X)"),
      destination, length, pattern);

  // NOTE: length must be % 4, so we can work on uint32s.
  uint32_t* p = (uint32_t*)SHIM_MEM_ADDR(destination);

  // TODO(benvanik): ensure byte order is correct - we're writing back the
  // swapped arg value.

  for (uint32_t n = 0; n < length / 4; n++, p++) {
    *p = pattern;
  }
}



//RtlInitializeCriticalSection
//RtlEnterCriticalSection
//RtlLeaveCriticalSection

}


void xe::kernel::xboxkrnl::RegisterRtlExports(
    ExportResolver* export_resolver, KernelState* state) {
  #define SHIM_SET_MAPPING(ordinal, shim, impl) \
    export_resolver->SetFunctionMapping("xboxkrnl.exe", ordinal, \
        state, (xe_kernel_export_shim_fn)shim, (xe_kernel_export_impl_fn)impl)

  SHIM_SET_MAPPING(0x0000011A, RtlCompareMemory_shim, NULL);
  SHIM_SET_MAPPING(0x0000011B, RtlCompareMemoryUlong_shim, NULL);
  SHIM_SET_MAPPING(0x00000126, RtlFillMemoryUlong_shim, NULL);

  SHIM_SET_MAPPING(0x0000012B, RtlImageXexHeaderField_shim, NULL);

  #undef SET_MAPPING
}
