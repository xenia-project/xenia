/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/xboxkrnl/xboxkrnl_rtl.h"

#include <algorithm>
#include <string>

#include "third_party/pe/pe_image.h"
#include "xenia/base/atomic.h"
#include "xenia/base/chrono.h"
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
namespace xe {
namespace kernel {
namespace xboxkrnl {
struct X_STRING {
  unsigned short length;
  unsigned short pad;
  uint32_t ptr;
};
// https://msdn.microsoft.com/en-us/library/ff561778
dword_result_t RtlCompareMemory_entry(lpvoid_t source1, lpvoid_t source2,
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
dword_result_t RtlCompareMemoryUlong_entry(lpvoid_t source, dword_t length,
                                           dword_t pattern) {
  uint32_t num_compared_bytes = 0;

  uint32_t swapped_pattern = xe::byte_swap(pattern.value());

  char* host_source = (char*)source.host_address();

  for (uint32_t aligned_length = length & 0xFFFFFFFCU; aligned_length;
       num_compared_bytes += 4) {
    if (*(uint32_t*)(host_source + num_compared_bytes) != swapped_pattern) {
      break;
    }
    aligned_length = aligned_length - 4;
  }
  return num_compared_bytes;
}
DECLARE_XBOXKRNL_EXPORT1(RtlCompareMemoryUlong, kMemory, kImplemented);

// https://msdn.microsoft.com/en-us/library/ff552263
void RtlFillMemoryUlong_entry(lpvoid_t destination, dword_t length,
                              dword_t pattern) {
  // NOTE: length must be % 4, so we can work on uint32s.
  uint32_t count = length >> 2;

  uint32_t* p = destination.as<uint32_t*>();
  uint32_t swapped_pattern = xe::byte_swap(pattern.value());
  for (uint32_t n = 0; n < count; n++, p++) {
    *p = swapped_pattern;
  }
}
DECLARE_XBOXKRNL_EXPORT1(RtlFillMemoryUlong, kMemory, kImplemented);

static constexpr const unsigned char rtl_lower_table[256] = {
    0x0,  0x1,  0x2,  0x3,  0x4,  0x5,  0x6,  0x7,  0x8,  0x9,  0xA,  0xB,
    0xC,  0xD,  0xE,  0xF,  0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
    0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23,
    0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F,
    0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x3B,
    0x3C, 0x3D, 0x3E, 0x3F, 0x40, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67,
    0x68, 0x69, 0x6A, 0x6B, 0x6C, 0x6D, 0x6E, 0x6F, 0x70, 0x71, 0x72, 0x73,
    0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A, 0x5B, 0x5C, 0x5D, 0x5E, 0x5F,
    0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6A, 0x6B,
    0x6C, 0x6D, 0x6E, 0x6F, 0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77,
    0x78, 0x79, 0x7A, 0x7B, 0x7C, 0x7D, 0x7E, 0x7F, 0x80, 0x81, 0x82, 0x83,
    0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8A, 0x8B, 0x8C, 0x8D, 0x8E, 0x8F,
    0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9A, 0x9B,
    0x9C, 0x9D, 0x9E, 0x9F, 0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7,
    0xA8, 0xA9, 0xAA, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF, 0xB0, 0xB1, 0xB2, 0xB3,
    0xB4, 0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xBB, 0xBC, 0xBD, 0xBE, 0xBF,
    0xE0, 0xE1, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA, 0xEB,
    0xEC, 0xED, 0xEE, 0xEF, 0xF0, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xD7,
    0xF8, 0xF9, 0xFA, 0xFB, 0xFC, 0xFD, 0xFE, 0xDF, 0xE0, 0xE1, 0xE2, 0xE3,
    0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA, 0xEB, 0xEC, 0xED, 0xEE, 0xEF,
    0xF0, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8, 0xF9, 0xFA, 0xFB,
    0xFC, 0xFD, 0xFE, 0xFF};

static constexpr const unsigned char rtl_upper_table[256] = {
    0x0,  0x1,  0x2,  0x3,  0x4,  0x5,  0x6,  0x7,  0x8,  0x9,  0xA,  0xB,
    0xC,  0xD,  0xE,  0xF,  0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
    0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23,
    0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F,
    0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x3B,
    0x3C, 0x3D, 0x3E, 0x3F, 0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47,
    0x48, 0x49, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F, 0x50, 0x51, 0x52, 0x53,
    0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x5B, 0x5C, 0x5D, 0x5E, 0x5F,
    0x60, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x4B,
    0x4C, 0x4D, 0x4E, 0x4F, 0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57,
    0x58, 0x59, 0x5A, 0x7B, 0x7C, 0x7D, 0x7E, 0x7F, 0x80, 0x81, 0x82, 0x83,
    0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8A, 0x8B, 0x8C, 0x8D, 0x8E, 0x8F,
    0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9A, 0x9B,
    0x9C, 0x9D, 0x9E, 0x9F, 0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7,
    0xA8, 0xA9, 0xAA, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF, 0xB0, 0xB1, 0xB2, 0xB3,
    0xB4, 0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xBB, 0xBC, 0xBD, 0xBE, 0xBF,
    0xC0, 0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xCB,
    0xCC, 0xCD, 0xCE, 0xCF, 0xD0, 0xD1, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7,
    0xD8, 0xD9, 0xDA, 0xDB, 0xDC, 0xDD, 0xDE, 0xDF, 0xC0, 0xC1, 0xC2, 0xC3,
    0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xCB, 0xCC, 0xCD, 0xCE, 0xCF,
    0xD0, 0xD1, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xF7, 0xD8, 0xD9, 0xDA, 0xDB,
    0xDC, 0xDD, 0xDE, 0x3F};

dword_result_t RtlUpperChar_entry(dword_t in) {
  return rtl_upper_table[in & 0xff];
}
DECLARE_XBOXKRNL_EXPORT1(RtlUpperChar, kNone, kImplemented);

dword_result_t RtlLowerChar_entry(dword_t in) {
  return rtl_lower_table[in & 0xff];
}
DECLARE_XBOXKRNL_EXPORT1(RtlLowerChar, kNone, kImplemented);

static int RtlCompareStringN_impl(uint8_t* string_1, unsigned int string_1_len,
                                  uint8_t* string_2, unsigned int string_2_len,
                                  int case_insensitive) {
  if (string_1_len == 0xFFFFFFFF) {
    uint8_t* string1_strlen_iter = string_1;
    while (*string1_strlen_iter++);
    string_1_len =
        static_cast<unsigned int>(string1_strlen_iter - string_1 - 1);
  }
  if (string_2_len == 0xFFFFFFFF) {
    uint8_t* string2_strlen_iter = string_2;
    while (*string2_strlen_iter++);
    string_2_len =
        static_cast<unsigned int>(string2_strlen_iter - string_2 - 1);
  }
  uint8_t* string1_end = &string_1[std::min(string_2_len, string_1_len)];
  if (case_insensitive) {
    while (string_1 < string1_end) {
      unsigned c1 = *string_1++;
      unsigned c2 = *string_2++;
      if (c1 != c2) {
        unsigned cu1 = rtl_upper_table[c1];
        unsigned cu2 = rtl_upper_table[c2];
        if (cu1 != cu2) {
          return cu1 - cu2;
        }
      }
    }
  } else {
    while (string_1 < string1_end) {
      unsigned c1 = *string_1++;
      unsigned c2 = *string_2++;
      if (c1 != c2) {
        return c1 - c2;
      }
    }
  }
  // why? not sure, but its the original logic
  return string_1_len - string_2_len;
}
dword_result_t RtlCompareStringN_entry(lpstring_t string_1,
                                       dword_t string_1_len,
                                       lpstring_t string_2,
                                       dword_t string_2_len,
                                       dword_t case_insensitive) {
  return RtlCompareStringN_impl(
      reinterpret_cast<uint8_t*>(string_1.host_address()), string_1_len,
      reinterpret_cast<uint8_t*>(string_2.host_address()), string_2_len,
      case_insensitive);
}

DECLARE_XBOXKRNL_EXPORT1(RtlCompareStringN, kNone, kImplemented);

dword_result_t RtlCompareString_entry(lpvoid_t string_1, lpvoid_t string_2,
                                      dword_t case_insensitive) {
  X_STRING* xs1 = string_1.as<X_STRING*>();
  X_STRING* xs2 = string_2.as<X_STRING*>();

  unsigned length_1 = xe::load_and_swap<uint16_t>(&xs1->length);
  unsigned length_2 = xe::load_and_swap<uint16_t>(&xs2->length);

  uint32_t ptr_1 = xe::load_and_swap<uint32_t>(&xs1->ptr);

  uint32_t ptr_2 = xe::load_and_swap<uint32_t>(&xs2->ptr);

  auto kmem = kernel_memory();

  return RtlCompareStringN_impl(
      kmem->TranslateVirtual<uint8_t*>(ptr_1), length_1,
      kmem->TranslateVirtual<uint8_t*>(ptr_2), length_2, case_insensitive);
}
DECLARE_XBOXKRNL_EXPORT1(RtlCompareString, kNone, kImplemented);
// https://msdn.microsoft.com/en-us/library/ff561918
void RtlInitAnsiString_entry(pointer_t<X_ANSI_STRING> destination,
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
// https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-rtlupcaseunicodechar
dword_result_t RtlUpcaseUnicodeChar_entry(dword_t SourceCharacter) {
  return std::use_facet<std::ctype<char16_t>>(std::locale())
      .toupper(SourceCharacter);
}
DECLARE_XBOXKRNL_EXPORT1(RtlUpcaseUnicodeChar, kNone, kImplemented);

// https://msdn.microsoft.com/en-us/library/ff561899
void RtlFreeAnsiString_entry(pointer_t<X_ANSI_STRING> string) {
  if (string->pointer) {
    kernel_memory()->SystemHeapFree(string->pointer);
  }

  string->reset();
}
DECLARE_XBOXKRNL_EXPORT1(RtlFreeAnsiString, kNone, kImplemented);

// https://msdn.microsoft.com/en-us/library/ff561934
pointer_result_t RtlInitUnicodeString_entry(
    pointer_t<X_UNICODE_STRING> destination, lpu16string_t source) {
  if (source) {
    destination->length = (uint16_t)source.value().size() * 2;
    destination->maximum_length = (uint16_t)(source.value().size() + 1) * 2;
    destination->pointer = source.guest_address();
  } else {
    destination->reset();
  }
  return destination.guest_address();
}
DECLARE_XBOXKRNL_EXPORT1(RtlInitUnicodeString, kNone, kImplemented);

// https://msdn.microsoft.com/en-us/library/ff561903
void RtlFreeUnicodeString_entry(pointer_t<X_UNICODE_STRING> string) {
  if (string->pointer) {
    kernel_memory()->SystemHeapFree(string->pointer);
  }

  string->reset();
}
DECLARE_XBOXKRNL_EXPORT1(RtlFreeUnicodeString, kNone, kImplemented);

void RtlCopyString_entry(pointer_t<X_ANSI_STRING> destination,
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

void RtlCopyUnicodeString_entry(pointer_t<X_UNICODE_STRING> destination,
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
dword_result_t RtlUnicodeStringToAnsiString_entry(
    pointer_t<X_ANSI_STRING> destination_ptr,
    pointer_t<X_UNICODE_STRING> source_ptr, dword_t alloc_dest) {
  // NTSTATUS
  // _Inout_  PANSI_STRING DestinationString,
  // _In_     PCUNICODE_STRING SourceString,
  // _In_     BOOLEAN AllocateDestinationString

  std::u16string unicode_str =
      util::TranslateUnicodeString(kernel_memory(), source_ptr);
  std::string ansi_str = xe::to_utf8(unicode_str);
  if (ansi_str.size() > 0xFFFF - 1) {
    return X_STATUS_INVALID_PARAMETER_2;
  }

  X_STATUS result = X_STATUS_SUCCESS;
  if (alloc_dest) {
    uint32_t buffer_ptr =
        kernel_memory()->SystemHeapAlloc(uint32_t(ansi_str.size() + 1));

    memcpy(kernel_memory()->TranslateVirtual(buffer_ptr), ansi_str.data(),
           ansi_str.size() + 1);
    destination_ptr->length = static_cast<uint16_t>(ansi_str.size());
    destination_ptr->maximum_length =
        static_cast<uint16_t>(ansi_str.size() + 1);
    destination_ptr->pointer = static_cast<uint32_t>(buffer_ptr);
  } else {
    uint32_t buffer_capacity = destination_ptr->maximum_length;
    auto buffer_ptr =
        kernel_memory()->TranslateVirtual(destination_ptr->pointer);
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
dword_result_t RtlMultiByteToUnicodeN_entry(lpword_t destination_ptr,
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
dword_result_t RtlUnicodeToMultiByteN_entry(pointer_t<uint8_t> destination_ptr,
                                            dword_t destination_len,
                                            lpdword_t written_ptr,
                                            lpword_t source_ptr,
                                            dword_t source_len) {
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

// https://undocumented.ntinternals.net/UserMode/Undocumented%20Functions/Executable%20Images/RtlImageNtHeader.html
static IMAGE_NT_HEADERS32* ImageNtHeader(uint8_t* module) {
  if (!module) {
    return 0;
  }

  // Little-endian! no swapping!

  auto dos_header = module;
  auto dos_magic = *reinterpret_cast<const uint16_t*>(&dos_header[0x00]);
  if (dos_magic != 0x5A4D) {  // 'MZ'
    return 0;
  }
  auto dos_lfanew = *reinterpret_cast<const int32_t*>(&dos_header[0x3C]);

  auto nt_header = &dos_header[dos_lfanew];
  auto nt_magic = *reinterpret_cast<const uint32_t*>(&nt_header[0x00]);
  if (nt_magic != 0x4550) {  // 'PE'
    return 0;
  }
  return reinterpret_cast<IMAGE_NT_HEADERS32*>(nt_header);
}

pointer_result_t RtlImageNtHeader_entry(lpvoid_t module) {
  auto result = ImageNtHeader(module.as<uint8_t*>());
  if (!result) {
    return 0;
  }

  return kernel_memory()->HostToGuestVirtual(result);
}
DECLARE_XBOXKRNL_EXPORT1(RtlImageNtHeader, kNone, kImplemented);
// https://learn.microsoft.com/en-us/windows/win32/api/dbghelp/nf-dbghelp-imagedirectoryentrytodata
dword_result_t RtlImageDirectoryEntryToData_entry(dword_t Base,
                                                  dword_t MappedAsImage_,
                                                  word_t DirectoryEntry,
                                                  dword_t Size,
                                                  const ppc_context_t& ctx) {
  bool MappedAsImage = static_cast<unsigned char>(MappedAsImage_);
  uint32_t aligned_base = Base;
  if ((Base & 1) != 0) {
    aligned_base = Base & 0xFFFFFFFE;
    MappedAsImage = false;
  }
  IMAGE_NT_HEADERS32* nt_header =
      ImageNtHeader(ctx->TranslateVirtual<uint8_t*>(aligned_base));

  if (!nt_header) {
    return 0;
  }
  if (nt_header->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC) {
    return 0;
  }
  if (DirectoryEntry >= nt_header->OptionalHeader.NumberOfRvaAndSizes) {
    return 0;
  }
  uint32_t Address =
      nt_header->OptionalHeader.DataDirectory[DirectoryEntry].VirtualAddress;
  if (!Address) {
    return 0;
  }
  xe::store_and_swap<uint32_t>(
      ctx->TranslateVirtual(Size),
      nt_header->OptionalHeader.DataDirectory[DirectoryEntry].Size);
  if (MappedAsImage || Address < nt_header->OptionalHeader.SizeOfHeaders) {
    return aligned_base + Address;
  }

  uint32_t n_sections = nt_header->FileHeader.NumberOfSections;
  IMAGE_SECTION_HEADER* v8 = reinterpret_cast<IMAGE_SECTION_HEADER*>(
      reinterpret_cast<char*>(&nt_header->OptionalHeader) +
      nt_header->FileHeader.SizeOfOptionalHeader);
  if (!n_sections) {
    return 0;
  }

  uint32_t i = 0;
  while (true) {
    uint32_t section_virtual_address = v8->VirtualAddress;
    uint32_t sizeof_section = v8->SizeOfRawData;
    if (Address >= section_virtual_address &&
        Address < sizeof_section + section_virtual_address) {
      break;
    }
    ++i;
    ++v8;
    if (i >= n_sections) {
      return 0;
    }
  }

  if (v8) {
    return aligned_base + Address - v8->VirtualAddress;
  }
  return 0;
}

DECLARE_XBOXKRNL_EXPORT1(RtlImageDirectoryEntryToData, kNone, kImplemented);

pointer_result_t RtlImageXexHeaderField_entry(pointer_t<xex2_header> xex_header,
                                              dword_t field_dword) {
  uint32_t field_value = 0;
  uint32_t field = field_dword;  // VS acts weird going from dword_t -> enum

  if (!xex_header) {
    return field_value;
  }

  UserModule::GetOptHeader(kernel_memory(), xex_header, xex2_header_keys(field),
                           &field_value);

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

void RtlInitializeCriticalSection_entry(pointer_t<X_RTL_CRITICAL_SECTION> cs) {
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

dword_result_t RtlInitializeCriticalSectionAndSpinCount_entry(
    pointer_t<X_RTL_CRITICAL_SECTION> cs, dword_t spin_count) {
  return xeRtlInitializeCriticalSectionAndSpinCount(cs, cs.guest_address(),
                                                    spin_count);
}
DECLARE_XBOXKRNL_EXPORT1(RtlInitializeCriticalSectionAndSpinCount, kNone,
                         kImplemented);

static void CriticalSectionPrefetchW(const void* vp) {
#if XE_ARCH_AMD64 == 1
  if (amd64::GetFeatureFlags() & amd64::kX64EmitPrefetchW) {
    swcache::PrefetchW(vp);
  }
#endif
}

void RtlEnterCriticalSection_entry(pointer_t<X_RTL_CRITICAL_SECTION> cs) {
  if (!cs.guest_address()) {
    XELOGE("Null critical section in RtlEnterCriticalSection!");
    return;
  }
  CriticalSectionPrefetchW(&cs->lock_count);
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

dword_result_t RtlTryEnterCriticalSection_entry(
    pointer_t<X_RTL_CRITICAL_SECTION> cs) {
  if (!cs.guest_address()) {
    XELOGE("Null critical section in RtlTryEnterCriticalSection!");
    return 1;  // pretend we got the critical section.
  }
  CriticalSectionPrefetchW(&cs->lock_count);
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

void RtlLeaveCriticalSection_entry(pointer_t<X_RTL_CRITICAL_SECTION> cs) {
  if (!cs.guest_address()) {
    XELOGE("Null critical section in RtlLeaveCriticalSection!");
    return;
  }
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
static_assert_size(X_TIME_FIELDS, 16);

// https://docs.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-rtltimetotimefields
void RtlTimeToTimeFields_entry(lpqword_t time_ptr,
                               pointer_t<X_TIME_FIELDS> time_fields_ptr) {
  // Use host clock because we don't want scaling to be applied, just conversion
  using xe::chrono::WinSystemClock;
  auto tp =
      WinSystemClock::to_sys(WinSystemClock::from_file_time(time_ptr.value()));
  auto dp = date::floor<date::days>(tp);
  auto year_month_day = date::year_month_day{dp};
  auto weekday = date::weekday{dp};
  auto time = date::hh_mm_ss{date::floor<std::chrono::milliseconds>(tp - dp)};
  time_fields_ptr->year = static_cast<int>(year_month_day.year());
  time_fields_ptr->month = static_cast<unsigned>(year_month_day.month());
  time_fields_ptr->day = static_cast<unsigned>(year_month_day.day());
  time_fields_ptr->weekday = weekday.c_encoding();
  time_fields_ptr->hour = time.hours().count();
  time_fields_ptr->minute = time.minutes().count();
  time_fields_ptr->second = static_cast<uint16_t>(time.seconds().count());
  time_fields_ptr->milliseconds =
      static_cast<uint16_t>(time.subseconds().count());
}
DECLARE_XBOXKRNL_EXPORT1(RtlTimeToTimeFields, kNone, kImplemented);

// https://docs.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-rtltimefieldstotime
dword_result_t RtlTimeFieldsToTime_entry(
    pointer_t<X_TIME_FIELDS> time_fields_ptr, lpqword_t time_ptr) {
  using xe::chrono::WinSystemClock;
  if (time_fields_ptr->year < 1601 || time_fields_ptr->month < 1 ||
      time_fields_ptr->month > 12 || time_fields_ptr->day < 1 ||
      time_fields_ptr->day > 31 || time_fields_ptr->hour > 23 ||
      time_fields_ptr->minute > 59 || time_fields_ptr->second > 59 ||
      time_fields_ptr->milliseconds > 999) {
    return 0;
  }
  auto year = date::year{time_fields_ptr->year};
  auto month = date::month{time_fields_ptr->month};
  auto day = date::day{time_fields_ptr->day};
  auto year_month_day = date::year_month_day{year, month, day};
  if (!year_month_day.ok()) {
    return 0;
  }
  auto dp = static_cast<date::sys_days>(year_month_day);
  std::chrono::system_clock::time_point time = dp;
  time += std::chrono::hours{time_fields_ptr->hour};
  time += std::chrono::minutes{time_fields_ptr->minute};
  time += std::chrono::seconds{time_fields_ptr->second};
  time += std::chrono::milliseconds{time_fields_ptr->milliseconds};
  *time_ptr = WinSystemClock::to_file_time(WinSystemClock::from_sys(time));
  return 1;
}
DECLARE_XBOXKRNL_EXPORT1(RtlTimeFieldsToTime, kNone, kImplemented);

static uint32_t crc32_table[256] = {
    0x00000000u, 0x77073096u, 0xEE0E612Cu, 0x990951BAu, 0x076DC419u,
    0x706AF48Fu, 0xE963A535u, 0x9E6495A3u, 0x0EDB8832u, 0x79DCB8A4u,
    0xE0D5E91Eu, 0x97D2D988u, 0x09B64C2Bu, 0x7EB17CBDu, 0xE7B82D07u,
    0x90BF1D91u, 0x1DB71064u, 0x6AB020F2u, 0xF3B97148u, 0x84BE41DEu,
    0x1ADAD47Du, 0x6DDDE4EBu, 0xF4D4B551u, 0x83D385C7u, 0x136C9856u,
    0x646BA8C0u, 0xFD62F97Au, 0x8A65C9ECu, 0x14015C4Fu, 0x63066CD9u,
    0xFA0F3D63u, 0x8D080DF5u, 0x3B6E20C8u, 0x4C69105Eu, 0xD56041E4u,
    0xA2677172u, 0x3C03E4D1u, 0x4B04D447u, 0xD20D85FDu, 0xA50AB56Bu,
    0x35B5A8FAu, 0x42B2986Cu, 0xDBBBC9D6u, 0xACBCF940u, 0x32D86CE3u,
    0x45DF5C75u, 0xDCD60DCFu, 0xABD13D59u, 0x26D930ACu, 0x51DE003Au,
    0xC8D75180u, 0xBFD06116u, 0x21B4F4B5u, 0x56B3C423u, 0xCFBA9599u,
    0xB8BDA50Fu, 0x2802B89Eu, 0x5F058808u, 0xC60CD9B2u, 0xB10BE924u,
    0x2F6F7C87u, 0x58684C11u, 0xC1611DABu, 0xB6662D3Du, 0x76DC4190u,
    0x01DB7106u, 0x98D220BCu, 0xEFD5102Au, 0x71B18589u, 0x06B6B51Fu,
    0x9FBFE4A5u, 0xE8B8D433u, 0x7807C9A2u, 0x0F00F934u, 0x9609A88Eu,
    0xE10E9818u, 0x7F6A0DBBu, 0x086D3D2Du, 0x91646C97u, 0xE6635C01u,
    0x6B6B51F4u, 0x1C6C6162u, 0x856530D8u, 0xF262004Eu, 0x6C0695EDu,
    0x1B01A57Bu, 0x8208F4C1u, 0xF50FC457u, 0x65B0D9C6u, 0x12B7E950u,
    0x8BBEB8EAu, 0xFCB9887Cu, 0x62DD1DDFu, 0x15DA2D49u, 0x8CD37CF3u,
    0xFBD44C65u, 0x4DB26158u, 0x3AB551CEu, 0xA3BC0074u, 0xD4BB30E2u,
    0x4ADFA541u, 0x3DD895D7u, 0xA4D1C46Du, 0xD3D6F4FBu, 0x4369E96Au,
    0x346ED9FCu, 0xAD678846u, 0xDA60B8D0u, 0x44042D73u, 0x33031DE5u,
    0xAA0A4C5Fu, 0xDD0D7CC9u, 0x5005713Cu, 0x270241AAu, 0xBE0B1010u,
    0xC90C2086u, 0x5768B525u, 0x206F85B3u, 0xB966D409u, 0xCE61E49Fu,
    0x5EDEF90Eu, 0x29D9C998u, 0xB0D09822u, 0xC7D7A8B4u, 0x59B33D17u,
    0x2EB40D81u, 0xB7BD5C3Bu, 0xC0BA6CADu, 0xEDB88320u, 0x9ABFB3B6u,
    0x03B6E20Cu, 0x74B1D29Au, 0xEAD54739u, 0x9DD277AFu, 0x04DB2615u,
    0x73DC1683u, 0xE3630B12u, 0x94643B84u, 0x0D6D6A3Eu, 0x7A6A5AA8u,
    0xE40ECF0Bu, 0x9309FF9Du, 0x0A00AE27u, 0x7D079EB1u, 0xF00F9344u,
    0x8708A3D2u, 0x1E01F268u, 0x6906C2FEu, 0xF762575Du, 0x806567CBu,
    0x196C3671u, 0x6E6B06E7u, 0xFED41B76u, 0x89D32BE0u, 0x10DA7A5Au,
    0x67DD4ACCu, 0xF9B9DF6Fu, 0x8EBEEFF9u, 0x17B7BE43u, 0x60B08ED5u,
    0xD6D6A3E8u, 0xA1D1937Eu, 0x38D8C2C4u, 0x4FDFF252u, 0xD1BB67F1u,
    0xA6BC5767u, 0x3FB506DDu, 0x48B2364Bu, 0xD80D2BDAu, 0xAF0A1B4Cu,
    0x36034AF6u, 0x41047A60u, 0xDF60EFC3u, 0xA867DF55u, 0x316E8EEFu,
    0x4669BE79u, 0xCB61B38Cu, 0xBC66831Au, 0x256FD2A0u, 0x5268E236u,
    0xCC0C7795u, 0xBB0B4703u, 0x220216B9u, 0x5505262Fu, 0xC5BA3BBEu,
    0xB2BD0B28u, 0x2BB45A92u, 0x5CB36A04u, 0xC2D7FFA7u, 0xB5D0CF31u,
    0x2CD99E8Bu, 0x5BDEAE1Du, 0x9B64C2B0u, 0xEC63F226u, 0x756AA39Cu,
    0x026D930Au, 0x9C0906A9u, 0xEB0E363Fu, 0x72076785u, 0x05005713u,
    0x95BF4A82u, 0xE2B87A14u, 0x7BB12BAEu, 0x0CB61B38u, 0x92D28E9Bu,
    0xE5D5BE0Du, 0x7CDCEFB7u, 0x0BDBDF21u, 0x86D3D2D4u, 0xF1D4E242u,
    0x68DDB3F8u, 0x1FDA836Eu, 0x81BE16CDu, 0xF6B9265Bu, 0x6FB077E1u,
    0x18B74777u, 0x88085AE6u, 0xFF0F6A70u, 0x66063BCAu, 0x11010B5Cu,
    0x8F659EFFu, 0xF862AE69u, 0x616BFFD3u, 0x166CCF45u, 0xA00AE278u,
    0xD70DD2EEu, 0x4E048354u, 0x3903B3C2u, 0xA7672661u, 0xD06016F7u,
    0x4969474Du, 0x3E6E77DBu, 0xAED16A4Au, 0xD9D65ADCu, 0x40DF0B66u,
    0x37D83BF0u, 0xA9BCAE53u, 0xDEBB9EC5u, 0x47B2CF7Fu, 0x30B5FFE9u,
    0xBDBDF21Cu, 0xCABAC28Au, 0x53B39330u, 0x24B4A3A6u, 0xBAD03605u,
    0xCDD70693u, 0x54DE5729u, 0x23D967BFu, 0xB3667A2Eu, 0xC4614AB8u,
    0x5D681B02u, 0x2A6F2B94u, 0xB40BBE37u, 0xC30C8EA1u, 0x5A05DF1Bu,
    0x2D02EF8Du,
};

dword_result_t RtlComputeCrc32_entry(dword_t seed, lpvoid_t buffer,
                                     dword_t length) {
  if (!length) {
    return seed.value();
  }
  uint32_t hash = ~seed;
  for (uint32_t i = 0; i < length; ++i) {
    hash = crc32_table[buffer[i] ^ (hash & 0xFF)] ^ (hash >> 8);
  }
  return ~hash;
}
DECLARE_XBOXKRNL_EXPORT1(RtlComputeCrc32, kNone, kImplemented);

static void RtlRip_entry(const ppc_context_t& ctx) {
  uint32_t arg1 = static_cast<uint32_t>(ctx->r[3]);
  uint32_t arg2 = static_cast<uint32_t>(ctx->r[4]);
  const char* msg_str1 = "";

  const char* msg_str2 = "";

  if (arg1) {
    msg_str1 = ctx->TranslateVirtual<const char*>(arg1);
  }

  if (arg2) {
    msg_str2 = ctx->TranslateVirtual<const char*>(arg2);
  }

  XELOGE("RtlRip called, arg1 = {}, arg2 = {}\n", msg_str1, msg_str2);

  // we should break here... not sure what to do exactly
}
DECLARE_XBOXKRNL_EXPORT1(RtlRip, kNone, kImportant);

void RtlGetStackLimits_entry(lpdword_t out_end, lpdword_t out_base,
                             const ppc_context_t& ctx) {
  auto kpcr = ctx->TranslateVirtualGPR<X_KPCR*>(ctx->r[13]);

  uint32_t stack_base;
  uint32_t stack_end;

  if (kpcr->use_alternative_stack) {
    stack_base = kpcr->alt_stack_base_ptr;
    stack_end = kpcr->alt_stack_end_ptr;
  } else {
    stack_base = kpcr->stack_base_ptr;
    stack_end = kpcr->stack_end_ptr;
  }
  *out_base = stack_base;
  *out_end = stack_end;
}
DECLARE_XBOXKRNL_EXPORT1(RtlGetStackLimits, kNone, kImplemented);

}  // namespace xboxkrnl
}  // namespace kernel
}  // namespace xe

DECLARE_XBOXKRNL_EMPTY_REGISTER_EXPORTS(Rtl);
