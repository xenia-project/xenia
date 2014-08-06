/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/kernel/xboxkrnl_rtl.h>

#include <locale>
#include <codecvt>

#include <xenia/kernel/kernel_state.h>
#include <xenia/kernel/xboxkrnl_private.h>
#include <xenia/kernel/objects/xthread.h>
#include <xenia/kernel/objects/xuser_module.h>
#include <xenia/kernel/util/shim_utils.h>
#include <xenia/kernel/util/xex2.h>


using namespace xe;
using namespace xe::kernel;
using namespace xe::kernel::xboxkrnl;


namespace xe {
namespace kernel {


// http://msdn.microsoft.com/en-us/library/ff561778
uint32_t xeRtlCompareMemory(uint32_t source1_ptr, uint32_t source2_ptr,
                            uint32_t length) {
  KernelState* state = shared_kernel_state_;
  assert_not_null(state);

  // SIZE_T
  // _In_  const VOID *Source1,
  // _In_  const VOID *Source2,
  // _In_  SIZE_T Length

  uint8_t* p1 = IMPL_MEM_ADDR(source1_ptr);
  uint8_t* p2 = IMPL_MEM_ADDR(source2_ptr);

  // Note that the return value is the number of bytes that match, so it's best
  // we just do this ourselves vs. using memcmp.
  // On Windows we could use the builtin function.

  uint32_t c = 0;
  for (uint32_t n = 0; n < length; n++, p1++, p2++) {
    if (*p1 == *p2) {
      c++;
    }
  }

  return c;
}


SHIM_CALL RtlCompareMemory_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t source1 = SHIM_GET_ARG_32(0);
  uint32_t source2 = SHIM_GET_ARG_32(1);
  uint32_t length = SHIM_GET_ARG_32(2);

  XELOGD(
      "RtlCompareMemory(%.8X, %.8X, %d)",
      source1, source2, length);

  uint32_t result = xeRtlCompareMemory(source1, source2, length);
  SHIM_SET_RETURN_64(result);
}


// http://msdn.microsoft.com/en-us/library/ff552123
uint32_t xeRtlCompareMemoryUlong(uint32_t source_ptr, uint32_t length,
                                 uint32_t pattern) {
  KernelState* state = shared_kernel_state_;
  assert_not_null(state);

  // SIZE_T
  // _In_  PVOID Source,
  // _In_  SIZE_T Length,
  // _In_  ULONG Pattern

  if ((source_ptr % 4) || (length % 4)) {
    return 0;
  }

  uint8_t* p = IMPL_MEM_ADDR(source_ptr);

  // Swap pattern.
  // TODO(benvanik): ensure byte order of pattern is correct.
  // Since we are doing byte-by-byte comparison we may not want to swap.
  // GET_ARG swaps, so this is a swap back. Ugly.
  const uint32_t pb32 = poly::byte_swap(pattern);
  const uint8_t* pb = (uint8_t*)&pb32;

  uint32_t c = 0;
  for (uint32_t n = 0; n < length; n++, p++) {
    if (*p == pb[n % 4]) {
      c++;
    }
  }

  return c;
}


SHIM_CALL RtlCompareMemoryUlong_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t source = SHIM_GET_ARG_32(0);
  uint32_t length = SHIM_GET_ARG_32(1);
  uint32_t pattern = SHIM_GET_ARG_32(2);

  XELOGD(
      "RtlCompareMemoryUlong(%.8X, %d, %.8X)",
      source, length, pattern);

  uint32_t result = xeRtlCompareMemoryUlong(source, length, pattern);
  SHIM_SET_RETURN_64(result);
}


// http://msdn.microsoft.com/en-us/library/ff552263
void xeRtlFillMemoryUlong(uint32_t destination_ptr, uint32_t length,
                          uint32_t pattern) {
  KernelState* state = shared_kernel_state_;
  assert_not_null(state);

  // VOID
  // _Out_  PVOID Destination,
  // _In_   SIZE_T Length,
  // _In_   ULONG Pattern

  // NOTE: length must be % 4, so we can work on uint32s.
  uint32_t* p = (uint32_t*)IMPL_MEM_ADDR(destination_ptr);

  // TODO(benvanik): ensure byte order is correct - we're writing back the
  // swapped arg value.

  uint32_t count = length >> 2;
  uint32_t native_pattern = poly::byte_swap(pattern);

  // TODO: unroll loop?

  for (uint32_t n = 0; n < count; n++, p++) {
    *p = native_pattern;
  }
}


SHIM_CALL RtlFillMemoryUlong_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t destination = SHIM_GET_ARG_32(0);
  uint32_t length = SHIM_GET_ARG_32(1);
  uint32_t pattern = SHIM_GET_ARG_32(2);

  XELOGD(
      "RtlFillMemoryUlong(%.8X, %d, %.8X)",
      destination, length, pattern);

  xeRtlFillMemoryUlong(destination, length, pattern);
}


// typedef struct _STRING {
//   USHORT Length;
//   USHORT MaximumLength;
//   PCHAR  Buffer;
// } ANSI_STRING, *PANSI_STRING;


// http://msdn.microsoft.com/en-us/library/ff561918
void xeRtlInitAnsiString(uint32_t destination_ptr, uint32_t source_ptr) {
  KernelState* state = shared_kernel_state_;
  assert_not_null(state);

  // VOID
  // _Out_     PANSI_STRING DestinationString,
  // _In_opt_  PCSZ SourceString

  if (source_ptr != 0) {
    const char* source = (char*)IMPL_MEM_ADDR(source_ptr);
    uint16_t length = (uint16_t)xestrlena(source);
    IMPL_SET_MEM_16(destination_ptr + 0, length);
    IMPL_SET_MEM_16(destination_ptr + 2, length + 1);
  } else {
    IMPL_SET_MEM_16(destination_ptr + 0, 0);
    IMPL_SET_MEM_16(destination_ptr + 2, 0);
  }
  IMPL_SET_MEM_32(destination_ptr + 4, source_ptr);
}


SHIM_CALL RtlInitAnsiString_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t destination_ptr = SHIM_GET_ARG_32(0);
  uint32_t source_ptr = SHIM_GET_ARG_32(1);

  const char* source = source_ptr ? (char*)SHIM_MEM_ADDR(source_ptr) : NULL;
  XELOGD("RtlInitAnsiString(%.8X, %.8X = %s)",
         destination_ptr, source_ptr, source ? source : "<null>");

  xeRtlInitAnsiString(destination_ptr, source_ptr);
}


// http://msdn.microsoft.com/en-us/library/ff561899
void xeRtlFreeAnsiString(uint32_t string_ptr) {
  KernelState* state = shared_kernel_state_;
  assert_not_null(state);

  // VOID
  // _Inout_  PANSI_STRING AnsiString

  uint32_t buffer = IMPL_MEM_32(string_ptr + 4);
  if (!buffer) {
    return;
  }
  uint32_t length = IMPL_MEM_16(string_ptr + 2);
  state->memory()->HeapFree(buffer, length);

  IMPL_SET_MEM_16(string_ptr + 0, 0);
  IMPL_SET_MEM_16(string_ptr + 2, 0);
  IMPL_SET_MEM_32(string_ptr + 4, 0);
}


SHIM_CALL RtlFreeAnsiString_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t string_ptr = SHIM_GET_ARG_32(0);

  XELOGD("RtlFreeAnsiString(%.8X)", string_ptr);

  xeRtlFreeAnsiString(string_ptr);
}


// typedef struct _UNICODE_STRING {
//   USHORT Length;
//   USHORT MaximumLength;
//   PWSTR  Buffer;
// } UNICODE_STRING, *PUNICODE_STRING;


// http://msdn.microsoft.com/en-us/library/ff561934
void xeRtlInitUnicodeString(uint32_t destination_ptr, uint32_t source_ptr) {
  KernelState* state = shared_kernel_state_;
  assert_not_null(state);

  // VOID
  // _Out_     PUNICODE_STRING DestinationString,
  // _In_opt_  PCWSTR SourceString

  const wchar_t* source =
      source_ptr ? (const wchar_t*)IMPL_MEM_ADDR(source_ptr) : NULL;

  if (source) {
    uint16_t length = (uint16_t)xestrlenw(source);
    IMPL_SET_MEM_16(destination_ptr + 0, length * 2);
    IMPL_SET_MEM_16(destination_ptr + 2, (length + 1) * 2);
    IMPL_SET_MEM_32(destination_ptr + 4, source_ptr);
  } else {
    IMPL_SET_MEM_16(destination_ptr + 0, 0);
    IMPL_SET_MEM_16(destination_ptr + 2, 0);
    IMPL_SET_MEM_32(destination_ptr + 4, 0);
  }
}


SHIM_CALL RtlInitUnicodeString_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t destination_ptr = SHIM_GET_ARG_32(0);
  uint32_t source_ptr = SHIM_GET_ARG_32(1);

  const wchar_t* source =
      source_ptr ? (const wchar_t*)SHIM_MEM_ADDR(source_ptr) : NULL;
  XELOGD("RtlInitUnicodeString(%.8X, %.8X = %ls)",
         destination_ptr, source_ptr, source ? source : L"<null>");

  xeRtlInitUnicodeString(destination_ptr, source_ptr);
}


// http://msdn.microsoft.com/en-us/library/ff561903
void xeRtlFreeUnicodeString(uint32_t string_ptr) {
  KernelState* state = shared_kernel_state_;
  assert_not_null(state);

  // VOID
  // _Inout_  PUNICODE_STRING UnicodeString

  uint32_t buffer = IMPL_MEM_32(string_ptr + 4);
  if (!buffer) {
    return;
  }
  uint32_t length = IMPL_MEM_16(string_ptr + 2);
  state->memory()->HeapFree(buffer, length);

  IMPL_SET_MEM_16(string_ptr + 0, 0);
  IMPL_SET_MEM_16(string_ptr + 2, 0);
  IMPL_SET_MEM_32(string_ptr + 4, 0);
}


SHIM_CALL RtlFreeUnicodeString_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t string_ptr = SHIM_GET_ARG_32(0);

  XELOGD("RtlFreeUnicodeString(%.8X)", string_ptr);

  xeRtlFreeUnicodeString(string_ptr);
}


// http://msdn.microsoft.com/en-us/library/ff562969
X_STATUS xeRtlUnicodeStringToAnsiString(uint32_t destination_ptr,
                                        uint32_t source_ptr,
                                        uint32_t alloc_dest) {
  KernelState* state = shared_kernel_state_;
  assert_not_null(state);

  // NTSTATUS
  // _Inout_  PANSI_STRING DestinationString,
  // _In_     PCUNICODE_STRING SourceString,
  // _In_     BOOLEAN AllocateDestinationString

  std::wstring unicode_str = poly::load_and_swap<std::wstring>(
      IMPL_MEM_ADDR(IMPL_MEM_32(source_ptr + 4)));
  std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
  std::string ansi_str = converter.to_bytes(unicode_str);
  if (ansi_str.size() > 0xFFFF - 1) {
    return X_STATUS_INVALID_PARAMETER_2;
  }

  X_STATUS result = X_STATUS_SUCCESS;
  if (alloc_dest) {
    auto buffer_ptr = state->memory()->HeapAlloc(0, ansi_str.size() + 1, 0);
    memcpy(IMPL_MEM_ADDR(buffer_ptr), ansi_str.data(), ansi_str.size() + 1);
    IMPL_SET_MEM_16(destination_ptr + 0,
                    static_cast<uint16_t>(ansi_str.size()));
    IMPL_SET_MEM_16(destination_ptr + 2,
                    static_cast<uint16_t>(ansi_str.size() + 1));
    IMPL_SET_MEM_32(destination_ptr + 4, static_cast<uint32_t>(buffer_ptr));
  } else {
    uint32_t buffer_capacity = IMPL_MEM_16(destination_ptr + 2);
    uint32_t buffer_ptr = IMPL_MEM_32(destination_ptr + 4);
    if (buffer_capacity < ansi_str.size() + 1) {
      // Too large - we just write what we can.
      result = X_STATUS_BUFFER_OVERFLOW;
      memcpy(IMPL_MEM_ADDR(buffer_ptr), ansi_str.data(), buffer_capacity - 1);
    } else {
      memcpy(IMPL_MEM_ADDR(buffer_ptr), ansi_str.data(), ansi_str.size() + 1);
    }
    IMPL_SET_MEM_8(buffer_ptr + buffer_capacity - 1, 0);  // \0
  }
  return result;
}


SHIM_CALL RtlUnicodeStringToAnsiString_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t destination_ptr = SHIM_GET_ARG_32(0);
  uint32_t source_ptr = SHIM_GET_ARG_32(1);
  uint32_t alloc_dest = SHIM_GET_ARG_32(2);

  XELOGD("RtlUnicodeStringToAnsiString(%.8X, %.8X, %d)",
         destination_ptr, source_ptr, alloc_dest);

  X_STATUS result = xeRtlUnicodeStringToAnsiString(
      destination_ptr, source_ptr, alloc_dest);
  SHIM_SET_RETURN_32(result);
}


SHIM_CALL RtlMultiByteToUnicodeN_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t destination_ptr = SHIM_GET_ARG_32(0);
  uint32_t destination_len = SHIM_GET_ARG_32(1);
  uint32_t written_ptr = SHIM_GET_ARG_32(2);
  uint32_t source_ptr = SHIM_GET_ARG_32(3);
  uint32_t source_len = SHIM_GET_ARG_32(4);

  uint32_t copy_len = destination_len >> 1;
  copy_len = copy_len < source_len ? copy_len : source_len;

  // TODO: maybe use MultiByteToUnicode on Win32? would require swapping

  auto source = (uint8_t*)SHIM_MEM_ADDR(source_ptr);
  auto destination = (uint16_t*)SHIM_MEM_ADDR(destination_ptr);
  for (uint32_t i = 0; i < copy_len; i++)
  {
    *destination++ = poly::byte_swap(*source++);
  }

  if (written_ptr != 0)
  {
    SHIM_SET_MEM_32(written_ptr, copy_len << 1);
  }

  SHIM_SET_RETURN_32(0);
}


SHIM_CALL RtlUnicodeToMultiByteN_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t destination_ptr = SHIM_GET_ARG_32(0);
  uint32_t destination_len = SHIM_GET_ARG_32(1);
  uint32_t written_ptr = SHIM_GET_ARG_32(2);
  uint32_t source_ptr = SHIM_GET_ARG_32(3);
  uint32_t source_len = SHIM_GET_ARG_32(4);

  uint32_t copy_len = source_len >> 1;
  copy_len = copy_len < destination_len ? copy_len : destination_len;

  // TODO: maybe use UnicodeToMultiByte on Win32?

  auto source = (uint16_t*)SHIM_MEM_ADDR(source_ptr);
  auto destination = (uint8_t*)SHIM_MEM_ADDR(destination_ptr);
  for (uint32_t i = 0; i < copy_len; i++)
  {
    uint16_t c = poly::byte_swap(*source++);
    *destination++ = c < 256 ? (uint8_t)c : '?';
  }

  if (written_ptr != 0)
  {
    SHIM_SET_MEM_32(written_ptr, copy_len);
  }

  SHIM_SET_RETURN_32(0);
}


uint32_t xeRtlNtStatusToDosError(X_STATUS status) {
  if (!status || (status & 0x20000000)) {
    // Success.
    return status;
  } else if ((status & 0xF0000000) == 0xD0000000) {
    // High bit doesn't matter.
    status &= ~0x10000000;
  }

  // TODO(benvanik): implement lookup table.
  XELOGE("RtlNtStatusToDosError lookup NOT IMPLEMENTED");

  return 317; // ERROR_MR_MID_NOT_FOUND
}


SHIM_CALL RtlNtStatusToDosError_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t status = SHIM_GET_ARG_32(0);

  XELOGD(
      "RtlNtStatusToDosError(%.4X)",
      status);

  uint32_t result = xeRtlNtStatusToDosError(status);
  SHIM_SET_RETURN_32(result);
}


uint32_t xeRtlImageXexHeaderField(uint32_t xex_header_base_ptr,
                                  uint32_t image_field) {
  KernelState* state = shared_kernel_state_;
  assert_not_null(state);

  // PVOID
  // PVOID XexHeaderBase
  // DWORD ImageField

  // NOTE: this is totally faked!
  // We set the XexExecutableModuleHandle pointer to a block that has at offset
  // 0x58 a pointer to our XexHeaderBase. If the value passed doesn't match
  // then die.
  // The only ImageField I've seen in the wild is
  // 0x20401 (XEX_HEADER_DEFAULT_HEAP_SIZE), so that's all we'll support.

  XUserModule* module = NULL;

  // TODO(benvanik): use xex_header_base to dereference this.
  // Right now we are only concerned with games making this call on their main
  // module, so this hack is fine.
  module = state->GetExecutableModule();

  const xe_xex2_header_t* xex_header = module->xex_header();
  for (size_t n = 0; n < xex_header->header_count; n++) {
    if (xex_header->headers[n].key == image_field) {
      module->Release();
      return xex_header->headers[n].value;
    }
  }

  module->Release();
  return 0;
}


SHIM_CALL RtlImageXexHeaderField_shim(
    PPCContext* ppc_state, KernelState* state) {
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

  uint32_t result = xeRtlImageXexHeaderField(xex_header_base, image_field);
  SHIM_SET_RETURN_64(result);
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

static_assert_size(X_RTL_CRITICAL_SECTION, 28);

void xeRtlInitializeCriticalSection(uint32_t cs_ptr) {
  KernelState* state = shared_kernel_state_;
  assert_not_null(state);

  // VOID
  // _Out_  LPCRITICAL_SECTION lpCriticalSection

  X_RTL_CRITICAL_SECTION* cs = (X_RTL_CRITICAL_SECTION*)IMPL_MEM_ADDR(cs_ptr);
  cs->unknown00           = 1;
  cs->spin_count_div_256  = 0;
  cs->lock_count          = -1;
  cs->recursion_count     = 0;
  cs->owning_thread_id    = 0;
}


SHIM_CALL RtlInitializeCriticalSection_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t cs_ptr = SHIM_GET_ARG_32(0);

  XELOGD("RtlInitializeCriticalSection(%.8X)", cs_ptr);

  xeRtlInitializeCriticalSection(cs_ptr);
}


X_STATUS xeRtlInitializeCriticalSectionAndSpinCount(
    uint32_t cs_ptr, uint32_t spin_count) {
  KernelState* state = shared_kernel_state_;
  assert_not_null(state);

  // NTSTATUS
  // _Out_  LPCRITICAL_SECTION lpCriticalSection,
  // _In_   DWORD dwSpinCount

  // Spin count is rouned up to 256 intervals then packed in.
  //uint32_t spin_count_div_256 = (uint32_t)floor(spin_count / 256.0f + 0.5f);
  uint32_t spin_count_div_256 = (spin_count + 255) >> 8;
  if (spin_count_div_256 > 255) {
    spin_count_div_256 = 255;
  }

  X_RTL_CRITICAL_SECTION* cs = (X_RTL_CRITICAL_SECTION*)IMPL_MEM_ADDR(cs_ptr);
  cs->unknown00           = 1;
  cs->spin_count_div_256  = spin_count_div_256;
  cs->lock_count          = -1;
  cs->recursion_count     = 0;
  cs->owning_thread_id    = 0;

  return X_STATUS_SUCCESS;
}


SHIM_CALL RtlInitializeCriticalSectionAndSpinCount_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t cs_ptr = SHIM_GET_ARG_32(0);
  uint32_t spin_count = SHIM_GET_ARG_32(1);

  XELOGD("RtlInitializeCriticalSectionAndSpinCount(%.8X, %d)",
         cs_ptr, spin_count);

  X_STATUS result = xeRtlInitializeCriticalSectionAndSpinCount(
      cs_ptr, spin_count);
  SHIM_SET_RETURN_32(result);
}


// TODO(benvanik): remove the need for passing in thread_id.
void xeRtlEnterCriticalSection(uint32_t cs_ptr, uint32_t thread_id) {
  KernelState* state = shared_kernel_state_;
  assert_not_null(state);

  // VOID
  // _Inout_  LPCRITICAL_SECTION lpCriticalSection

  X_RTL_CRITICAL_SECTION* cs = (X_RTL_CRITICAL_SECTION*)IMPL_MEM_ADDR(cs_ptr);

  uint32_t spin_wait_remaining = cs->spin_count_div_256 * 256;
spin:
  if (poly::atomic_inc(&cs->lock_count) != 0) {
    // If this thread already owns the CS increment the recursion count.
    if (cs->owning_thread_id == thread_id) {
      cs->recursion_count++;
      return;
    }
    poly::atomic_dec(&cs->lock_count);

    // Thread was locked - spin wait.
    if (spin_wait_remaining) {
      spin_wait_remaining--;
      goto spin;
    }

    // All out of spin waits, create a full waiter.
    // TODO(benvanik): contention - do a real wait!
    //XELOGE("RtlEnterCriticalSection tried to really lock!");
    spin_wait_remaining = 1; // HACK: spin forever
    Sleep(1);
    goto spin;
  }

  // Now own the lock.
  cs->owning_thread_id  = thread_id;
  cs->recursion_count   = 1;
}

SHIM_CALL RtlEnterCriticalSection_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t cs_ptr = SHIM_GET_ARG_32(0);

  // XELOGD("RtlEnterCriticalSection(%.8X)", cs_ptr);

  const uint8_t* thread_state_block = ppc_state->membase + ppc_state->r[13];
  uint32_t thread_id = XThread::GetCurrentThreadId(thread_state_block);

  xeRtlEnterCriticalSection(cs_ptr, thread_id);
}


// TODO(benvanik): remove the need for passing in thread_id.
uint32_t xeRtlTryEnterCriticalSection(uint32_t cs_ptr, uint32_t thread_id) {
  KernelState* state = shared_kernel_state_;
  assert_not_null(state);

  // DWORD
  // _Inout_  LPCRITICAL_SECTION lpCriticalSection

  X_RTL_CRITICAL_SECTION* cs = (X_RTL_CRITICAL_SECTION*)IMPL_MEM_ADDR(cs_ptr);

  if (poly::atomic_cas(-1, 0, &cs->lock_count)) {
    // Able to steal the lock right away.
    cs->owning_thread_id  = thread_id;
    cs->recursion_count   = 1;
    return 1;
  } else if (cs->owning_thread_id == thread_id) {
    poly::atomic_inc(&cs->lock_count);
    ++cs->recursion_count;
    return 1;
  }

  return 0;
}


SHIM_CALL RtlTryEnterCriticalSection_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t cs_ptr = SHIM_GET_ARG_32(0);

  // XELOGD("RtlTryEnterCriticalSection(%.8X)", cs_ptr);

  const uint8_t* thread_state_block = ppc_state->membase + ppc_state->r[13];
  uint32_t thread_id = XThread::GetCurrentThreadId(thread_state_block);

  uint32_t result = xeRtlTryEnterCriticalSection(cs_ptr, thread_id);
  SHIM_SET_RETURN_64(result);
}


void xeRtlLeaveCriticalSection(uint32_t cs_ptr) {
  KernelState* state = shared_kernel_state_;
  assert_not_null(state);

  // VOID
  // _Inout_  LPCRITICAL_SECTION lpCriticalSection

  X_RTL_CRITICAL_SECTION* cs = (X_RTL_CRITICAL_SECTION*)IMPL_MEM_ADDR(cs_ptr);

  // Drop recursion count - if we are still not zero'ed return.
  uint32_t recursion_count = --cs->recursion_count;
  if (recursion_count) {
    poly::atomic_dec(&cs->lock_count);
    return;
  }

  // Unlock!
  cs->owning_thread_id  = 0;
  if (poly::atomic_dec(&cs->lock_count) != -1) {
    // There were waiters - wake one of them.
    // TODO(benvanik): wake a waiter.
    XELOGE("RtlLeaveCriticalSection would have woken a waiter");
  }
}


SHIM_CALL RtlLeaveCriticalSection_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t cs_ptr = SHIM_GET_ARG_32(0);

  // XELOGD("RtlLeaveCriticalSection(%.8X)", cs_ptr);

  xeRtlLeaveCriticalSection(cs_ptr);
}


SHIM_CALL RtlTimeToTimeFields_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t time_ptr = SHIM_GET_ARG_32(0);
  uint32_t time_fields_ptr = SHIM_GET_ARG_32(1);

  XELOGD("RtlTimeToTimeFields(%.8X, %.8X)", time_ptr, time_fields_ptr);

  uint64_t time = SHIM_MEM_64(time_ptr);
  FILETIME ft;
  ft.dwHighDateTime = time >> 32;
  ft.dwLowDateTime = (uint32_t)time;

  SYSTEMTIME st;
  FileTimeToSystemTime(&ft, &st);

  SHIM_SET_MEM_16(time_fields_ptr + 0, st.wYear);
  SHIM_SET_MEM_16(time_fields_ptr + 2, st.wMonth);
  SHIM_SET_MEM_16(time_fields_ptr + 4, st.wDay);
  SHIM_SET_MEM_16(time_fields_ptr + 6, st.wHour);
  SHIM_SET_MEM_16(time_fields_ptr + 8, st.wMinute);
  SHIM_SET_MEM_16(time_fields_ptr + 10, st.wSecond);
  SHIM_SET_MEM_16(time_fields_ptr + 12, st.wMilliseconds);
}


SHIM_CALL RtlTimeFieldsToTime_shim(
    PPCContext* ppc_state, KernelState* state) {
  uint32_t time_fields_ptr = SHIM_GET_ARG_32(0);
  uint32_t time_ptr = SHIM_GET_ARG_32(1);

  XELOGD("RtlTimeFieldsToTime(%.8X, %.8X)", time_fields_ptr, time_ptr);

  SYSTEMTIME st;
  st.wYear          = SHIM_MEM_16(time_fields_ptr + 0);
  st.wMonth         = SHIM_MEM_16(time_fields_ptr + 2);
  st.wDay           = SHIM_MEM_16(time_fields_ptr + 4);
  st.wHour          = SHIM_MEM_16(time_fields_ptr + 6);
  st.wMinute        = SHIM_MEM_16(time_fields_ptr + 8);
  st.wSecond        = SHIM_MEM_16(time_fields_ptr + 10);
  st.wMilliseconds  = SHIM_MEM_16(time_fields_ptr + 12);

  FILETIME ft;
  if (!SystemTimeToFileTime(&st, &ft)) {
    // set last error = ERROR_INVALID_PARAMETER
    SHIM_SET_RETURN_64(0);
    return;
  }

  uint64_t time = (uint64_t(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
  SHIM_SET_MEM_64(time_ptr, time);
  SHIM_SET_RETURN_64(1);
}


}  // namespace kernel
}  // namespace xe


void xe::kernel::xboxkrnl::RegisterRtlExports(
    ExportResolver* export_resolver, KernelState* state) {
  SHIM_SET_MAPPING("xboxkrnl.exe", RtlCompareMemory, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", RtlCompareMemoryUlong, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", RtlFillMemoryUlong, state);

  SHIM_SET_MAPPING("xboxkrnl.exe", RtlInitAnsiString, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", RtlFreeAnsiString, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", RtlInitUnicodeString, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", RtlFreeUnicodeString, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", RtlUnicodeStringToAnsiString, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", RtlMultiByteToUnicodeN, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", RtlUnicodeToMultiByteN, state);

  SHIM_SET_MAPPING("xboxkrnl.exe", RtlTimeToTimeFields, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", RtlTimeFieldsToTime, state);

  SHIM_SET_MAPPING("xboxkrnl.exe", RtlNtStatusToDosError, state);

  SHIM_SET_MAPPING("xboxkrnl.exe", RtlImageXexHeaderField, state);

  SHIM_SET_MAPPING("xboxkrnl.exe", RtlInitializeCriticalSection, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", RtlInitializeCriticalSectionAndSpinCount, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", RtlEnterCriticalSection, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", RtlTryEnterCriticalSection, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", RtlLeaveCriticalSection, state);
}
