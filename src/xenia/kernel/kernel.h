/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_KERNEL_H_
#define XENIA_KERNEL_KERNEL_H_

#include "xenia/base/byte_order.h"
#include "xenia/base/memory.h"
#include "xenia/xbox.h"

namespace xe {
namespace kernel {

// IOCTL_, used by NtDeviceIoControlFile
constexpr uint32_t X_IOCTL_DISK_GET_DRIVE_GEOMETRY = 0x70000;
constexpr uint32_t X_IOCTL_DISK_GET_PARTITION_INFO = 0x74004;

// MEM_*, used by NtAllocateVirtualMemory
enum X_MEM : uint32_t {
  X_MEM_COMMIT = 0x00001000,
  X_MEM_RESERVE = 0x00002000,
  X_MEM_DECOMMIT = 0x00004000,
  X_MEM_RELEASE = 0x00008000,
  X_MEM_FREE = 0x00010000,
  X_MEM_PRIVATE = 0x00020000,
  X_MEM_RESET = 0x00080000,
  X_MEM_TOP_DOWN = 0x00100000,
  X_MEM_NOZERO = 0x00800000,
  X_MEM_LARGE_PAGES = 0x20000000,
  X_MEM_HEAP = 0x40000000,
  X_MEM_16MB_PAGES = 0x80000000  // from Valve SDK
};

// PAGE_*, used by NtAllocateVirtualMemory
enum X_PAGE : uint32_t {
  X_PAGE_NOACCESS = 0x00000001,
  X_PAGE_READONLY = 0x00000002,
  X_PAGE_READWRITE = 0x00000004,
  X_PAGE_WRITECOPY = 0x00000008,
  X_PAGE_EXECUTE = 0x00000010,
  X_PAGE_EXECUTE_READ = 0x00000020,
  X_PAGE_EXECUTE_READWRITE = 0x00000040,
  X_PAGE_EXECUTE_WRITECOPY = 0x00000080,
  X_PAGE_GUARD = 0x00000100,
  X_PAGE_NOCACHE = 0x00000200,
  X_PAGE_WRITECOMBINE = 0x00000400
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
static_assert_size(XAM_OVERLAPPED, 0x1C);

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

  void reset() {
    length = 0;
    maximum_length = 0;
    pointer = 0;
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
};
static_assert_size(X_UNICODE_STRING, 8);

// https://github.com/CodeAsm/ffplay360/blob/master/Common/XTLOnPC.h
struct X_VIDEO_MODE {
  be<uint32_t> display_width;
  be<uint32_t> display_height;
  be<uint32_t> is_interlaced;
  be<uint32_t> is_widescreen;
  be<uint32_t> is_hi_def;
  be<float> refresh_rate;
  be<uint32_t> video_standard;
  be<uint32_t> pixel_rate;
  be<uint32_t> widescreen_flag;
  be<uint32_t> reserved[3];
};
static_assert_size(X_VIDEO_MODE, 48);

// https://docs.microsoft.com/en-us/windows/win32/api/ntdef/ns-ntdef-list_entry
struct X_LIST_ENTRY {
  be<uint32_t> flink_ptr;  // next entry / head
  be<uint32_t> blink_ptr;  // previous entry / head
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

struct X_EX_TITLE_TERMINATE_REGISTRATION {
  xe::be<uint32_t> notification_routine;  // 0x0
  xe::be<uint32_t> priority;              // 0x4
  X_LIST_ENTRY list_entry;                // 0x8
};
static_assert_size(X_EX_TITLE_TERMINATE_REGISTRATION, 16);

enum X_OBJECT_HEADER_FLAGS : uint16_t {
  OBJECT_HEADER_FLAG_NAMED_OBJECT =
      1,  // if set, has X_OBJECT_HEADER_NAME_INFO prior to X_OBJECT_HEADER
  OBJECT_HEADER_FLAG_IS_PERMANENT = 2,
  OBJECT_HEADER_FLAG_CONTAINED_IN_DIRECTORY =
      4,  // this object resides in an X_OBJECT_DIRECTORY
  OBJECT_HEADER_IS_TITLE_OBJECT = 0x10,  // used in obcreateobject

};

struct X_OBJECT_DIRECTORY {
  // each is a pointer to X_OBJECT_HEADER_NAME_INFO
  // i believe offset 0 = pointer to next in bucket
  xe::be<uint32_t> name_buckets[13];
};
static_assert_size(X_OBJECT_DIRECTORY, 0x34);

// https://www.geoffchappell.com/studies/windows/km/ntoskrnl/inc/ntos/ob/object_header_name_info.htm
// quite different, though
struct X_OBJECT_HEADER_NAME_INFO {
  // i think that this is the next link in an X_OBJECT_DIRECTORY's buckets
  xe::be<uint32_t> next_in_directory;
  xe::be<uint32_t> object_directory;  // pointer to X_OBJECT_DIRECTORY
  X_ANSI_STRING name;
};

struct X_OBJECT_ATTRIBUTES {
  xe::be<uint32_t> root_directory;  // 0x0
  xe::be<uint32_t> name_ptr;        // 0x4 PANSI_STRING
  xe::be<uint32_t> attributes;      // 0xC
};

struct X_OBJECT_TYPE {
  xe::be<uint32_t> allocate_proc;  // 0x0
  xe::be<uint32_t> free_proc;      // 0x4
  xe::be<uint32_t> close_proc;     // 0x8
  xe::be<uint32_t> delete_proc;    // 0xC
  xe::be<uint32_t> unknown_proc;   // 0x10
  xe::be<uint32_t>
      unknown_size_or_object_;  // this seems to be a union, it can be a pointer
                                // or it can be the size of the object
  xe::be<uint32_t> pool_tag;    // 0x18
};
static_assert_size(X_OBJECT_TYPE, 0x1C);

struct X_KSYMLINK {
  xe::be<uint32_t> refed_object_maybe;
  X_ANSI_STRING refed_object_name_maybe;
};
static_assert_size(X_KSYMLINK, 0xC);
// https://msdn.microsoft.com/en-us/library/windows/desktop/aa363082.aspx
typedef struct {
  // Renamed due to a collision with exception_code from Windows excpt.h.
  xe::be<uint32_t> code;
  xe::be<uint32_t> exception_flags;
  xe::be<uint32_t> exception_record;
  xe::be<uint32_t> exception_address;
  xe::be<uint32_t> number_parameters;
  xe::be<uint32_t> exception_information[15];
} X_EXCEPTION_RECORD;
static_assert_size(X_EXCEPTION_RECORD, 0x50);

struct X_KSPINLOCK {
  xe::be<uint32_t> prcb_of_owner;
};
static_assert_size(X_KSPINLOCK, 4);

struct MESSAGEBOX_RESULT {
  union {
    xe::be<uint32_t> ButtonPressed;
    xe::be<uint16_t> Passcode[4];
  };
};
static_assert_size(MESSAGEBOX_RESULT, 0x8);

inline bool IsOfflineXUID(uint64_t xuid) { return ((xuid >> 60) & 0xF) == 0xE; }

inline bool IsOnlineXUID(uint64_t xuid) {
  return ((xuid >> 48) & 0xFFFF) == 0x9;
}

inline bool IsGuestXUID(uint64_t xuid) {
  const uint32_t HighPart = xuid >> 48;
  return ((HighPart & 0x000F) == 0x9) && ((HighPart & 0x00C0) > 0);
}

inline bool IsTeamXUID(uint64_t xuid) {
  return (xuid & 0xFF00000000000140) == 0xFE00000000000100;
}

inline bool IsValidXUID(uint64_t xuid) {
  const bool valid = IsOfflineXUID(xuid) || IsOnlineXUID(xuid) ||
                     IsTeamXUID(xuid) || IsGuestXUID(xuid);

  return valid;
}

}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_KERNEL_H_
