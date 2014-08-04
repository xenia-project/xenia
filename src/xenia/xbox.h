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

#include <xenia/common.h>
#include <xenia/core.h>


namespace xe {


typedef uint32_t X_HANDLE;
#define X_INVALID_HANDLE_VALUE  ((X_HANDLE)-1)


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
#define X_STATUS_INVALID_PAGE_PROTECTION                ((X_STATUS)0xC0000045L)
#define X_STATUS_MUTANT_NOT_OWNED                       ((X_STATUS)0xC0000046L)
#define X_STATUS_MEMORY_NOT_ALLOCATED                   ((X_STATUS)0xC00000A0L)
#define X_STATUS_INVALID_PARAMETER_1                    ((X_STATUS)0xC00000EFL)
#define X_STATUS_INVALID_PARAMETER_2                    ((X_STATUS)0xC00000F0L)
#define X_STATUS_INVALID_PARAMETER_3                    ((X_STATUS)0xC00000F1L)

// HRESULT (ERROR_*)
// Adding as needed.
typedef uint32_t X_RESULT;
#define X_FACILITY_WIN32 7
#define X_HRESULT_FROM_WIN32(x) x //((X_RESULT)(x) <= 0 ? ((X_RESULT)(x)) : ((X_RESULT) (((x) & 0x0000FFFF) | (X_FACILITY_WIN32 << 16) | 0x80000000)))
#define X_ERROR_SUCCESS                                 X_HRESULT_FROM_WIN32(0x00000000L)
#define X_ERROR_ACCESS_DENIED                           X_HRESULT_FROM_WIN32(0x00000005L)
#define X_ERROR_INVALID_HANDLE                          X_HRESULT_FROM_WIN32(0x00000006L)
#define X_ERROR_NO_MORE_FILES                           X_HRESULT_FROM_WIN32(0x00000018L)
#define X_ERROR_IO_PENDING                              X_HRESULT_FROM_WIN32(0x000003E5L)
#define X_ERROR_INSUFFICIENT_BUFFER                     X_HRESULT_FROM_WIN32(0x0000007AL)
#define X_ERROR_BAD_ARGUMENTS                           X_HRESULT_FROM_WIN32(0x000000A0L)
#define X_ERROR_BUSY                                    X_HRESULT_FROM_WIN32(0x000000AAL)
#define X_ERROR_DEVICE_NOT_CONNECTED                    X_HRESULT_FROM_WIN32(0x0000048FL)
#define X_ERROR_NOT_FOUND                               X_HRESULT_FROM_WIN32(0x00000490L)
#define X_ERROR_CANCELLED                               X_HRESULT_FROM_WIN32(0x000004C7L)
#define X_ERROR_NO_SUCH_USER                            X_HRESULT_FROM_WIN32(0x00000525L)
#define X_ERROR_EMPTY                                   X_HRESULT_FROM_WIN32(0x000010D2L)

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


typedef enum _X_FILE_ATTRIBUTES {
  X_FILE_ATTRIBUTE_NONE         = 0x0000,
  X_FILE_ATTRIBUTE_READONLY     = 0x0001,
  X_FILE_ATTRIBUTE_HIDDEN       = 0x0002,
  X_FILE_ATTRIBUTE_SYSTEM       = 0x0004,
  X_FILE_ATTRIBUTE_DIRECTORY    = 0x0010,
  X_FILE_ATTRIBUTE_ARCHIVE      = 0x0020,
  X_FILE_ATTRIBUTE_DEVICE       = 0x0040,
  X_FILE_ATTRIBUTE_NORMAL       = 0x0080,
  X_FILE_ATTRIBUTE_TEMPORARY    = 0x0100,
  X_FILE_ATTRIBUTE_COMPRESSED   = 0x0800,
  X_FILE_ATTRIBUTE_ENCRYPTED    = 0x4000,
} X_FILE_ATTRIBUTES;


// http://code.google.com/p/vdash/source/browse/trunk/vdash/include/kernel.h
typedef enum _X_FILE_INFORMATION_CLASS {
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
} X_FILE_INFORMATION_CLASS;


inline void XOverlappedSetResult(void* ptr, uint32_t value) {
  auto p = reinterpret_cast<uint32_t*>(ptr);
  poly::store_and_swap<uint32_t>(&p[0], value);
}
inline void XOverlappedSetLength(void* ptr, uint32_t value) {
  auto p = reinterpret_cast<uint32_t*>(ptr);
  poly::store_and_swap<uint32_t>(&p[1], value);
}
inline uint32_t XOverlappedGetContext(void* ptr) {
  auto p = reinterpret_cast<uint32_t*>(ptr);
  return poly::load_and_swap<uint32_t>(&p[2]);
}
inline void XOverlappedSetContext(void* ptr, uint32_t value) {
  auto p = reinterpret_cast<uint32_t*>(ptr);
  poly::store_and_swap<uint32_t>(&p[2], value);
}
inline void XOverlappedSetExtendedError(void* ptr, uint32_t value) {
  auto p = reinterpret_cast<uint32_t*>(ptr);
  poly::store_and_swap<uint32_t>(&p[7], value);
}
inline X_HANDLE XOverlappedGetEvent(void* ptr) {
  auto p = reinterpret_cast<uint32_t*>(ptr);
  return poly::load_and_swap<uint32_t>(&p[4]);
}
inline uint32_t XOverlappedGetCompletionRoutine(void* ptr) {
  auto p = reinterpret_cast<uint32_t*>(ptr);
  return poly::load_and_swap<uint32_t>(&p[5]);
}
inline uint32_t XOverlappedGetCompletionContext(void* ptr) {
  auto p = reinterpret_cast<uint32_t*>(ptr);
  return poly::load_and_swap<uint32_t>(&p[6]);
}

class X_ANSI_STRING {
private:
  uint16_t    length;
  uint16_t    maximum_length;
  const char* buffer;

public:
  X_ANSI_STRING() {
    Zero();
  }
  X_ANSI_STRING(const uint8_t* base, uint32_t p) {
    Read(base, p);
  }
  void Read(const uint8_t* base, uint32_t p) {
    length = poly::load_and_swap<uint16_t>(base + p);
    maximum_length = poly::load_and_swap<uint16_t>(base + p + 2);
    if (maximum_length) {
      buffer = (const char*)(base + poly::load_and_swap<uint32_t>(base + p + 4));
    } else {
      buffer = 0;
    }
  }
  void Zero() {
    length = maximum_length = 0;
    buffer = 0;
  }
  char* Duplicate() {
    if (buffer == NULL || length == 0) {
      return NULL;
    }
    auto copy = (char*)xe_calloc(length+1);
    xestrncpya(copy, length+1, buffer, length);
    return copy;
  }
};


class X_OBJECT_ATTRIBUTES {
public:
  uint32_t      root_directory;
  uint32_t      object_name_ptr;
  X_ANSI_STRING object_name;
  uint32_t      attributes;

  X_OBJECT_ATTRIBUTES() {
    Zero();
  }
  X_OBJECT_ATTRIBUTES(const uint8_t* base, uint32_t p) {
    Read(base, p);
  }
  void Read(const uint8_t* base, uint32_t p) {
    root_directory  = poly::load_and_swap<uint32_t>(base + p);
    object_name_ptr = poly::load_and_swap<uint32_t>(base + p + 4);
    if (object_name_ptr) {
      object_name.Read(base, object_name_ptr);
    } else {
      object_name.Zero();
    }
    attributes      = poly::load_and_swap<uint32_t>(base + p + 8);
  }
  void Zero() {
    root_directory = 0;
    object_name_ptr = 0;
    object_name.Zero();
    attributes = 0;
  }
};


// Values seem to be all over the place - GUIDs?
typedef uint32_t XNotificationID;


typedef enum _X_INPUT_FLAG {
  X_INPUT_FLAG_GAMEPAD      = 0x00000001,
} X_INPUT_FLAG;

typedef enum _X_INPUT_GAMEPAD_BUTTON {
  X_INPUT_GAMEPAD_DPAD_UP         = 0x0001,
  X_INPUT_GAMEPAD_DPAD_DOWN       = 0x0002,
  X_INPUT_GAMEPAD_DPAD_LEFT       = 0x0004,
  X_INPUT_GAMEPAD_DPAD_RIGHT      = 0x0008,
  X_INPUT_GAMEPAD_START           = 0x0010,
  X_INPUT_GAMEPAD_BACK            = 0x0020,
  X_INPUT_GAMEPAD_LEFT_THUMB      = 0x0040,
  X_INPUT_GAMEPAD_RIGHT_THUMB     = 0x0080,
  X_INPUT_GAMEPAD_LEFT_SHOULDER   = 0x0100,
  X_INPUT_GAMEPAD_RIGHT_SHOULDER  = 0x0200,
  X_INPUT_GAMEPAD_A               = 0x1000,
  X_INPUT_GAMEPAD_B               = 0x2000,
  X_INPUT_GAMEPAD_X               = 0x4000,
  X_INPUT_GAMEPAD_Y               = 0x8000,
} X_INPUT_GAMEPAD_BUTTON;

class X_INPUT_GAMEPAD {
public:
  uint16_t          buttons;
  uint8_t           left_trigger;
  uint8_t           right_trigger;
  int16_t           thumb_lx;
  int16_t           thumb_ly;
  int16_t           thumb_rx;
  int16_t           thumb_ry;

  X_INPUT_GAMEPAD() {
    Zero();
  }
  X_INPUT_GAMEPAD(const uint8_t* base, uint32_t p) {
    Read(base, p);
  }
  void Read(const uint8_t* base, uint32_t p) {
    buttons = poly::load_and_swap<uint16_t>(base + p);
    left_trigger = poly::load_and_swap<uint8_t>(base + p + 2);
    right_trigger = poly::load_and_swap<uint8_t>(base + p + 3);
    thumb_lx = poly::load_and_swap<int16_t>(base + p + 4);
    thumb_ly = poly::load_and_swap<int16_t>(base + p + 6);
    thumb_rx = poly::load_and_swap<int16_t>(base + p + 8);
    thumb_ry = poly::load_and_swap<int16_t>(base + p + 10);
  }
  void Write(uint8_t* base, uint32_t p) {
    poly::store_and_swap<uint16_t>(base + p, buttons);
    poly::store_and_swap<uint8_t>(base + p + 2, left_trigger);
    poly::store_and_swap<uint8_t>(base + p + 3, right_trigger);
    poly::store_and_swap<int16_t>(base + p + 4, thumb_lx);
    poly::store_and_swap<int16_t>(base + p + 6, thumb_ly);
    poly::store_and_swap<int16_t>(base + p + 8, thumb_rx);
    poly::store_and_swap<int16_t>(base + p + 10, thumb_ry);
  }
  void Zero() {
    buttons = 0;
    left_trigger = right_trigger = 0;
    thumb_lx = thumb_ly = thumb_rx = thumb_ry = 0;
  }
};
class X_INPUT_STATE {
public:
  uint32_t          packet_number;
  X_INPUT_GAMEPAD   gamepad;

  X_INPUT_STATE() {
    Zero();
  }
  X_INPUT_STATE(const uint8_t* base, uint32_t p) {
    Read(base, p);
  }
  void Read(const uint8_t* base, uint32_t p) {
    packet_number = poly::load_and_swap<uint32_t>(base + p);
    gamepad.Read(base, p + 4);
  }
  void Write(uint8_t* base, uint32_t p) {
    poly::store_and_swap<uint32_t>(base + p, packet_number);
    gamepad.Write(base, p + 4);
  }
  void Zero() {
    packet_number = 0;
    gamepad.Zero();
  }
};
class X_INPUT_VIBRATION {
public:
  uint16_t          left_motor_speed;
  uint16_t          right_motor_speed;

  X_INPUT_VIBRATION() {
    Zero();
  }
  X_INPUT_VIBRATION(const uint8_t* base, uint32_t p) {
    Read(base, p);
  }
  void Read(const uint8_t* base, uint32_t p) {
    left_motor_speed  = poly::load_and_swap<uint16_t>(base + p);
    right_motor_speed = poly::load_and_swap<uint16_t>(base + p + 2);
  }
  void Write(uint8_t* base, uint32_t p) {
    poly::store_and_swap<uint16_t>(base + p, left_motor_speed);
    poly::store_and_swap<uint16_t>(base + p + 2, right_motor_speed);
  }
  void Zero() {
    left_motor_speed = right_motor_speed = 0;
  }
};
class X_INPUT_CAPABILITIES {
public:
  uint8_t           type;
  uint8_t           sub_type;
  uint16_t          flags;
  X_INPUT_GAMEPAD   gamepad;
  X_INPUT_VIBRATION vibration;

  X_INPUT_CAPABILITIES() {
    Zero();
  }
  X_INPUT_CAPABILITIES(const uint8_t* base, uint32_t p) {
    Read(base, p);
  }
  void Read(const uint8_t* base, uint32_t p) {
    type      = poly::load_and_swap<uint8_t>(base + p);
    sub_type  = poly::load_and_swap<uint8_t>(base + p + 1);
    flags     = poly::load_and_swap<uint16_t>(base + p + 2);
    gamepad.Read(base, p + 4);
    vibration.Read(base, p + 4 + 12);
  }
  void Write(uint8_t* base, uint32_t p) {
    poly::store_and_swap<uint8_t>(base + p, type);
    poly::store_and_swap<uint8_t>(base + p + 1, sub_type);
    poly::store_and_swap<uint16_t>(base + p + 2, flags);
    gamepad.Write(base, p + 4);
    vibration.Write(base, p + 4 + 12);
  }
  void Zero() {
    type = 0;
    sub_type = 0;
    flags = 0;
    gamepad.Zero();
    vibration.Zero();
  }
};
// http://msdn.microsoft.com/en-us/library/windows/desktop/microsoft.directx_sdk.reference.xinput_keystroke(v=vs.85).aspx
class X_INPUT_KEYSTROKE {
public:
  uint16_t  virtual_key;
  uint16_t  unicode;
  uint16_t  flags;
  uint8_t   user_index;
  uint8_t   hid_code;

  X_INPUT_KEYSTROKE() {
    Zero();
  }
  X_INPUT_KEYSTROKE(const uint8_t* base, uint32_t p) {
    Read(base, p);
  }
  void Read(const uint8_t* base, uint32_t p) {
    virtual_key = poly::load_and_swap<uint16_t>(base + p + 0);
    unicode     = poly::load_and_swap<uint16_t>(base + p + 2);
    flags       = poly::load_and_swap<uint16_t>(base + p + 4);
    user_index  = poly::load_and_swap<uint8_t>(base + p + 6);
    hid_code    = poly::load_and_swap<uint8_t>(base + p + 7);
  }
  void Write(uint8_t* base, uint32_t p) {
    poly::store_and_swap<uint16_t>(base + p + 0, virtual_key);
    poly::store_and_swap<uint16_t>(base + p + 2, unicode);
    poly::store_and_swap<uint16_t>(base + p + 4, flags);
    poly::store_and_swap<uint8_t>(base + p + 6, user_index);
    poly::store_and_swap<uint8_t>(base + p + 7, hid_code);
  }
  void Zero() {
    virtual_key = 0;
    unicode = 0;
    flags = 0;
    user_index = 0;
    hid_code = 0;
  }
};


}  // namespace xe


#endif  // XENIA_XBOX_H_
