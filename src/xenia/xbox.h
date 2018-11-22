/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_XBOX_H_
#define XENIA_XBOX_H_

#include <string>

#include "xenia/base/memory.h"

// TODO(benvanik): split this header, cleanup, etc.
// clang-format off

namespace xe {

#pragma pack(push, 4)

typedef uint32_t X_HANDLE;
#define X_INVALID_HANDLE_VALUE ((X_HANDLE)-1)

// TODO(benvanik): type all of this so we get some safety.

// NT_STATUS (STATUS_*)
// https://msdn.microsoft.com/en-us/library/cc704588.aspx
// Adding as needed.
typedef uint32_t X_STATUS;
#define XSUCCEEDED(s)     ((s & 0xC0000000) == 0)
#define XFAILED(s)        (!XSUCCEEDED(s))
#define X_STATUS_SUCCESS                                ((X_STATUS)0x00000000L)
#define X_STATUS_ABANDONED_WAIT_0                       ((X_STATUS)0x00000080L)
#define X_STATUS_USER_APC                               ((X_STATUS)0x000000C0L)
#define X_STATUS_ALERTED                                ((X_STATUS)0x00000101L)
#define X_STATUS_TIMEOUT                                ((X_STATUS)0x00000102L)
#define X_STATUS_PENDING                                ((X_STATUS)0x00000103L)
#define X_STATUS_OBJECT_NAME_EXISTS                     ((X_STATUS)0x40000000L)
#define X_STATUS_TIMER_RESUME_IGNORED                   ((X_STATUS)0x40000025L)
#define X_STATUS_BUFFER_OVERFLOW                        ((X_STATUS)0x80000005L)
#define X_STATUS_NO_MORE_FILES                          ((X_STATUS)0x80000006L)
#define X_STATUS_UNSUCCESSFUL                           ((X_STATUS)0xC0000001L)
#define X_STATUS_NOT_IMPLEMENTED                        ((X_STATUS)0xC0000002L)
#define X_STATUS_INVALID_INFO_CLASS                     ((X_STATUS)0xC0000003L)
#define X_STATUS_INFO_LENGTH_MISMATCH                   ((X_STATUS)0xC0000004L)
#define X_STATUS_ACCESS_VIOLATION                       ((X_STATUS)0xC0000005L)
#define X_STATUS_INVALID_HANDLE                         ((X_STATUS)0xC0000008L)
#define X_STATUS_INVALID_PARAMETER                      ((X_STATUS)0xC000000DL)
#define X_STATUS_NO_SUCH_FILE                           ((X_STATUS)0xC000000FL)
#define X_STATUS_END_OF_FILE                            ((X_STATUS)0xC0000011L)
#define X_STATUS_NO_MEMORY                              ((X_STATUS)0xC0000017L)
#define X_STATUS_ALREADY_COMMITTED                      ((X_STATUS)0xC0000021L)
#define X_STATUS_ACCESS_DENIED                          ((X_STATUS)0xC0000022L)
#define X_STATUS_BUFFER_TOO_SMALL                       ((X_STATUS)0xC0000023L)
#define X_STATUS_OBJECT_TYPE_MISMATCH                   ((X_STATUS)0xC0000024L)
#define X_STATUS_OBJECT_NAME_NOT_FOUND                  ((X_STATUS)0xC0000034L)
#define X_STATUS_OBJECT_NAME_COLLISION                  ((X_STATUS)0xC0000035L)
#define X_STATUS_INVALID_PAGE_PROTECTION                ((X_STATUS)0xC0000045L)
#define X_STATUS_MUTANT_NOT_OWNED                       ((X_STATUS)0xC0000046L)
#define X_STATUS_PROCEDURE_NOT_FOUND                    ((X_STATUS)0xC000007AL)
#define X_STATUS_INSUFFICIENT_RESOURCES                 ((X_STATUS)0xC000009AL)
#define X_STATUS_MEMORY_NOT_ALLOCATED                   ((X_STATUS)0xC00000A0L)
#define X_STATUS_NOT_SUPPORTED                          ((X_STATUS)0xC00000BBL)
#define X_STATUS_INVALID_PARAMETER_1                    ((X_STATUS)0xC00000EFL)
#define X_STATUS_INVALID_PARAMETER_2                    ((X_STATUS)0xC00000F0L)
#define X_STATUS_INVALID_PARAMETER_3                    ((X_STATUS)0xC00000F1L)
#define X_STATUS_DLL_NOT_FOUND                          ((X_STATUS)0xC0000135L)
#define X_STATUS_ENTRYPOINT_NOT_FOUND                   ((X_STATUS)0xC0000139L)
#define X_STATUS_MAPPED_ALIGNMENT                       ((X_STATUS)0xC0000220L)
#define X_STATUS_NOT_FOUND                              ((X_STATUS)0xC0000225L)
#define X_STATUS_DRIVER_ORDINAL_NOT_FOUND               ((X_STATUS)0xC0000262L)
#define X_STATUS_DRIVER_ENTRYPOINT_NOT_FOUND            ((X_STATUS)0xC0000263L)

// Win32 error codes (ERROR_*)
// https://msdn.microsoft.com/en-us/library/windows/desktop/ms681381(v=vs.85).aspx
// Adding as needed.
typedef uint32_t X_RESULT;
#define X_FACILITY_WIN32 0x0007
#define X_RESULT_FROM_WIN32(x) ((X_RESULT)(x))

#define X_ERROR_SUCCESS                         X_RESULT_FROM_WIN32(0x00000000L)
#define X_ERROR_FILE_NOT_FOUND                  X_RESULT_FROM_WIN32(0x00000002L)
#define X_ERROR_PATH_NOT_FOUND                  X_RESULT_FROM_WIN32(0x00000003L)
#define X_ERROR_ACCESS_DENIED                   X_RESULT_FROM_WIN32(0x00000005L)
#define X_ERROR_INVALID_HANDLE                  X_RESULT_FROM_WIN32(0x00000006L)
#define X_ERROR_NO_MORE_FILES                   X_RESULT_FROM_WIN32(0x00000012L)
#define X_ERROR_INVALID_PARAMETER               X_RESULT_FROM_WIN32(0x00000057L)
#define X_ERROR_IO_PENDING                      X_RESULT_FROM_WIN32(0x000003E5L)
#define X_ERROR_INSUFFICIENT_BUFFER             X_RESULT_FROM_WIN32(0x0000007AL)
#define X_ERROR_INVALID_NAME                    X_RESULT_FROM_WIN32(0x0000007BL)
#define X_ERROR_BAD_ARGUMENTS                   X_RESULT_FROM_WIN32(0x000000A0L)
#define X_ERROR_BUSY                            X_RESULT_FROM_WIN32(0x000000AAL)
#define X_ERROR_ALREADY_EXISTS                  X_RESULT_FROM_WIN32(0x000000B7L)
#define X_ERROR_DEVICE_NOT_CONNECTED            X_RESULT_FROM_WIN32(0x0000048FL)
#define X_ERROR_NOT_FOUND                       X_RESULT_FROM_WIN32(0x00000490L)
#define X_ERROR_CANCELLED                       X_RESULT_FROM_WIN32(0x000004C7L)
#define X_ERROR_NOT_LOGGED_ON                   X_RESULT_FROM_WIN32(0x000004DDL)
#define X_ERROR_NO_SUCH_USER                    X_RESULT_FROM_WIN32(0x00000525L)
#define X_ERROR_FUNCTION_FAILED                 X_RESULT_FROM_WIN32(0x0000065BL)
#define X_ERROR_EMPTY                           X_RESULT_FROM_WIN32(0x000010D2L)

// HRESULT codes
typedef uint32_t X_HRESULT;
#define X_HRESULT_FROM_WIN32(x) ((int32_t)(x) <= 0 \
                                  ? (static_cast<X_HRESULT>(x)) \
                                  : (static_cast<X_HRESULT>(((x) & 0xFFFF) | (X_FACILITY_WIN32 << 16) | \
                                    0x80000000L)))

#define X_E_FALSE                               static_cast<X_HRESULT>(0x80000000L)
#define X_E_SUCCESS                             X_HRESULT_FROM_WIN32(X_ERROR_SUCCESS)
#define X_E_INVALIDARG                          X_HRESULT_FROM_WIN32(X_ERROR_INVALID_PARAMETER)
#define X_E_DEVICE_NOT_CONNECTED                X_HRESULT_FROM_WIN32(X_ERROR_DEVICE_NOT_CONNECTED)
#define X_E_NO_SUCH_USER                        X_HRESULT_FROM_WIN32(X_ERROR_NO_SUCH_USER)

// MEM_*, used by NtAllocateVirtualMemory
#define X_MEM_COMMIT              0x00001000
#define X_MEM_RESERVE             0x00002000
#define X_MEM_DECOMMIT            0x00004000
#define X_MEM_RELEASE             0x00008000
#define X_MEM_FREE                0x00010000
#define X_MEM_PRIVATE             0x00020000
#define X_MEM_RESET               0x00080000
#define X_MEM_TOP_DOWN            0x00100000
#define X_MEM_NOZERO              0x00800000
#define X_MEM_LARGE_PAGES         0x20000000
#define X_MEM_HEAP                0x40000000
#define X_MEM_16MB_PAGES          0x80000000  // from Valve SDK

// PAGE_*, used by NtAllocateVirtualMemory
#define X_PAGE_NOACCESS           0x00000001
#define X_PAGE_READONLY           0x00000002
#define X_PAGE_READWRITE          0x00000004
#define X_PAGE_WRITECOPY          0x00000008
#define X_PAGE_EXECUTE            0x00000010
#define X_PAGE_EXECUTE_READ       0x00000020
#define X_PAGE_EXECUTE_READWRITE  0x00000040
#define X_PAGE_EXECUTE_WRITECOPY  0x00000080
#define X_PAGE_GUARD              0x00000100
#define X_PAGE_NOCACHE            0x00000200
#define X_PAGE_WRITECOMBINE       0x00000400

// Sockets/networking.
#define X_INVALID_SOCKET (uint32_t)(~0)
#define X_SOCKET_ERROR (uint32_t)(-1)

// clang-format on

enum X_FILE_ATTRIBUTES : uint32_t {
  X_FILE_ATTRIBUTE_NONE = 0x0000,
  X_FILE_ATTRIBUTE_READONLY = 0x0001,
  X_FILE_ATTRIBUTE_HIDDEN = 0x0002,
  X_FILE_ATTRIBUTE_SYSTEM = 0x0004,
  X_FILE_ATTRIBUTE_DIRECTORY = 0x0010,
  X_FILE_ATTRIBUTE_ARCHIVE = 0x0020,
  X_FILE_ATTRIBUTE_DEVICE = 0x0040,
  X_FILE_ATTRIBUTE_NORMAL = 0x0080,
  X_FILE_ATTRIBUTE_TEMPORARY = 0x0100,
  X_FILE_ATTRIBUTE_COMPRESSED = 0x0800,
  X_FILE_ATTRIBUTE_ENCRYPTED = 0x4000,
};

// https://github.com/oukiar/vdash/blob/master/vdash/include/kernel.h
enum X_FILE_INFORMATION_CLASS {
  XFileDirectoryInformation = 1,
  XFileFullDirectoryInformation,
  XFileBothDirectoryInformation,
  XFileBasicInformation,
  XFileStandardInformation,
  XFileInternalInformation,
  XFileEaInformation,
  XFileAccessInformation,
  XFileNameInformation,
  XFileRenameInformation,
  XFileLinkInformation,
  XFileNamesInformation,
  XFileDispositionInformation,
  XFilePositionInformation,
  XFileFullEaInformation,
  XFileModeInformation,
  XFileAlignmentInformation,
  XFileAllInformation,
  XFileAllocationInformation,
  XFileEndOfFileInformation,
  XFileAlternateNameInformation,
  XFileStreamInformation,
  XFileMountPartitionInformation,
  XFileMountPartitionsInformation,
  XFilePipeRemoteInformation,
  XFileSectorInformation,
  XFileXctdCompressionInformation,
  XFileCompressionInformation,
  XFileObjectIdInformation,
  XFileCompletionInformation,
  XFileMoveClusterInformation,
  XFileIoPriorityInformation,
  XFileReparsePointInformation,
  XFileNetworkOpenInformation,
  XFileAttributeTagInformation,
  XFileTrackingInformation,
  XFileMaximumInformation
};

// Known as XOVERLAPPED to 360 code.
struct XAM_OVERLAPPED {
  xe::be<uint32_t> result;              // 0x0
  xe::be<uint32_t> length;              // 0x4
  xe::be<uint32_t> context;             // 0x8
  xe::be<uint32_t> event;               // 0xC
  xe::be<uint32_t> completion_routine;  // 0x10
  xe::be<uint32_t> completion_context;  // 0x14
  xe::be<uint32_t> extended_error;      // 0x18
};

inline uint32_t XOverlappedGetResult(void* ptr) {
  auto p = reinterpret_cast<uint32_t*>(ptr);
  return xe::load_and_swap<uint32_t>(&p[0]);
}
inline void XOverlappedSetResult(void* ptr, uint32_t value) {
  auto p = reinterpret_cast<uint32_t*>(ptr);
  xe::store_and_swap<uint32_t>(&p[0], value);
}
inline uint32_t XOverlappedGetLength(void* ptr) {
  auto p = reinterpret_cast<uint32_t*>(ptr);
  return xe::load_and_swap<uint32_t>(&p[1]);
}
inline void XOverlappedSetLength(void* ptr, uint32_t value) {
  auto p = reinterpret_cast<uint32_t*>(ptr);
  xe::store_and_swap<uint32_t>(&p[1], value);
}
inline uint32_t XOverlappedGetContext(void* ptr) {
  auto p = reinterpret_cast<uint32_t*>(ptr);
  return xe::load_and_swap<uint32_t>(&p[2]);
}
inline void XOverlappedSetContext(void* ptr, uint32_t value) {
  auto p = reinterpret_cast<uint32_t*>(ptr);
  xe::store_and_swap<uint32_t>(&p[2], value);
}
inline X_HANDLE XOverlappedGetEvent(void* ptr) {
  auto p = reinterpret_cast<uint32_t*>(ptr);
  return xe::load_and_swap<uint32_t>(&p[3]);
}
inline uint32_t XOverlappedGetCompletionRoutine(void* ptr) {
  auto p = reinterpret_cast<uint32_t*>(ptr);
  return xe::load_and_swap<uint32_t>(&p[4]);
}
inline uint32_t XOverlappedGetCompletionContext(void* ptr) {
  auto p = reinterpret_cast<uint32_t*>(ptr);
  return xe::load_and_swap<uint32_t>(&p[5]);
}
inline void XOverlappedSetExtendedError(void* ptr, uint32_t value) {
  auto p = reinterpret_cast<uint32_t*>(ptr);
  xe::store_and_swap<uint32_t>(&p[6], value);
}

struct X_ANSI_STRING {
  xe::be<uint16_t> length;
  xe::be<uint16_t> maximum_length;
  xe::be<uint32_t> pointer;

  static X_ANSI_STRING* Translate(uint8_t* membase, uint32_t guest_address) {
    if (!guest_address) {
      return nullptr;
    }
    return reinterpret_cast<X_ANSI_STRING*>(membase + guest_address);
  }

  static X_ANSI_STRING* TranslateIndirect(uint8_t* membase,
                                          uint32_t guest_address_ptr) {
    if (!guest_address_ptr) {
      return nullptr;
    }
    uint32_t guest_address =
        xe::load_and_swap<uint32_t>(membase + guest_address_ptr);
    return Translate(membase, guest_address);
  }

  static std::string to_string(uint8_t* membase, uint32_t guest_address) {
    auto str = Translate(membase, guest_address);
    return str ? str->to_string(membase) : "";
  }

  static std::string to_string_indirect(uint8_t* membase,
                                        uint32_t guest_address_ptr) {
    auto str = TranslateIndirect(membase, guest_address_ptr);
    return str ? str->to_string(membase) : "";
  }

  void reset() {
    length = 0;
    maximum_length = 0;
    pointer = 0;
  }

  std::string to_string(uint8_t* membase) const {
    if (!length) {
      return "";
    }
    return std::string(reinterpret_cast<const char*>(membase + pointer),
                       length);
  }
};
static_assert_size(X_ANSI_STRING, 8);

struct X_UNICODE_STRING {
  xe::be<uint16_t> length;          // 0x0
  xe::be<uint16_t> maximum_length;  // 0x2
  xe::be<uint32_t> pointer;         // 0x4

  void reset() {
    length = 0;
    maximum_length = 0;
    pointer = 0;
  }

  std::wstring to_string(uint8_t* membase) const {
    if (!length) {
      return L"";
    }

    return std::wstring(reinterpret_cast<const wchar_t*>(membase + pointer),
                        length);
  }
};
static_assert_size(X_UNICODE_STRING, 8);

// https://pastebin.com/SMypYikG
typedef uint32_t XNotificationID;

// https://github.com/CodeAsm/ffplay360/blob/master/Common/XTLOnPC.h
struct X_VIDEO_MODE {
  be<uint32_t> display_width;
  be<uint32_t> display_height;
  be<uint32_t> is_interlaced;
  be<uint32_t> is_widescreen;
  be<uint32_t> is_hi_def;
  be<float> refresh_rate;
  be<uint32_t> video_standard;
  be<uint32_t> unknown_0x8a;
  be<uint32_t> unknown_0x01;
  be<uint32_t> reserved[3];
};
static_assert_size(X_VIDEO_MODE, 48);

struct X_LIST_ENTRY {
  be<uint32_t> flink_ptr;  // next entry / head
  be<uint32_t> blink_ptr;  // previous entry / head

  // Assumes X_LIST_ENTRY is within guest memory!
  void initialize(uint8_t* virtual_membase) {
    flink_ptr = static_cast<uint32_t>(reinterpret_cast<uint8_t*>(this) -
                                      virtual_membase);
    blink_ptr = static_cast<uint32_t>(reinterpret_cast<uint8_t*>(this) -
                                      virtual_membase);
  }
};
static_assert_size(X_LIST_ENTRY, 8);

struct X_SINGLE_LIST_ENTRY {
  be<uint32_t> next;  // 0x0 pointer to next entry
};
static_assert_size(X_SINGLE_LIST_ENTRY, 4);

// https://www.nirsoft.net/kernel_struct/vista/SLIST_HEADER.html
struct X_SLIST_HEADER {
  X_SINGLE_LIST_ENTRY next;  // 0x0
  be<uint16_t> depth;        // 0x4
  be<uint16_t> sequence;     // 0x6
};
static_assert_size(X_SLIST_HEADER, 8);

// https://msdn.microsoft.com/en-us/library/windows/hardware/ff550671(v=vs.85).aspx
struct X_IO_STATUS_BLOCK {
  union {
    xe::be<X_STATUS> status;
    xe::be<uint32_t> pointer;
  };
  xe::be<uint32_t> information;
};

struct X_EX_TITLE_TERMINATE_REGISTRATION {
  xe::be<uint32_t> notification_routine;  // 0x0
  xe::be<uint32_t> priority;              // 0x4
  X_LIST_ENTRY list_entry;                // 0x8 ??
};
static_assert_size(X_EX_TITLE_TERMINATE_REGISTRATION, 16);

struct X_OBJECT_ATTRIBUTES {
  xe::be<uint32_t> root_directory;  // 0x0
  xe::be<uint32_t> name_ptr;        // 0x4 PANSI_STRING
  xe::be<uint32_t> attributes;      // 0xC
};

// https://msdn.microsoft.com/en-us/library/windows/desktop/aa363082.aspx
typedef struct {
  xe::be<uint32_t> exception_code;
  xe::be<uint32_t> exception_flags;
  xe::be<uint32_t> exception_record;
  xe::be<uint32_t> exception_address;
  xe::be<uint32_t> number_parameters;
  xe::be<uint32_t> exception_information[15];
} X_EXCEPTION_RECORD;
static_assert_size(X_EXCEPTION_RECORD, 0x50);

#pragma pack(pop)

}  // namespace xe

// clang-format on

#endif  // XENIA_XBOX_H_
