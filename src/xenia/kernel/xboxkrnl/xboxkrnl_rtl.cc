/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/xboxkrnl/xboxkrnl_rtl.h"

#include <algorithm>
#include <string>

#include "xenia/base/atomic.h"
#include "xenia/base/logging.h"
#include "xenia/base/string.h"
#include "xenia/base/threading.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/user_module.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/util/xex2.h"
#include "xenia/kernel/xboxkrnl/xboxkrnl_private.h"
#include "xenia/kernel/xthread.h"

// For FileTimeToSystemTime and SystemTimeToFileTime:
#include "xenia/base/platform_win.h"

namespace xe {
namespace kernel {
namespace xboxkrnl {

// http://msdn.microsoft.com/en-us/library/ff561778
dword_result_t RtlCompareMemory(lpvoid_t source1, lpvoid_t source2,
                                dword_t length) {
  uint8_t* p1 = source1;
  uint8_t* p2 = source2;

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
DECLARE_XBOXKRNL_EXPORT(RtlCompareMemory, ExportTag::kImplemented);

// http://msdn.microsoft.com/en-us/library/ff552123
dword_result_t RtlCompareMemoryUlong(lpvoid_t source, dword_t length,
                                     dword_t pattern) {
  // Return 0 if source/length not aligned
  if (source.guest_address() % 4 || length % 4) {
    return 0;
  }

  uint32_t n = 0;
  for (uint32_t i = 0; i < (length / 4); i++) {
    // FIXME: This assumes as_array returns xe::be
    uint32_t val = source.as_array<uint32_t>()[i];
    if (val == pattern) {
      n++;
    }
  }

  return n;
}
DECLARE_XBOXKRNL_EXPORT(RtlCompareMemoryUlong, ExportTag::kImplemented);

// http://msdn.microsoft.com/en-us/library/ff552263
void RtlFillMemoryUlong(lpvoid_t destination, dword_t length, dword_t pattern) {
  // NOTE: length must be % 4, so we can work on uint32s.
  uint32_t count = length >> 2;

  uint32_t* p = destination.as<uint32_t*>();
  uint32_t swapped_pattern = xe::byte_swap(pattern.value());
  for (uint32_t n = 0; n < count; n++, p++) {
    *p = swapped_pattern;
  }
}
DECLARE_XBOXKRNL_EXPORT(RtlFillMemoryUlong, ExportTag::kImplemented);

dword_result_t RtlCompareString(lpstring_t string_1, lpstring_t string_2,
                                dword_t case_insensitive) {
  int ret = case_insensitive ? strcasecmp(string_1, string_2)
                             : std::strcmp(string_1, string_2);

  return ret;
}
DECLARE_XBOXKRNL_EXPORT(RtlCompareString, ExportTag::kImplemented);

dword_result_t RtlCompareStringN(lpstring_t string_1, dword_t string_1_len,
                                 lpstring_t string_2, dword_t string_2_len,
                                 dword_t case_insensitive) {
  uint32_t len1 = string_1_len;
  uint32_t len2 = string_2_len;

  if (string_1_len == 0xFFFF) {
    len1 = uint32_t(std::strlen(string_1));
  }
  if (string_2_len == 0xFFFF) {
    len2 = uint32_t(std::strlen(string_2));
  }
  auto len = std::min(string_1_len, string_2_len);

  int ret = case_insensitive ? strncasecmp(string_1, string_2, len)
                             : std::strncmp(string_1, string_2, len);

  return ret;
}
DECLARE_XBOXKRNL_EXPORT(RtlCompareStringN, ExportTag::kImplemented);

// http://msdn.microsoft.com/en-us/library/ff561918
void RtlInitAnsiString(pointer_t<X_ANSI_STRING> destination,
                       lpstring_t source) {
  if (source) {
    uint16_t length = (uint16_t)strlen(source);
    destination->length = length;
    destination->maximum_length = length + 1;
  } else {
    destination->reset();
  }

  destination->pointer = source.guest_address();
}
DECLARE_XBOXKRNL_EXPORT(RtlInitAnsiString, ExportTag::kImplemented);

// http://msdn.microsoft.com/en-us/library/ff561899
void RtlFreeAnsiString(pointer_t<X_ANSI_STRING> string) {
  if (string->pointer) {
    kernel_memory()->SystemHeapFree(string->pointer);
  }

  string->reset();
}
DECLARE_XBOXKRNL_EXPORT(RtlFreeAnsiString, ExportTag::kImplemented);

// http://msdn.microsoft.com/en-us/library/ff561934
void RtlInitUnicodeString(pointer_t<X_UNICODE_STRING> destination,
                          lpwstring_t source) {
  if (source) {
    destination->length = (uint16_t)source.value().size() * 2;
    destination->maximum_length = (uint16_t)(source.value().size() + 1) * 2;
    destination->pointer = source.guest_address();
  } else {
    destination->reset();
  }
}
DECLARE_XBOXKRNL_EXPORT(RtlInitUnicodeString, ExportTag::kImplemented);

// http://msdn.microsoft.com/en-us/library/ff561903
void RtlFreeUnicodeString(pointer_t<X_UNICODE_STRING> string) {
  if (string->pointer) {
    kernel_memory()->SystemHeapFree(string->pointer);
  }

  string->reset();
}
DECLARE_XBOXKRNL_EXPORT(RtlFreeUnicodeString, ExportTag::kImplemented);

// http://msdn.microsoft.com/en-us/library/ff562969
SHIM_CALL RtlUnicodeStringToAnsiString_shim(PPCContext* ppc_context,
                                            KernelState* kernel_state) {
  uint32_t destination_ptr = SHIM_GET_ARG_32(0);
  uint32_t source_ptr = SHIM_GET_ARG_32(1);
  uint32_t alloc_dest = SHIM_GET_ARG_32(2);

  XELOGD("RtlUnicodeStringToAnsiString(%.8X, %.8X, %d)", destination_ptr,
         source_ptr, alloc_dest);

  // NTSTATUS
  // _Inout_  PANSI_STRING DestinationString,
  // _In_     PCUNICODE_STRING SourceString,
  // _In_     BOOLEAN AllocateDestinationString

  std::wstring unicode_str = xe::load_and_swap<std::wstring>(
      SHIM_MEM_ADDR(SHIM_MEM_32(source_ptr + 4)));
  std::string ansi_str = xe::to_string(unicode_str);
  if (ansi_str.size() > 0xFFFF - 1) {
    SHIM_SET_RETURN_32(X_STATUS_INVALID_PARAMETER_2);
    return;
  }

  X_STATUS result = X_STATUS_SUCCESS;
  if (alloc_dest) {
    uint32_t buffer_ptr =
        kernel_state->memory()->SystemHeapAlloc(uint32_t(ansi_str.size() + 1));
    memcpy(SHIM_MEM_ADDR(buffer_ptr), ansi_str.data(), ansi_str.size() + 1);
    SHIM_SET_MEM_16(destination_ptr + 0,
                    static_cast<uint16_t>(ansi_str.size()));
    SHIM_SET_MEM_16(destination_ptr + 2,
                    static_cast<uint16_t>(ansi_str.size() + 1));
    SHIM_SET_MEM_32(destination_ptr + 4, static_cast<uint32_t>(buffer_ptr));
  } else {
    uint32_t buffer_capacity = SHIM_MEM_16(destination_ptr + 2);
    uint32_t buffer_ptr = SHIM_MEM_32(destination_ptr + 4);
    if (buffer_capacity < ansi_str.size() + 1) {
      // Too large - we just write what we can.
      result = X_STATUS_BUFFER_OVERFLOW;
      memcpy(SHIM_MEM_ADDR(buffer_ptr), ansi_str.data(), buffer_capacity - 1);
    } else {
      memcpy(SHIM_MEM_ADDR(buffer_ptr), ansi_str.data(), ansi_str.size() + 1);
    }
    SHIM_SET_MEM_8(buffer_ptr + buffer_capacity - 1, 0);  // \0
  }
  SHIM_SET_RETURN_32(result);
}

SHIM_CALL RtlMultiByteToUnicodeN_shim(PPCContext* ppc_context,
                                      KernelState* kernel_state) {
  uint32_t destination_ptr = SHIM_GET_ARG_32(0);
  uint32_t destination_len = SHIM_GET_ARG_32(1);
  uint32_t written_ptr = SHIM_GET_ARG_32(2);
  uint32_t source_ptr = SHIM_GET_ARG_32(3);
  uint32_t source_len = SHIM_GET_ARG_32(4);

  uint32_t copy_len = destination_len >> 1;
  copy_len = copy_len < source_len ? copy_len : source_len;

  // TODO(benvanik): maybe use MultiByteToUnicode on Win32? would require
  // swapping.

  for (uint32_t i = 0; i < copy_len; i++) {
    xe::store_and_swap<uint16_t>(
        SHIM_MEM_ADDR(destination_ptr + i * 2),
        xe::load<uint8_t>(SHIM_MEM_ADDR(source_ptr + i)));
  }

  if (written_ptr != 0) {
    SHIM_SET_MEM_32(written_ptr, copy_len << 1);
  }

  SHIM_SET_RETURN_32(0);
}

SHIM_CALL RtlUnicodeToMultiByteN_shim(PPCContext* ppc_context,
                                      KernelState* kernel_state) {
  uint32_t destination_ptr = SHIM_GET_ARG_32(0);
  uint32_t destination_len = SHIM_GET_ARG_32(1);
  uint32_t written_ptr = SHIM_GET_ARG_32(2);
  uint32_t source_ptr = SHIM_GET_ARG_32(3);
  uint32_t source_len = SHIM_GET_ARG_32(4);

  uint32_t copy_len = source_len >> 1;
  copy_len = copy_len < destination_len ? copy_len : destination_len;

  // TODO(benvanik): maybe use UnicodeToMultiByte on Win32?

  auto source = reinterpret_cast<uint16_t*>(SHIM_MEM_ADDR(source_ptr));
  auto destination = reinterpret_cast<uint8_t*>(SHIM_MEM_ADDR(destination_ptr));
  for (uint32_t i = 0; i < copy_len; i++) {
    uint16_t c = xe::byte_swap(*source++);
    *destination++ = c < 256 ? (uint8_t)c : '?';
  }

  if (written_ptr != 0) {
    SHIM_SET_MEM_32(written_ptr, copy_len);
  }

  SHIM_SET_RETURN_32(0);
}

pointer_result_t RtlImageXexHeaderField(pointer_t<xex2_header> xex_header,
                                        dword_t field_dword) {
  uint32_t field_value = 0;
  uint32_t field = field_dword;  // VS acts weird going from dword_t -> enum

  UserModule::GetOptHeader(kernel_memory()->virtual_membase(), xex_header,
                           xe_xex2_header_keys(field), &field_value);

  return field_value;
}
DECLARE_XBOXKRNL_EXPORT(RtlImageXexHeaderField, ExportTag::kImplemented);

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
// Ref:
// http://svn.reactos.org/svn/reactos/trunk/reactos/lib/rtl/critical.c?view=markup

// This structure tries to match the one on the 360 as best I can figure out.
// Unfortunately some games have the critical sections pre-initialized in
// their embedded data and InitializeCriticalSection will never be called.
#pragma pack(push, 1)
struct X_RTL_CRITICAL_SECTION {
  xe::be<uint8_t> unk_00;              // 0x0
  xe::be<uint8_t> spin_count_div_256;  // 0x1
  xe::be<uint16_t> __padding0;         // 0x2
  xe::be<uint32_t> unk_04;             // 0x4 maybe the handle to the event?
  xe::be<uint32_t> queue_head;  // 0x8 head of queue, pointing to this offset
  xe::be<uint32_t> queue_tail;  // 0xC tail of queue?
  int32_t lock_count;           // 0x10 -1 -> 0 on first lock 0x10
  xe::be<int32_t> recursion_count;    // 0x14  0 -> 1 on first lock 0x14
  xe::be<uint32_t> owning_thread_id;  // 0x18 0 unless locked 0x18
};
#pragma pack(pop)
static_assert_size(X_RTL_CRITICAL_SECTION, 28);

void xeRtlInitializeCriticalSection(X_RTL_CRITICAL_SECTION* cs,
                                    uint32_t cs_ptr) {
  cs->unk_00 = 1;
  cs->spin_count_div_256 = 0;
  cs->queue_head = cs_ptr + 8;
  cs->queue_tail = cs_ptr + 8;
  cs->lock_count = -1;
  cs->recursion_count = 0;
  cs->owning_thread_id = 0;
}

void RtlInitializeCriticalSection(pointer_t<X_RTL_CRITICAL_SECTION> cs) {
  xeRtlInitializeCriticalSection(cs, cs.guest_address());
}
DECLARE_XBOXKRNL_EXPORT(RtlInitializeCriticalSection, ExportTag::kImplemented);

X_STATUS xeRtlInitializeCriticalSectionAndSpinCount(X_RTL_CRITICAL_SECTION* cs,
                                                    uint32_t cs_ptr,
                                                    uint32_t spin_count) {
  // Spin count is rounded up to 256 intervals then packed in.
  // uint32_t spin_count_div_256 = (uint32_t)floor(spin_count / 256.0f + 0.5f);
  uint32_t spin_count_div_256 = (spin_count + 255) >> 8;
  if (spin_count_div_256 > 255) {
    spin_count_div_256 = 255;
  }

  cs->unk_00 = 1;
  cs->spin_count_div_256 = spin_count_div_256;
  cs->queue_head = cs_ptr + 8;
  cs->queue_tail = cs_ptr + 8;
  cs->lock_count = -1;
  cs->recursion_count = 0;
  cs->owning_thread_id = 0;

  return X_STATUS_SUCCESS;
}

dword_result_t RtlInitializeCriticalSectionAndSpinCount(
    pointer_t<X_RTL_CRITICAL_SECTION> cs, dword_t spin_count) {
  return xeRtlInitializeCriticalSectionAndSpinCount(cs, cs.guest_address(),
                                                    spin_count);
}
DECLARE_XBOXKRNL_EXPORT(RtlInitializeCriticalSectionAndSpinCount,
                        ExportTag::kImplemented);

SHIM_CALL RtlEnterCriticalSection_shim(PPCContext* ppc_context,
                                       KernelState* kernel_state) {
  // VOID
  // _Inout_  LPCRITICAL_SECTION lpCriticalSection
  uint32_t cs_ptr = SHIM_GET_ARG_32(0);

  // XELOGD("RtlEnterCriticalSection(%.8X)", cs_ptr);

  uint32_t thread_id = XThread::GetCurrentThreadId();

  auto cs = reinterpret_cast<X_RTL_CRITICAL_SECTION*>(SHIM_MEM_ADDR(cs_ptr));
  uint32_t spin_wait_remaining = cs->spin_count_div_256 * 256;

spin:
  if (xe::atomic_inc(&cs->lock_count) != 0) {
    // If this thread already owns the CS increment the recursion count.
    if (cs->owning_thread_id == thread_id) {
      cs->recursion_count++;
      return;
    }
    xe::atomic_dec(&cs->lock_count);

    // Thread was locked - spin wait.
    if (spin_wait_remaining) {
      spin_wait_remaining--;
      goto spin;
    }

    // All out of spin waits, create a full waiter.
    // TODO(benvanik): contention - do a real wait!
    // XELOGE("RtlEnterCriticalSection tried to really lock!");
    spin_wait_remaining = 0;  // HACK: spin forever
    xe::threading::MaybeYield();
    goto spin;
  }

  // Now own the lock.
  cs->owning_thread_id = thread_id;
  cs->recursion_count = 1;
}

SHIM_CALL RtlTryEnterCriticalSection_shim(PPCContext* ppc_context,
                                          KernelState* kernel_state) {
  // DWORD
  // _Inout_  LPCRITICAL_SECTION lpCriticalSection
  uint32_t cs_ptr = SHIM_GET_ARG_32(0);

  // XELOGD("RtlTryEnterCriticalSection(%.8X)", cs_ptr);

  uint32_t thread_id = XThread::GetCurrentThreadId();

  auto cs = reinterpret_cast<X_RTL_CRITICAL_SECTION*>(SHIM_MEM_ADDR(cs_ptr));

  uint32_t result = 0;
  if (xe::atomic_cas(-1, 0, &cs->lock_count)) {
    // Able to steal the lock right away.
    cs->owning_thread_id = thread_id;
    cs->recursion_count = 1;
    result = 1;
  } else if (cs->owning_thread_id == thread_id) {
    xe::atomic_inc(&cs->lock_count);
    ++cs->recursion_count;
    result = 1;
  }
  SHIM_SET_RETURN_32(result);
}

SHIM_CALL RtlLeaveCriticalSection_shim(PPCContext* ppc_context,
                                       KernelState* kernel_state) {
  // VOID
  // _Inout_  LPCRITICAL_SECTION lpCriticalSection
  uint32_t cs_ptr = SHIM_GET_ARG_32(0);

  // XELOGD("RtlLeaveCriticalSection(%.8X)", cs_ptr);

  // FYI: No need to check if the owning thread is calling this, as that should
  // be the only case.

  auto cs = reinterpret_cast<X_RTL_CRITICAL_SECTION*>(SHIM_MEM_ADDR(cs_ptr));

  // Drop recursion count - if we are still not zero'ed return.
  int32_t recursion_count = --cs->recursion_count;
  assert_true(recursion_count > -1);
  if (recursion_count) {
    assert_true(cs->recursion_count > 0);

    xe::atomic_dec(&cs->lock_count);
    return;
  }

  // Not owned - unlock!
  cs->owning_thread_id = 0;
  if (xe::atomic_dec(&cs->lock_count) != -1) {
    // There were waiters - wake one of them.
    // TODO(benvanik): wake a waiter.
    XELOGE("RtlLeaveCriticalSection would have woken a waiter");
  }

  XThread::GetCurrentThread()->CheckApcs();
}

struct X_TIME_FIELDS {
  xe::be<uint16_t> year;
  xe::be<uint16_t> month;
  xe::be<uint16_t> day;
  xe::be<uint16_t> hour;
  xe::be<uint16_t> minute;
  xe::be<uint16_t> second;
  xe::be<uint16_t> milliseconds;
  xe::be<uint16_t> weekday;
};
static_assert(sizeof(X_TIME_FIELDS) == 16, "Must be LARGEINTEGER");

void RtlTimeToTimeFields(lpqword_t time_ptr,
                         pointer_t<X_TIME_FIELDS> time_fields_ptr) {
  // TODO(benvanik): replace with our own version.
  FILETIME ft;
  ft.dwHighDateTime = static_cast<uint32_t>(time_ptr.value() >> 32);
  ft.dwLowDateTime = static_cast<uint32_t>(time_ptr.value());
  SYSTEMTIME st;
  FileTimeToSystemTime(&ft, &st);

  time_fields_ptr->year = st.wYear;
  time_fields_ptr->month = st.wMonth;
  time_fields_ptr->day = st.wDay;
  time_fields_ptr->hour = st.wHour;
  time_fields_ptr->minute = st.wMinute;
  time_fields_ptr->second = st.wSecond;
  time_fields_ptr->milliseconds = st.wMilliseconds;
  time_fields_ptr->weekday = st.wDayOfWeek;
}
DECLARE_XBOXKRNL_EXPORT(RtlTimeToTimeFields, ExportTag::kImplemented);

dword_result_t RtlTimeFieldsToTime(pointer_t<X_TIME_FIELDS> time_fields_ptr,
                                   lpqword_t time_ptr) {
  // TODO(benvanik): replace with our own version.
  SYSTEMTIME st;
  st.wYear = time_fields_ptr->year;
  st.wMonth = time_fields_ptr->month;
  st.wDay = time_fields_ptr->day;
  st.wHour = time_fields_ptr->hour;
  st.wMinute = time_fields_ptr->minute;
  st.wSecond = time_fields_ptr->second;
  st.wMilliseconds = time_fields_ptr->milliseconds;
  st.wDayOfWeek = time_fields_ptr->weekday;
  FILETIME ft;
  if (!SystemTimeToFileTime(&st, &ft)) {
    // set last error = ERROR_INVALID_PARAMETER
    return 0;
  }
  uint64_t time = (uint64_t(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
  *time_ptr = time;
  return 1;
}
DECLARE_XBOXKRNL_EXPORT(RtlTimeFieldsToTime, ExportTag::kImplemented);

void RegisterRtlExports(xe::cpu::ExportResolver* export_resolver,
                        KernelState* kernel_state) {
  SHIM_SET_MAPPING("xboxkrnl.exe", RtlUnicodeStringToAnsiString, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", RtlMultiByteToUnicodeN, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", RtlUnicodeToMultiByteN, state);

  SHIM_SET_MAPPING("xboxkrnl.exe", RtlEnterCriticalSection, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", RtlTryEnterCriticalSection, state);
  SHIM_SET_MAPPING("xboxkrnl.exe", RtlLeaveCriticalSection, state);
}

}  // namespace xboxkrnl
}  // namespace kernel
}  // namespace xe
