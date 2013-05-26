/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/kernel/modules/xboxkrnl/xboxkrnl_rtl.h>

#include <xenia/kernel/shim_utils.h>
#include <xenia/kernel/xbox.h>
#include <xenia/kernel/xex2.h>
#include <xenia/kernel/modules/xboxkrnl/objects/xmodule.h>
#include <xenia/kernel/modules/xboxkrnl/objects/xthread.h>


using namespace xe;
using namespace xe::kernel;
using namespace xe::kernel::xboxkrnl;


namespace {


// http://msdn.microsoft.com/en-us/library/ff561778
SHIM_CALL RtlCompareMemory_shim(
    xe_ppc_state_t* ppc_state, KernelState* state) {
  // SIZE_T
  // _In_  const VOID *Source1,
  // _In_  const VOID *Source2,
  // _In_  SIZE_T Length

  uint32_t source1 = SHIM_GET_ARG_32(0);
  uint32_t source2 = SHIM_GET_ARG_32(1);
  uint32_t length = SHIM_GET_ARG_32(2);

  XELOGD(
      "RtlCompareMemory(%.8X, %.8X, %d)",
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
SHIM_CALL RtlCompareMemoryUlong_shim(
    xe_ppc_state_t* ppc_state, KernelState* state) {
  // SIZE_T
  // _In_  PVOID Source,
  // _In_  SIZE_T Length,
  // _In_  ULONG Pattern

  uint32_t source = SHIM_GET_ARG_32(0);
  uint32_t length = SHIM_GET_ARG_32(1);
  uint32_t pattern = SHIM_GET_ARG_32(2);

  XELOGD(
      "RtlCompareMemoryUlong(%.8X, %d, %.8X)",
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
SHIM_CALL RtlFillMemoryUlong_shim(
    xe_ppc_state_t* ppc_state, KernelState* state) {
  // VOID
  // _Out_  PVOID Destination,
  // _In_   SIZE_T Length,
  // _In_   ULONG Pattern

  uint32_t destination = SHIM_GET_ARG_32(0);
  uint32_t length = SHIM_GET_ARG_32(1);
  uint32_t pattern = SHIM_GET_ARG_32(2);

  XELOGD(
      "RtlFillMemoryUlong(%.8X, %d, %.8X)",
      destination, length, pattern);

  // NOTE: length must be % 4, so we can work on uint32s.
  uint32_t* p = (uint32_t*)SHIM_MEM_ADDR(destination);

  // TODO(benvanik): ensure byte order is correct - we're writing back the
  // swapped arg value.

  for (uint32_t n = 0; n < length / 4; n++, p++) {
    *p = pattern;
  }
}


// typedef struct _STRING {
//   USHORT Length;
//   USHORT MaximumLength;
//   PCHAR  Buffer;
// } ANSI_STRING, *PANSI_STRING;


// http://msdn.microsoft.com/en-us/library/ff561918
SHIM_CALL RtlInitAnsiString_shim(
    xe_ppc_state_t* ppc_state, KernelState* state) {
  // VOID
  // _Out_     PANSI_STRING DestinationString,
  // _In_opt_  PCSZ SourceString

  uint32_t destination_ptr = SHIM_GET_ARG_32(0);
  uint32_t source_ptr = SHIM_GET_ARG_32(1);

  const char* source = source_ptr ? (char*)SHIM_MEM_ADDR(source_ptr) : NULL;
  XELOGD("RtlInitAnsiString(%.8X, %.8X = %s)",
         destination_ptr, source_ptr, source ? source : "<null>");

  uint16_t length = source ? (uint16_t)xestrlena(source) : 0;
  SHIM_SET_MEM_16(destination_ptr + 0, length * 2);
  SHIM_SET_MEM_16(destination_ptr + 2, length * 2);
  SHIM_SET_MEM_32(destination_ptr + 4, source_ptr);
}


// http://msdn.microsoft.com/en-us/library/ff561899
SHIM_CALL RtlFreeAnsiString_shim(
    xe_ppc_state_t* ppc_state, KernelState* state) {
  // VOID
  // _Inout_  PANSI_STRING AnsiString

  uint32_t string_ptr = SHIM_GET_ARG_32(0);

  XELOGD("RtlFreeAnsiString(%.8X)", string_ptr);

  //uint32_t buffer = SHIM_MEM_32(string_ptr + 4);
  // TODO(benvanik): free the buffer
  XELOGE("RtlFreeAnsiString leaking buffer");

  SHIM_SET_MEM_16(string_ptr + 0, 0);
  SHIM_SET_MEM_16(string_ptr + 2, 0);
  SHIM_SET_MEM_32(string_ptr + 4, 0);
}


// typedef struct _UNICODE_STRING {
//   USHORT Length;
//   USHORT MaximumLength;
//   PWSTR  Buffer;
// } UNICODE_STRING, *PUNICODE_STRING;


// http://msdn.microsoft.com/en-us/library/ff561934
SHIM_CALL RtlInitUnicodeString_shim(
    xe_ppc_state_t* ppc_state, KernelState* state) {
  // VOID
  // _Out_     PUNICODE_STRING DestinationString,
  // _In_opt_  PCWSTR SourceString

  uint32_t destination_ptr = SHIM_GET_ARG_32(0);
  uint32_t source_ptr = SHIM_GET_ARG_32(1);

  const wchar_t* source =
      source_ptr ? (const wchar_t*)SHIM_MEM_ADDR(source_ptr) : NULL;
  XELOGD("RtlInitUnicodeString(%.8X, %.8X = %ls)",
         destination_ptr, source_ptr, source ? source : L"<null>");

  uint16_t length = source ? (uint16_t)xestrlenw(source) : 0;
  SHIM_SET_MEM_16(destination_ptr + 0, length * 2);
  SHIM_SET_MEM_16(destination_ptr + 2, length * 2);
  SHIM_SET_MEM_32(destination_ptr + 4, source_ptr);
}


// http://msdn.microsoft.com/en-us/library/ff561903
SHIM_CALL RtlFreeUnicodeString_shim(
    xe_ppc_state_t* ppc_state, KernelState* state) {
  // VOID
  // _Inout_  PUNICODE_STRING UnicodeString

  uint32_t string_ptr = SHIM_GET_ARG_32(0);

  XELOGD("RtlFreeUnicodeString(%.8X)", string_ptr);

  //uint32_t buffer = SHIM_MEM_32(string_ptr + 4);
  // TODO(benvanik): free the buffer
  XELOGE("RtlFreeUnicodeString leaking buffer");

  SHIM_SET_MEM_16(string_ptr + 0, 0);
  SHIM_SET_MEM_16(string_ptr + 2, 0);
  SHIM_SET_MEM_32(string_ptr + 4, 0);
}


// http://msdn.microsoft.com/en-us/library/ff562969
SHIM_CALL RtlUnicodeStringToAnsiString_shim(
    xe_ppc_state_t* ppc_state, KernelState* state) {
  // NTSTATUS
  // _Inout_  PANSI_STRING DestinationString,
  // _In_     PCUNICODE_STRING SourceString,
  // _In_     BOOLEAN AllocateDestinationString

  uint32_t destination_ptr = SHIM_GET_ARG_32(0);
  uint32_t source_ptr = SHIM_GET_ARG_32(1);
  uint32_t alloc_dest = SHIM_GET_ARG_32(2);

  XELOGD("RtlUnicodeStringToAnsiString(%.8X, %.8X, %d)",
         destination_ptr, source_ptr, alloc_dest);

  XELOGE("RtlUnicodeStringToAnsiString not yet implemented");

  if (alloc_dest) {
    // Allocate a new buffer to place the string into.
    //SHIM_SET_MEM_32(destination_ptr + 4, buffer_ptr);
  } else {
    // Reuse the buffer in the target.
    //uint32_t buffer_size = SHIM_MEM_16(destination_ptr + 2);
  }

  SHIM_SET_RETURN(X_STATUS_UNSUCCESSFUL);
}


SHIM_CALL RtlImageXexHeaderField_shim(
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
      "RtlImageXexHeaderField(%.8X, %.8X)",
      xex_header_base, image_field);

  if (xex_header_base != 0x80101100) {
    XELOGE("RtlImageXexHeaderField with non-magic base NOT IMPLEMENTED");
    SHIM_SET_RETURN(0);
    return;
  }

  XModule* module = NULL;

  // TODO(benvanik): use xex_header_base to dereference this.
  // Right now we are only concerned with games making this call on their main
  // module, so this hack is fine.
  module = state->GetExecutableModule();

  uint32_t return_value = 0;
  const xe_xex2_header_t* xex_header = module->xex_header();
  for (size_t n = 0; n < xex_header->header_count; n++) {
    if (xex_header->headers[n].key == image_field) {
      return_value = xex_header->headers[n].value;
      break;
    }
  }

  SHIM_SET_RETURN(return_value);
}


// Unfortunately the Windows RTL_CRITICAL_SECTION object is bigger than the one
// on the 360 (32b vs. 28b). This means that we can't do in-place splatting of
// the critical sections. Also, the 360 never calls RtlDeleteCriticalSection
// so we can't clean up the native handles.
//
// Because of this, we reimplement it poorly. Hooray.
// We have 28b to work with so we need to be careful. We map our struct directly
// into guest memory, as it should be opaque and so long as our size is right
// the user code will never know.
//
// Ref: http://msdn.microsoft.com/en-us/magazine/cc164040.aspx
// Ref: http://svn.reactos.org/svn/reactos/trunk/reactos/lib/rtl/critical.c?view=markup


// This structure tries to match the one on the 360 as best I can figure out.
// Unfortunately some games have the critical sections pre-initialized in
// their embedded data and InitializeCriticalSection will never be called.
namespace {
#pragma pack(push, 1)
typedef struct {
  uint8_t     unknown00;
  uint8_t     spin_count_div_256; // * 256
  uint8_t     __padding[6];
  //uint32_t    unknown04; // maybe the handle to the event?
  uint32_t    unknown08;          // head of queue, pointing to this offset
  uint32_t    unknown0C;          // tail of queue?
  int32_t     lock_count;         // -1 -> 0 on first lock 0x10
  uint32_t    recursion_count;    //  0 -> 1 on first lock 0x14
  uint32_t    owning_thread_id;   // 0 unless locked 0x18
} X_RTL_CRITICAL_SECTION;
#pragma pack(pop)
}

XESTATICASSERT(sizeof(X_RTL_CRITICAL_SECTION) == 28, "X_RTL_CRITICAL_SECTION must be 28 bytes");

SHIM_CALL RtlInitializeCriticalSection_shim(
    xe_ppc_state_t* ppc_state, KernelState* state) {
  // VOID
  // _Out_  LPCRITICAL_SECTION lpCriticalSection

  uint32_t cs_ptr = SHIM_GET_ARG_32(0);

  XELOGD("RtlInitializeCriticalSection(%.8X)", cs_ptr);

  X_RTL_CRITICAL_SECTION* cs = (X_RTL_CRITICAL_SECTION*)SHIM_MEM_ADDR(cs_ptr);
  cs->spin_count_div_256  = 0;
  cs->lock_count          = -1;
  cs->recursion_count     = 0;
  cs->owning_thread_id    = 0;
}


SHIM_CALL RtlInitializeCriticalSectionAndSpinCount_shim(
    xe_ppc_state_t* ppc_state, KernelState* state) {
  // NTSTATUS
  // _Out_  LPCRITICAL_SECTION lpCriticalSection,
  // _In_   DWORD dwSpinCount

  uint32_t cs_ptr = SHIM_GET_ARG_32(0);
  uint32_t spin_count = SHIM_GET_ARG_32(1);

  XELOGD("RtlInitializeCriticalSectionAndSpinCount(%.8X, %d)",
         cs_ptr, spin_count);

  // Spin count is rouned up to 256 intervals then packed in.
  //uint32_t spin_count_div_256 = (uint32_t)floor(spin_count / 256.0f + 0.5f);
  uint32_t spin_count_div_256 = (spin_count + 255) >> 8;
  if (spin_count_div_256 > 255)
  {
    spin_count_div_256 = 255;
  }

  X_RTL_CRITICAL_SECTION* cs = (X_RTL_CRITICAL_SECTION*)SHIM_MEM_ADDR(cs_ptr);
  cs->spin_count_div_256  = spin_count_div_256;
  cs->lock_count          = -1;
  cs->recursion_count     = 0;
  cs->owning_thread_id    = 0;

  SHIM_SET_RETURN(X_STATUS_SUCCESS);
}


SHIM_CALL RtlEnterCriticalSection_shim(
    xe_ppc_state_t* ppc_state, KernelState* state) {
  // VOID
  // _Inout_  LPCRITICAL_SECTION lpCriticalSection

  uint32_t cs_ptr = SHIM_GET_ARG_32(0);

  XELOGD("RtlEnterCriticalSection(%.8X)", cs_ptr);

  X_RTL_CRITICAL_SECTION* cs = (X_RTL_CRITICAL_SECTION*)SHIM_MEM_ADDR(cs_ptr);

  const uint8_t* thread_state_block = ppc_state->membase + ppc_state->r[13];
  uint32_t thread_id = XThread::GetCurrentThreadId(thread_state_block);
  uint32_t spin_wait_remaining = cs->spin_count_div_256 * 256;

spin:
  if (xe_atomic_inc_32(&cs->lock_count)) {
    // If this thread already owns the CS increment the recursion count.
    if (cs->owning_thread_id == thread_id) {
      cs->recursion_count++;
      return;
    }

    // Thread was locked - spin wait.
    if (spin_wait_remaining) {
      spin_wait_remaining--;
      goto spin;
    }

    // All out of spin waits, create a full waiter.
    // TODO(benvanik): contention - do a real wait!
    XELOGE("RtlEnterCriticalSection tried to really lock!");
  }

  // Now own the lock.
  cs->owning_thread_id  = thread_id;
  cs->recursion_count   = 1;
}


SHIM_CALL RtlTryEnterCriticalSection_shim(
    xe_ppc_state_t* ppc_state, KernelState* state) {
  // DWORD
  // _Inout_  LPCRITICAL_SECTION lpCriticalSection

  uint32_t cs_ptr = SHIM_GET_ARG_32(0);

  XELOGD("RtlTryEnterCriticalSection(%.8X)", cs_ptr);

  X_RTL_CRITICAL_SECTION* cs = (X_RTL_CRITICAL_SECTION*)SHIM_MEM_ADDR(cs_ptr);

  const uint8_t* thread_state_block = ppc_state->membase + ppc_state->r[13];
  uint32_t thread_id = XThread::GetCurrentThreadId(thread_state_block);

  if (xe_atomic_cas_32(-1, 0, &cs->lock_count)) {
    // Able to steal the lock right away.
    cs->owning_thread_id  = thread_id;
    cs->recursion_count   = 1;
    SHIM_SET_RETURN(1);
    return;
  } else if (cs->owning_thread_id == thread_id) {
    xe_atomic_inc_32(&cs->lock_count);
    ++cs->recursion_count;
    SHIM_SET_RETURN(1);
    return;
  }

  SHIM_SET_RETURN(0);
}


SHIM_CALL RtlLeaveCriticalSection_shim(
    xe_ppc_state_t* ppc_state, KernelState* state) {
  // VOID
  // _Inout_  LPCRITICAL_SECTION lpCriticalSection

  uint32_t cs_ptr = SHIM_GET_ARG_32(0);

  XELOGD("RtlLeaveCriticalSection(%.8X)", cs_ptr);

  X_RTL_CRITICAL_SECTION* cs = (X_RTL_CRITICAL_SECTION*)SHIM_MEM_ADDR(cs_ptr);

  // Drop recursion count - if we are still not zero'ed return.
  uint32_t recursion_count = --cs->recursion_count;
  if (recursion_count) {
    xe_atomic_dec_32(&cs->lock_count);
    return;
  }

  // Unlock!
  cs->owning_thread_id  = 0;
  if (xe_atomic_dec_32(&cs->lock_count) != -1) {
    // There were waiters - wake one of them.
    // TODO(benvanik): wake a waiter.
    XELOGE("RtlLeaveCriticalSection would have woken a waiter");
  }
}


}


void xe::kernel::xboxkrnl::RegisterRtlExports(
    ExportResolver* export_resolver, KernelState* state) {
  #define SHIM_SET_MAPPING(ordinal, shim, impl) \
    export_resolver->SetFunctionMapping("xboxkrnl.exe", ordinal, \
        state, (xe_kernel_export_shim_fn)shim, (xe_kernel_export_impl_fn)impl)

  SHIM_SET_MAPPING(0x0000011A, RtlCompareMemory_shim, NULL);
  SHIM_SET_MAPPING(0x0000011B, RtlCompareMemoryUlong_shim, NULL);
  SHIM_SET_MAPPING(0x00000126, RtlFillMemoryUlong_shim, NULL);

  SHIM_SET_MAPPING(0x0000012C, RtlInitAnsiString_shim, NULL);
  SHIM_SET_MAPPING(0x00000127, RtlFreeAnsiString_shim, NULL);
  SHIM_SET_MAPPING(0x0000012D, RtlInitUnicodeString_shim, NULL);
  SHIM_SET_MAPPING(0x00000128, RtlFreeUnicodeString_shim, NULL);
  SHIM_SET_MAPPING(0x00000142, RtlUnicodeStringToAnsiString_shim, NULL);

  SHIM_SET_MAPPING(0x0000012B, RtlImageXexHeaderField_shim, NULL);

  SHIM_SET_MAPPING(0x0000012E, RtlInitializeCriticalSection_shim, NULL);
  SHIM_SET_MAPPING(0x0000012F, RtlInitializeCriticalSectionAndSpinCount_shim, NULL);
  SHIM_SET_MAPPING(0x00000125, RtlEnterCriticalSection_shim, NULL);
  SHIM_SET_MAPPING(0x00000141, RtlTryEnterCriticalSection_shim, NULL);
  SHIM_SET_MAPPING(0x00000130, RtlLeaveCriticalSection_shim, NULL);

  #undef SET_MAPPING
}
