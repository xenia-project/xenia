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
#include "xenia/kernel/xboxkrnl/xboxkrnl_private.h"
#include "xenia/kernel/xboxkrnl/xboxkrnl_threading.h"
#include "xenia/kernel/xevent.h"
#include "xenia/kernel/xthread.h"

#if XE_PLATFORM_WIN32
#include "xenia/base/platform_win.h"
#define timegm _mkgmtime
#endif

namespace xe {
namespace kernel {
namespace xboxkrnl {

// https://msdn.microsoft.com/en-us/library/ff561778
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
DECLARE_XBOXKRNL_EXPORT1(RtlCompareMemory, kMemory, kImplemented);

// https://msdn.microsoft.com/en-us/library/ff552123
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
DECLARE_XBOXKRNL_EXPORT1(RtlCompareMemoryUlong, kMemory, kImplemented);

// https://msdn.microsoft.com/en-us/library/ff552263
void RtlFillMemoryUlong(lpvoid_t destination, dword_t length, dword_t pattern) {
  // NOTE: length must be % 4, so we can work on uint32s.
  uint32_t count = length >> 2;

  uint32_t* p = destination.as<uint32_t*>();
  uint32_t swapped_pattern = xe::byte_swap(pattern.value());
  for (uint32_t n = 0; n < count; n++, p++) {
    *p = swapped_pattern;
  }
}
DECLARE_XBOXKRNL_EXPORT1(RtlFillMemoryUlong, kMemory, kImplemented);

dword_result_t RtlUpperChar(dword_t in) {
  char c = in & 0xFF;
  if (c >= 'a' && c <= 'z') {
    return c ^ 0x20;
  }

  return c;
}
DECLARE_XBOXKRNL_EXPORT1(RtlUpperChar, kNone, kImplemented);

dword_result_t RtlLowerChar(dword_t in) {
  char c = in & 0xFF;
  if (c >= 'A' && c <= 'Z') {
    return c ^ 0x20;
  }

  return c;
}
DECLARE_XBOXKRNL_EXPORT1(RtlLowerChar, kNone, kImplemented);

dword_result_t RtlCompareString(lpstring_t string_1, lpstring_t string_2,
                                dword_t case_insensitive) {
  int ret = case_insensitive ? strcasecmp(string_1, string_2)
                             : std::strcmp(string_1, string_2);

  return ret;
}
DECLARE_XBOXKRNL_EXPORT1(RtlCompareString, kNone, kImplemented);

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
DECLARE_XBOXKRNL_EXPORT1(RtlCompareStringN, kNone, kImplemented);

// https://msdn.microsoft.com/en-us/library/ff561918
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
DECLARE_XBOXKRNL_EXPORT1(RtlInitAnsiString, kNone, kImplemented);

// https://msdn.microsoft.com/en-us/library/ff561899
void RtlFreeAnsiString(pointer_t<X_ANSI_STRING> string) {
  if (string->pointer) {
    kernel_memory()->SystemHeapFree(string->pointer);
  }

  string->reset();
}
DECLARE_XBOXKRNL_EXPORT1(RtlFreeAnsiString, kNone, kImplemented);

// https://msdn.microsoft.com/en-us/library/ff561934
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
DECLARE_XBOXKRNL_EXPORT1(RtlInitUnicodeString, kNone, kImplemented);

// https://msdn.microsoft.com/en-us/library/ff561903
void RtlFreeUnicodeString(pointer_t<X_UNICODE_STRING> string) {
  if (string->pointer) {
    kernel_memory()->SystemHeapFree(string->pointer);
  }

  string->reset();
}
DECLARE_XBOXKRNL_EXPORT1(RtlFreeUnicodeString, kNone, kImplemented);

void RtlCopyString(pointer_t<X_ANSI_STRING> destination,
                   pointer_t<X_ANSI_STRING> source) {
  if (!source) {
    destination->length = 0;
    return;
  }

  auto length = std::min(destination->maximum_length, source->length);
  if (length > 0) {
    auto dst_buf = kernel_memory()->TranslateVirtual(destination->pointer);
    auto src_buf = kernel_memory()->TranslateVirtual(source->pointer);
    std::memcpy(dst_buf, src_buf, length);
  }
  destination->length = length;
}
DECLARE_XBOXKRNL_EXPORT1(RtlCopyString, kNone, kImplemented);

void RtlCopyUnicodeString(pointer_t<X_UNICODE_STRING> destination,
                          pointer_t<X_UNICODE_STRING> source) {
  if (!source) {
    destination->length = 0;
    return;
  }

  auto length = std::min(destination->maximum_length, source->length);
  if (length > 0) {
    auto dst_buf = kernel_memory()->TranslateVirtual(destination->pointer);
    auto src_buf = kernel_memory()->TranslateVirtual(source->pointer);
    std::memcpy(dst_buf, src_buf, length * 2);
  }
  destination->length = length;
}
DECLARE_XBOXKRNL_EXPORT1(RtlCopyUnicodeString, kNone, kImplemented);

// https://msdn.microsoft.com/en-us/library/ff562969
dword_result_t RtlUnicodeStringToAnsiString(
    pointer_t<X_ANSI_STRING> destination_ptr,
    pointer_t<X_UNICODE_STRING> source_ptr, dword_t alloc_dest) {
  // NTSTATUS
  // _Inout_  PANSI_STRING DestinationString,
  // _In_     PCUNICODE_STRING SourceString,
  // _In_     BOOLEAN AllocateDestinationString

  std::wstring unicode_str =
      source_ptr->to_string(kernel_memory()->virtual_membase());
  std::string ansi_str = xe::to_string(unicode_str);
  if (ansi_str.size() > 0xFFFF - 1) {
    return X_STATUS_INVALID_PARAMETER_2;
  }

  X_STATUS result = X_STATUS_SUCCESS;
  if (alloc_dest) {
    uint32_t buffer_ptr =
        kernel_memory()->SystemHeapAlloc(uint32_t(ansi_str.size() + 1));

    memcpy(kernel_memory()->virtual_membase() + buffer_ptr, ansi_str.data(),
           ansi_str.size() + 1);
    destination_ptr->length = static_cast<uint16_t>(ansi_str.size());
    destination_ptr->maximum_length =
        static_cast<uint16_t>(ansi_str.size() + 1);
    destination_ptr->pointer = static_cast<uint32_t>(buffer_ptr);
  } else {
    uint32_t buffer_capacity = destination_ptr->maximum_length;
    auto buffer_ptr =
        kernel_memory()->virtual_membase() + destination_ptr->pointer;
    if (buffer_capacity < ansi_str.size() + 1) {
      // Too large - we just write what we can.
      result = X_STATUS_BUFFER_OVERFLOW;
      memcpy(buffer_ptr, ansi_str.data(), buffer_capacity - 1);
    } else {
      memcpy(buffer_ptr, ansi_str.data(), ansi_str.size() + 1);
    }
    buffer_ptr[buffer_capacity - 1] = 0;  // \0
  }
  return result;
}
DECLARE_XBOXKRNL_EXPORT1(RtlUnicodeStringToAnsiString, kNone, kImplemented);

// https://msdn.microsoft.com/en-us/library/ff553113
dword_result_t RtlMultiByteToUnicodeN(lpword_t destination_ptr,
                                      dword_t destination_len,
                                      lpdword_t written_ptr,
                                      pointer_t<uint8_t> source_ptr,
                                      dword_t source_len) {
  uint32_t copy_len = destination_len >> 1;
  copy_len = copy_len < source_len ? copy_len : source_len.value();

  // TODO(benvanik): maybe use MultiByteToUnicode on Win32? would require
  // swapping.

  for (uint32_t i = 0; i < copy_len; i++) {
    destination_ptr[i] = source_ptr[i];
  }

  if (written_ptr.guest_address() != 0) {
    *written_ptr = copy_len << 1;
  }

  return 0;
}
DECLARE_XBOXKRNL_EXPORT3(RtlMultiByteToUnicodeN, kNone, kImplemented,
                         kHighFrequency, kSketchy);

// https://msdn.microsoft.com/en-us/library/ff553261
dword_result_t RtlUnicodeToMultiByteN(pointer_t<uint8_t> destination_ptr,
                                      dword_t destination_len,
                                      lpdword_t written_ptr,
                                      lpword_t source_ptr, dword_t source_len) {
  uint32_t copy_len = source_len >> 1;
  copy_len = copy_len < destination_len ? copy_len : destination_len.value();

  // TODO(benvanik): maybe use UnicodeToMultiByte on Win32?
  for (uint32_t i = 0; i < copy_len; i++) {
    uint16_t c = source_ptr[i];
    destination_ptr[i] = c < 256 ? (uint8_t)c : '?';
  }

  if (written_ptr.guest_address() != 0) {
    *written_ptr = copy_len;
  }

  return 0;
}
DECLARE_XBOXKRNL_EXPORT3(RtlUnicodeToMultiByteN, kNone, kImplemented,
                         kHighFrequency, kSketchy);

pointer_result_t RtlImageXexHeaderField(pointer_t<xex2_header> xex_header,
                                        dword_t field_dword) {
  uint32_t field_value = 0;
  uint32_t field = field_dword;  // VS acts weird going from dword_t -> enum

  UserModule::GetOptHeader(kernel_memory()->virtual_membase(), xex_header,
                           xex2_header_keys(field), &field_value);

  return field_value;
}
DECLARE_XBOXKRNL_EXPORT1(RtlImageXexHeaderField, kNone, kImplemented);

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
// Ref:
// https://web.archive.org/web/20161214022602/https://msdn.microsoft.com/en-us/magazine/cc164040.aspx
// Ref:
// https://github.com/reactos/reactos/blob/master/sdk/lib/rtl/critical.c

// This structure tries to match the one on the 360 as best I can figure out.
// Unfortunately some games have the critical sections pre-initialized in
// their embedded data and InitializeCriticalSection will never be called.
#pragma pack(push, 1)
struct X_RTL_CRITICAL_SECTION {
  X_DISPATCH_HEADER header;
  int32_t lock_count;               // 0x10 -1 -> 0 on first lock
  xe::be<int32_t> recursion_count;  // 0x14  0 -> 1 on first lock
  xe::be<uint32_t> owning_thread;   // 0x18 PKTHREAD 0 unless locked
};
#pragma pack(pop)
static_assert_size(X_RTL_CRITICAL_SECTION, 28);

void xeRtlInitializeCriticalSection(X_RTL_CRITICAL_SECTION* cs,
                                    uint32_t cs_ptr) {
  cs->header.type = 1;      // EventSynchronizationObject (auto reset)
  cs->header.absolute = 0;  // spin count div 256
  cs->header.signal_state = 0;
  cs->lock_count = -1;
  cs->recursion_count = 0;
  cs->owning_thread = 0;
}

void RtlInitializeCriticalSection(pointer_t<X_RTL_CRITICAL_SECTION> cs) {
  xeRtlInitializeCriticalSection(cs, cs.guest_address());
}
DECLARE_XBOXKRNL_EXPORT1(RtlInitializeCriticalSection, kNone, kImplemented);

X_STATUS xeRtlInitializeCriticalSectionAndSpinCount(X_RTL_CRITICAL_SECTION* cs,
                                                    uint32_t cs_ptr,
                                                    uint32_t spin_count) {
  // Spin count is rounded up to 256 intervals then packed in.
  // uint32_t spin_count_div_256 = (uint32_t)floor(spin_count / 256.0f + 0.5f);
  uint32_t spin_count_div_256 = (spin_count + 255) >> 8;
  if (spin_count_div_256 > 255) {
    spin_count_div_256 = 255;
  }

  cs->header.type = 1;  // EventSynchronizationObject (auto reset)
  cs->header.absolute = spin_count_div_256;
  cs->header.signal_state = 0;
  cs->lock_count = -1;
  cs->recursion_count = 0;
  cs->owning_thread = 0;

  return X_STATUS_SUCCESS;
}

dword_result_t RtlInitializeCriticalSectionAndSpinCount(
    pointer_t<X_RTL_CRITICAL_SECTION> cs, dword_t spin_count) {
  return xeRtlInitializeCriticalSectionAndSpinCount(cs, cs.guest_address(),
                                                    spin_count);
}
DECLARE_XBOXKRNL_EXPORT1(RtlInitializeCriticalSectionAndSpinCount, kNone,
                         kImplemented);

void RtlEnterCriticalSection(pointer_t<X_RTL_CRITICAL_SECTION> cs) {
  uint32_t cur_thread = XThread::GetCurrentThread()->guest_object();
  uint32_t spin_count = cs->header.absolute * 256;

  if (cs->owning_thread == cur_thread) {
    // We already own the lock.
    xe::atomic_inc(&cs->lock_count);
    cs->recursion_count++;
    return;
  }

  // Spin loop
  while (spin_count--) {
    if (xe::atomic_cas(-1, 0, &cs->lock_count)) {
      // Acquired.
      cs->owning_thread = cur_thread;
      cs->recursion_count = 1;
      return;
    }
  }

  if (xe::atomic_inc(&cs->lock_count) != 0) {
    // Create a full waiter.
    xeKeWaitForSingleObject(reinterpret_cast<void*>(cs.host_address()), 8, 0, 0,
                            nullptr);
  }

  assert_true(cs->owning_thread == 0);
  cs->owning_thread = cur_thread;
  cs->recursion_count = 1;
}
DECLARE_XBOXKRNL_EXPORT2(RtlEnterCriticalSection, kNone, kImplemented,
                         kHighFrequency);

dword_result_t RtlTryEnterCriticalSection(
    pointer_t<X_RTL_CRITICAL_SECTION> cs) {
  uint32_t thread = XThread::GetCurrentThread()->guest_object();

  if (xe::atomic_cas(-1, 0, &cs->lock_count)) {
    // Able to steal the lock right away.
    cs->owning_thread = thread;
    cs->recursion_count = 1;
    return 1;
  } else if (cs->owning_thread == thread) {
    // Already own the lock.
    xe::atomic_inc(&cs->lock_count);
    ++cs->recursion_count;
    return 1;
  }

  // Failed to acquire lock.
  return 0;
}
DECLARE_XBOXKRNL_EXPORT2(RtlTryEnterCriticalSection, kNone, kImplemented,
                         kHighFrequency);

void RtlLeaveCriticalSection(pointer_t<X_RTL_CRITICAL_SECTION> cs) {
  assert_true(cs->owning_thread == XThread::GetCurrentThread()->guest_object());

  // Drop recursion count - if it isn't zero we still have the lock.
  assert_true(cs->recursion_count > 0);
  if (--cs->recursion_count != 0) {
    assert_true(cs->recursion_count >= 0);

    xe::atomic_dec(&cs->lock_count);
    return;
  }

  // Not owned - unlock!
  cs->owning_thread = 0;
  if (xe::atomic_dec(&cs->lock_count) != -1) {
    // There were waiters - wake one of them.
    xeKeSetEvent(reinterpret_cast<X_KEVENT*>(cs.host_address()), 1, 0);
  }
}
DECLARE_XBOXKRNL_EXPORT2(RtlLeaveCriticalSection, kNone, kImplemented,
                         kHighFrequency);

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

// https://support.microsoft.com/en-us/kb/167296
void RtlTimeToTimeFields(lpqword_t time_ptr,
                         pointer_t<X_TIME_FIELDS> time_fields_ptr) {
  int64_t time_ms = time_ptr.value() / 10000 - 11644473600000LL;
  time_t timet = time_ms / 1000;
  struct tm* tm = gmtime(&timet);

  time_fields_ptr->year = tm->tm_year + 1900;
  time_fields_ptr->month = tm->tm_mon + 1;
  time_fields_ptr->day = tm->tm_mday;
  time_fields_ptr->hour = tm->tm_hour;
  time_fields_ptr->minute = tm->tm_min;
  time_fields_ptr->second = tm->tm_sec;
  time_fields_ptr->milliseconds = time_ms % 1000;
  time_fields_ptr->weekday = tm->tm_wday;
}
DECLARE_XBOXKRNL_EXPORT1(RtlTimeToTimeFields, kNone, kImplemented);

dword_result_t RtlTimeFieldsToTime(pointer_t<X_TIME_FIELDS> time_fields_ptr,
                                   lpqword_t time_ptr) {
  struct tm tm;
  tm.tm_year = time_fields_ptr->year - 1900;
  tm.tm_mon = time_fields_ptr->month - 1;
  tm.tm_mday = time_fields_ptr->day;
  tm.tm_hour = time_fields_ptr->hour;
  tm.tm_min = time_fields_ptr->minute;
  tm.tm_sec = time_fields_ptr->second;
  tm.tm_isdst = 0;
  time_t timet = timegm(&tm);
  if (timet == -1) {
    // set last error = ERROR_INVALID_PARAMETER
    return 0;
  }
  uint64_t time =
      ((timet + 11644473600LL) * 1000 + time_fields_ptr->milliseconds) * 10000;
  *time_ptr = time;
  return 1;
}
DECLARE_XBOXKRNL_EXPORT1(RtlTimeFieldsToTime, kNone, kImplemented);

void RegisterRtlExports(xe::cpu::ExportResolver* export_resolver,
                        KernelState* kernel_state) {}

}  // namespace xboxkrnl
}  // namespace kernel
}  // namespace xe
