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

namespace xe {

#pragma pack(push, 4)

typedef uint32_t X_HANDLE;
#define X_INVALID_HANDLE_VALUE ((X_HANDLE)-1)

// TODO(benvanik): type all of this so we get some safety.

// NT_STATUS (STATUS_*)
// http://msdn.microsoft.com/en-us/library/cc704588.aspx
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
#define X_STATUS_MEMORY_NOT_ALLOCATED                   ((X_STATUS)0xC00000A0L)
#define X_STATUS_INVALID_PARAMETER_1                    ((X_STATUS)0xC00000EFL)
#define X_STATUS_INVALID_PARAMETER_2                    ((X_STATUS)0xC00000F0L)
#define X_STATUS_INVALID_PARAMETER_3                    ((X_STATUS)0xC00000F1L)
#define X_STATUS_DRIVER_ORDINAL_NOT_FOUND               ((X_STATUS)0xC0000262L)
#define X_STATUS_DRIVER_ENTRYPOINT_NOT_FOUND            ((X_STATUS)0xC0000263L)

// HRESULT (ERROR_*)
// Adding as needed.
// For some reason, half of these aren't *actually* HRESULTs.
// Windows is a weird place.
typedef uint32_t X_RESULT;
#define X_FACILITY_WIN32 7
#define X_RESULT_FROM_WIN32(x) x //((X_RESULT)(x) <= 0 ? ((X_RESULT)(x)) : ((X_RESULT) (((x) & 0x0000FFFF) | (X_FACILITY_WIN32 << 16) | 0x80000000)))
#define X_ERROR_SUCCESS                                 X_RESULT_FROM_WIN32(0x00000000L)
#define X_ERROR_FILE_NOT_FOUND                          X_RESULT_FROM_WIN32(0x00000002L)
#define X_ERROR_PATH_NOT_FOUND                          X_RESULT_FROM_WIN32(0x00000003L)
#define X_ERROR_ACCESS_DENIED                           X_RESULT_FROM_WIN32(0x00000005L)
#define X_ERROR_INVALID_HANDLE                          X_RESULT_FROM_WIN32(0x00000006L)
#define X_ERROR_NO_MORE_FILES                           X_RESULT_FROM_WIN32(0x00000012L)
#define X_ERROR_INVALID_PARAMETER                       X_RESULT_FROM_WIN32(0x00000057L)
#define X_ERROR_IO_PENDING                              X_RESULT_FROM_WIN32(0x000003E5L)
#define X_ERROR_INSUFFICIENT_BUFFER                     X_RESULT_FROM_WIN32(0x0000007AL)
#define X_ERROR_INVALID_NAME                            X_RESULT_FROM_WIN32(0x0000007BL)
#define X_ERROR_BAD_ARGUMENTS                           X_RESULT_FROM_WIN32(0x000000A0L)
#define X_ERROR_BUSY                                    X_RESULT_FROM_WIN32(0x000000AAL)
#define X_ERROR_ALREADY_EXISTS                          X_RESULT_FROM_WIN32(0x000000B7L)
#define X_ERROR_DEVICE_NOT_CONNECTED                    X_RESULT_FROM_WIN32(0x0000048FL)
#define X_ERROR_NOT_FOUND                               X_RESULT_FROM_WIN32(0x00000490L)
#define X_ERROR_CANCELLED                               X_RESULT_FROM_WIN32(0x000004C7L)
#define X_ERROR_NOT_LOGGED_ON                           X_RESULT_FROM_WIN32(0x000004DDL)
#define X_ERROR_NO_SUCH_USER                            X_RESULT_FROM_WIN32(0x00000525L)
#define X_ERROR_FUNCTION_FAILED                         X_RESULT_FROM_WIN32(0x0000065BL)
#define X_ERROR_EMPTY                                   X_RESULT_FROM_WIN32(0x000010D2L)

typedef uint32_t X_HRESULT;
#define X_E_SUCCESS                                     static_cast<X_HRESULT>(0)
#define X_E_FALSE                                       static_cast<X_HRESULT>(0x80000000L)
#define X_E_INVALIDARG                                  static_cast<X_HRESULT>(0x80070057L)

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
#define X_MEM_16MB_PAGES          0x80000000 // from Valve SDK

// FILE_*, used by NtOpenFile
#define X_FILE_SUPERSEDED         0x00000000
#define X_FILE_OPENED             0x00000001
#define X_FILE_CREATED            0x00000002
#define X_FILE_OVERWRITTEN        0x00000003
#define X_FILE_EXISTS             0x00000004
#define X_FILE_DOES_NOT_EXIST     0x00000005


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


// (?), used by KeGetCurrentProcessType
#define X_PROCTYPE_IDLE   0
#define X_PROCTYPE_USER   1
#define X_PROCTYPE_SYSTEM 2


// Sockets/networking.
#define X_INVALID_SOCKET          (uint32_t)(~0)
#define X_SOCKET_ERROR            (uint32_t)(-1)


// Thread enums.
#define X_CREATE_SUSPENDED        0x00000004


// TLS specials.
#define X_TLS_OUT_OF_INDEXES      UINT32_MAX  // (-1)


// Languages.
#define X_LANGUAGE_ENGLISH        1
#define X_LANGUAGE_JAPANESE       2

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

// http://code.google.com/p/vdash/source/browse/trunk/vdash/include/kernel.h
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

class X_ANSI_STRING {
 private:
  uint16_t length;
  uint16_t maximum_length;
  const char* buffer;

 public:
  X_ANSI_STRING() { Zero(); }
  X_ANSI_STRING(const uint8_t* base, uint32_t p) { Read(base, p); }
  void Read(const uint8_t* base, uint32_t p) {
    length = xe::load_and_swap<uint16_t>(base + p);
    maximum_length = xe::load_and_swap<uint16_t>(base + p + 2);
    if (maximum_length) {
      buffer = (const char*)(base + xe::load_and_swap<uint32_t>(base + p + 4));
    } else {
      buffer = 0;
    }
  }
  void Zero() {
    length = maximum_length = 0;
    buffer = 0;
  }
  char* Duplicate() {
    if (!buffer || !length) {
      return nullptr;
    }
    auto copy = (char*)calloc(length + 1, sizeof(char));
    std::strncpy(copy, buffer, length);
    return copy;
  }
  std::string to_string() {
    if (!buffer || !length) {
      return "";
    }
    std::string result(buffer, length);
    return result;
  }
};
// static_assert_size(X_ANSI_STRING, 8);

// Values seem to be all over the place - GUIDs?
typedef uint32_t XNotificationID;

// http://ffplay360.googlecode.com/svn/trunk/Common/XTLOnPC.h
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

enum X_INPUT_FLAG {
  X_INPUT_FLAG_GAMEPAD = 0x00000001,
};

enum X_INPUT_GAMEPAD_BUTTON {
  X_INPUT_GAMEPAD_DPAD_UP = 0x0001,
  X_INPUT_GAMEPAD_DPAD_DOWN = 0x0002,
  X_INPUT_GAMEPAD_DPAD_LEFT = 0x0004,
  X_INPUT_GAMEPAD_DPAD_RIGHT = 0x0008,
  X_INPUT_GAMEPAD_START = 0x0010,
  X_INPUT_GAMEPAD_BACK = 0x0020,
  X_INPUT_GAMEPAD_LEFT_THUMB = 0x0040,
  X_INPUT_GAMEPAD_RIGHT_THUMB = 0x0080,
  X_INPUT_GAMEPAD_LEFT_SHOULDER = 0x0100,
  X_INPUT_GAMEPAD_RIGHT_SHOULDER = 0x0200,
  X_INPUT_GAMEPAD_A = 0x1000,
  X_INPUT_GAMEPAD_B = 0x2000,
  X_INPUT_GAMEPAD_X = 0x4000,
  X_INPUT_GAMEPAD_Y = 0x8000,
};

struct X_INPUT_GAMEPAD {
  be<uint16_t> buttons;
  be<uint8_t> left_trigger;
  be<uint8_t> right_trigger;
  be<int16_t> thumb_lx;
  be<int16_t> thumb_ly;
  be<int16_t> thumb_rx;
  be<int16_t> thumb_ry;
};
static_assert_size(X_INPUT_GAMEPAD, 12);

struct X_INPUT_STATE {
  be<uint32_t> packet_number;
  X_INPUT_GAMEPAD gamepad;
};
static_assert_size(X_INPUT_STATE, sizeof(X_INPUT_GAMEPAD) + 4);

struct X_INPUT_VIBRATION {
  be<uint16_t> left_motor_speed;
  be<uint16_t> right_motor_speed;
};
static_assert_size(X_INPUT_VIBRATION, 4);

struct X_INPUT_CAPABILITIES {
  be<uint8_t> type;
  be<uint8_t> sub_type;
  be<uint16_t> flags;
  X_INPUT_GAMEPAD gamepad;
  X_INPUT_VIBRATION vibration;
};
static_assert_size(X_INPUT_CAPABILITIES,
                   sizeof(X_INPUT_GAMEPAD) + sizeof(X_INPUT_VIBRATION) + 4);

// http://msdn.microsoft.com/en-us/library/windows/desktop/microsoft.directx_sdk.reference.xinput_keystroke(v=vs.85).aspx
struct X_INPUT_KEYSTROKE {
  be<uint16_t> virtual_key;
  be<uint16_t> unicode;
  be<uint16_t> flags;
  be<uint8_t> user_index;
  be<uint8_t> hid_code;
};
static_assert_size(X_INPUT_KEYSTROKE, 8);

#pragma pack(pop)

}  // namespace xe

#endif  // XENIA_XBOX_H_
