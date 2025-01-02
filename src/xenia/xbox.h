/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_XBOX_H_
#define XENIA_XBOX_H_

#include <map>
#include <string>

#include "xenia/base/memory.h"
#include "xenia/base/string.h"

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
#define X_STATUS_OBJECT_NAME_INVALID                    ((X_STATUS)0xC0000033L)
#define X_STATUS_OBJECT_NAME_NOT_FOUND                  ((X_STATUS)0xC0000034L)
#define X_STATUS_OBJECT_NAME_COLLISION                  ((X_STATUS)0xC0000035L)
#define X_STATUS_INVALID_PAGE_PROTECTION                ((X_STATUS)0xC0000045L)
#define X_STATUS_MUTANT_NOT_OWNED                       ((X_STATUS)0xC0000046L)
#define X_STATUS_THREAD_IS_TERMINATING                  ((X_STATUS)0xC000004BL)
#define X_STATUS_PROCEDURE_NOT_FOUND                    ((X_STATUS)0xC000007AL)
#define X_STATUS_INVALID_IMAGE_FORMAT                   ((X_STATUS)0xC000007BL)
#define X_STATUS_INSUFFICIENT_RESOURCES                 ((X_STATUS)0xC000009AL)
#define X_STATUS_MEMORY_NOT_ALLOCATED                   ((X_STATUS)0xC00000A0L)
#define X_STATUS_FILE_IS_A_DIRECTORY                    ((X_STATUS)0xC00000BAL)
#define X_STATUS_NOT_SUPPORTED                          ((X_STATUS)0xC00000BBL)
#define X_STATUS_INVALID_PARAMETER_1                    ((X_STATUS)0xC00000EFL)
#define X_STATUS_INVALID_PARAMETER_2                    ((X_STATUS)0xC00000F0L)
#define X_STATUS_INVALID_PARAMETER_3                    ((X_STATUS)0xC00000F1L)
#define X_STATUS_PROCESS_IS_TERMINATING                 ((X_STATUS)0xC000010AL)
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
#define X_ERROR_NOT_ENOUGH_MEMORY               X_RESULT_FROM_WIN32(0x00000008L)
#define X_ERROR_NO_MORE_FILES                   X_RESULT_FROM_WIN32(0x00000012L)
#define X_ERROR_NOT_SUPPORTED                   X_RESULT_FROM_WIN32(0x00000032L)
#define X_ERROR_INVALID_PARAMETER               X_RESULT_FROM_WIN32(0x00000057L)
#define X_ERROR_INSUFFICIENT_BUFFER             X_RESULT_FROM_WIN32(0x0000007AL)
#define X_ERROR_INVALID_NAME                    X_RESULT_FROM_WIN32(0x0000007BL)
#define X_ERROR_BAD_ARGUMENTS                   X_RESULT_FROM_WIN32(0x000000A0L)
#define X_ERROR_BUSY                            X_RESULT_FROM_WIN32(0x000000AAL)
#define X_ERROR_ALREADY_EXISTS                  X_RESULT_FROM_WIN32(0x000000B7L)
#define X_ERROR_IO_INCOMPLETE                   X_RESULT_FROM_WIN32(0x000003E4L)
#define X_ERROR_IO_PENDING                      X_RESULT_FROM_WIN32(0x000003E5L)
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
#define X_E_ACCESS_DENIED                       X_HRESULT_FROM_WIN32(X_ERROR_ACCESS_DENIED)
#define X_E_NOT_IMPLEMENTED                     static_cast<X_HRESULT>(0x80004001L)
#define X_E_FAIL                                static_cast<X_HRESULT>(0x80004005L)
#define X_E_NO_MORE_FILES                       X_HRESULT_FROM_WIN32(X_ERROR_NO_MORE_FILES)
#define X_E_INVALIDARG                          X_HRESULT_FROM_WIN32(X_ERROR_INVALID_PARAMETER)
#define X_E_DEVICE_NOT_CONNECTED                X_HRESULT_FROM_WIN32(X_ERROR_DEVICE_NOT_CONNECTED)
#define X_E_NOTFOUND                            X_HRESULT_FROM_WIN32(X_ERROR_NOT_FOUND)
#define X_E_NO_SUCH_USER                        X_HRESULT_FROM_WIN32(X_ERROR_NO_SUCH_USER)

//IOCTL_, used by NtDeviceIoControlFile
constexpr uint32_t X_IOCTL_DISK_GET_DRIVE_GEOMETRY = 0x70000;
constexpr uint32_t X_IOCTL_DISK_GET_PARTITION_INFO = 0x74004;

// MEM_*, used by NtAllocateVirtualMemory
enum X_MEM : uint32_t {
  X_MEM_COMMIT      = 0x00001000,
  X_MEM_RESERVE     = 0x00002000,
  X_MEM_DECOMMIT    = 0x00004000,
  X_MEM_RELEASE     = 0x00008000,
  X_MEM_FREE        = 0x00010000,
  X_MEM_PRIVATE     = 0x00020000,
  X_MEM_RESET       = 0x00080000,
  X_MEM_TOP_DOWN    = 0x00100000,
  X_MEM_NOZERO      = 0x00800000,
  X_MEM_LARGE_PAGES = 0x20000000,
  X_MEM_HEAP        = 0x40000000,
  X_MEM_16MB_PAGES  = 0x80000000  // from Valve SDK
};

// PAGE_*, used by NtAllocateVirtualMemory
enum X_PAGE : uint32_t {
  X_PAGE_NOACCESS          = 0x00000001,
  X_PAGE_READONLY          = 0x00000002,
  X_PAGE_READWRITE         = 0x00000004,
  X_PAGE_WRITECOPY         = 0x00000008,
  X_PAGE_EXECUTE           = 0x00000010,
  X_PAGE_EXECUTE_READ      = 0x00000020,
  X_PAGE_EXECUTE_READWRITE = 0x00000040,
  X_PAGE_EXECUTE_WRITECOPY = 0x00000080,
  X_PAGE_GUARD             = 0x00000100,
  X_PAGE_NOCACHE           = 0x00000200,
  X_PAGE_WRITECOMBINE      = 0x00000400
};

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

constexpr uint8_t XUserMaxUserCount = 4;

constexpr uint8_t XUserIndexLatest = 0xFD;
constexpr uint8_t XUserIndexNone = 0xFE;
constexpr uint8_t XUserIndexAny = 0xFF;

// https://github.com/ThirteenAG/Ultimate-ASI-Loader/blob/master/source/xlive/xliveless.h
typedef uint32_t XNotificationID;
enum : XNotificationID {
  // Notification Areas
  kXNotifySystem = 0x00000001,
  kXNotifyLive = 0x00000002,
  kXNotifyFriends = 0x00000004,
  kXNotifyCustom = 0x00000008,
  kXNotifyDvdDrive = 0x00000010,  // ?
  kXNotifyXmp = 0x00000020,
  kXNotifyMsgr = 0x00000040,
  kXNotifyParty = 0x00000080,
  kXNotifyAll = 0x000000EF,

  // XNotification System (35 total)
  kXNotificationSystemUI = 0x00000009,
  kXNotificationSystemSignInChanged = 0x0000000A,
  kXNotificationSystemStorageDevicesChanged = 0x0000000B,
  kXNotificationSystemProfileSettingChanged = 0x0000000E,
  kXNotificationSystemMuteListChanged = 0x00000011,
  kXNotificationSystemInputDevicesChanged = 0x00000012,
  kXNotificationSystemInputDeviceConfigChanged = 0x00000013,
  kXNotificationSystemPlayerTimerNotice = 0x00000015,
  kXNotificationSystemPXLiveSystemUpdate = 0x00000016,
  kXNotificationSystemAvatarChanged = 0x00000017,
  kXNotificationSystemUnknown = 0x00000018,
  kXNotificationSystemNUIHardwareStatusChanged = 0x00000019,
  kXNotificationSystemNUIPause = 0x0000001A,
  kXNotificationSystemNUIUIApproach = 0x0000001B,
  kXNotificationSystemDeviceRemap = 0x0000001C,
  kXNotificationSystemNUIBindingChanged = 0x0000001D,
  kXNotificationSystemAudioLatencyChanged = 0x0000001E,
  kXNotificationSystemNUIChatBindingChanged = 0x0000001F,
  kXNotificationSystemInputActivityChanged = 0x00000020,

  // XNotification Live (20 total)
  kXNotificationLiveConnectionChanged = 0x02000001,
  kXNotificationLiveInviteAccepted = 0x02000002,
  kXNotificationLiveLinkStateChanged = 0x02000003,
  kXNotificationLiveContentInstalled = 0x02000007,
  kXNotificationLiveMembershipPurchased = 0x02000008,
  kXNotificationLiveVoicechatAway = 0x02000009,
  kXNotificationLivePresenceChanged = 0x0200000A,
  kXNotificationLiveUnknown = 0x02000012,

  // XNotification Friends (9 total)
  kXNotificationFriendsPresenceChanged = 0x04000001,
  kXNotificationFriendsFriendAdded = 0x04000002,
  kXNotificationFriendsFriendRemoved = 0x04000003,
  kXNotificationFriendsUnknown = 0x04000008,

  // XNotification Custom (5 total)
  kXNotificationCustomActionPressed = 0x06000003,
  kXNotificationCustomGamercard = 0x06000004,

  // XNotification Dvd ?
  kXNotificationDvdDriveUnknown = 0x80000003,
  kXNotificationDvdDriveUnknownDashContext = 0x8000000C,
  kXNotificationDvdDriveTrayStateChanged = 0x8000000D,

  // XNotification XMP (13 total)
  kXNotificationXmpStateChanged = 0x0A000001,
  kXNotificationXmpPlaybackBehaviorChanged = 0x0A000002,
  kXNotificationXmpPlaybackControllerChanged = 0x0A000003,
  kXNotificationXmpUnknown = 0x0A00000C,

  // XNotification Party (6 total)
  kXNotificationPartyMembersChanged = 0x0E000002,
  kXNotificationFriendUnknown = 0x0E000005,

  // XNotification Msgr
  kXNotificationMsgrUnknown = 0x0C00000E,
};

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
#pragma pack(pop)

// Found by dumping the kSectionStringTable sections of various games:
// and the language list at
// https://free60project.github.io/wiki/Profile_Account/
enum class XLanguage : uint32_t {
  kInvalid = 0,
  kEnglish = 1,
  kJapanese = 2,
  kGerman = 3,
  kFrench = 4,
  kSpanish = 5,
  kItalian = 6,
  kKorean = 7,
  kTChinese = 8,
  kPortuguese = 9,
  kSChinese = 10,
  kPolish = 11,
  kRussian = 12,
  // STFS headers can't support any more languages than these
  kMaxLanguages = 13
};

enum class XContentType : uint32_t {
  kSavedGame = 0x00000001,
  kMarketplaceContent = 0x00000002,
  kPublisher = 0x00000003,
  kXbox360Title = 0x00001000,
  kIptvPauseBuffer = 0x00002000,
  kXNACommunity = 0x00003000,
  kInstalledGame = 0x00004000,
  kXboxTitle = 0x00005000,
  kSocialTitle = 0x00006000,
  kGamesOnDemand = 0x00007000,
  kSUStoragePack = 0x00008000,
  kAvatarItem = 0x00009000,
  kProfile = 0x00010000,
  kGamerPicture = 0x00020000,
  kTheme = 0x00030000,
  kCacheFile = 0x00040000,
  kStorageDownload = 0x00050000,
  kXboxSavedGame = 0x00060000,
  kXboxDownload = 0x00070000,
  kGameDemo = 0x00080000,
  kVideo = 0x00090000,
  kGameTitle = 0x000A0000,
  kInstaller = 0x000B0000,
  kGameTrailer = 0x000C0000,
  kArcadeTitle = 0x000D0000,
  kXNA = 0x000E0000,
  kLicenseStore = 0x000F0000,
  kMovie = 0x00100000,
  kTV = 0x00200000,
  kMusicVideo = 0x00300000,
  kGameVideo = 0x00400000,
  kPodcastVideo = 0x00500000,
  kViralVideo = 0x00600000,
  kCommunityGame = 0x02000000,
};

const static std::map<XContentType, std::string> XContentTypeMap = {
    {XContentType::kSavedGame, "Saved Game"},
    {XContentType::kMarketplaceContent, "Marketplace Content"},
    {XContentType::kPublisher, "Publisher"},
    {XContentType::kXbox360Title, "Xbox 360 Title"},
    {XContentType::kIptvPauseBuffer, "IPTV Pause Buffer"},
    {XContentType::kXNACommunity, "XNA Community"},
    {XContentType::kInstalledGame, "Installed Game"},
    {XContentType::kXboxTitle, "Xbox Title"},
    {XContentType::kSocialTitle, "Social Title"},
    {XContentType::kGamesOnDemand, "Game on Demand"},
    {XContentType::kSUStoragePack, "SU Storage Pack"},
    {XContentType::kAvatarItem, "Avatar Item"},
    {XContentType::kProfile, "Profile"},
    {XContentType::kGamerPicture, "Gamer Picture"},
    {XContentType::kTheme, "Theme"},
    {XContentType::kCacheFile, "Cache File"},
    {XContentType::kStorageDownload, "Storage Download"},
    {XContentType::kXboxSavedGame, "Xbox Saved Game"},
    {XContentType::kXboxDownload, "Xbox Download"},
    {XContentType::kGameDemo, "Game Demo"},
    {XContentType::kVideo, "Video"},
    {XContentType::kGameTitle, "Game Title"},
    {XContentType::kInstaller, "Installer"},
    {XContentType::kGameTrailer, "Game Trailer"},
    {XContentType::kArcadeTitle, "Arcade Title"},
    {XContentType::kXNA, "XNA"},
    {XContentType::kLicenseStore, "License Store"},
    {XContentType::kMovie, "Movie"},
    {XContentType::kTV, "TV"},
    {XContentType::kMusicVideo, "Music Video"},
    {XContentType::kGameVideo, "Game Video"},
    {XContentType::kPodcastVideo, "Podcast Video"},
    {XContentType::kViralVideo, "Viral Video"},
    {XContentType::kCommunityGame, "Community Game"},
};

enum X_MARKETPLACE_ENTRYPOINT : uint32_t {
  ContentList = 0,
  ContentItem = 1,
  MembershipList = 2,
  MembershipItem = 3,
  ContentList_Background = 4,
  ContentItem_Background = 5,
  ForcedNameChangeV1 = 6,
  ForcedNameChangeV2 = 8,
  ProfileNameChange = 9,
  ActiveDownloads = 12
};

enum class XDeploymentType : uint32_t {
  kOpticalDisc = 0,
  kHardDrive = 1,  // Like extracted?
  kGoD = 2,
  kUnknown = 0xFF,
};

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

#pragma pack(push, 4)
struct X_XAMACCOUNTINFO {
  enum AccountReservedFlags {
    kPasswordProtected = 0x10000000,
    kLiveEnabled = 0x20000000,
    kRecovering = 0x40000000,
    kVersionMask = 0x000000FF
  };

  enum AccountUserFlags {
    kPaymentInstrumentCreditCard = 1,

    kCountryMask = 0xFF00,
    kSubscriptionTierMask = 0xF00000,
    kLanguageMask = 0x3E000000,

    kParentalControlEnabled = 0x1000000,
  };

  enum AccountSubscriptionTier {
    kSubscriptionTierNone = 0,
    kSubscriptionTierSilver = 3,
    kSubscriptionTierGold = 6,
    kSubscriptionTierFamilyGold = 9
  };

  enum AccountLiveFlags { kAcctRequiresManagement = 1 };

  xe::be<uint32_t> reserved_flags;
  xe::be<uint32_t> live_flags;
  char16_t gamertag[0x10];
  xe::be<uint64_t> xuid_online;  // 09....
  xe::be<uint32_t> cached_user_flags;
  xe::be<uint32_t> network_id;
  char passcode[4];
  char online_domain[0x14];
  char online_kerberos_realm[0x18];
  char online_key[0x10];
  char passport_membername[0x72];
  char passport_password[0x20];
  char owner_passport_membername[0x72];

  bool IsPasscodeEnabled() {
    return static_cast<bool>(reserved_flags &
                             AccountReservedFlags::kPasswordProtected);
  }

  bool IsLiveEnabled() {
    return static_cast<bool>(reserved_flags &
                             AccountReservedFlags::kLiveEnabled);
  }

  uint32_t GetCachedFlags() const { return cached_user_flags; };

  uint32_t GetCountry() const {
    return (cached_user_flags & kCountryMask) >> 8;
  }

  AccountSubscriptionTier GetSubscriptionTier() const {
    return static_cast<AccountSubscriptionTier>(
        (cached_user_flags & kSubscriptionTierMask) >> 20);
  }

  XLanguage GetLanguage() const {
    return static_cast<XLanguage>((cached_user_flags & kLanguageMask) >> 25);
  }

  std::string GetGamertagString() const {
    return xe::to_utf8(std::u16string(gamertag));
  }
};
static_assert_size(X_XAMACCOUNTINFO, 0x17C);
#pragma pack(pop)

struct X_PROFILEENUMRESULT {
  xe::be<uint64_t> xuid_offline;  // E0.....
  X_XAMACCOUNTINFO account;
  xe::be<uint32_t> device_id;
};
static_assert_size(X_PROFILEENUMRESULT, 0x188);

struct MESSAGEBOX_RESULT {
  union {
    xe::be<uint32_t> ButtonPressed;
    xe::be<uint16_t> Passcode[4];
  };
};

// clang-format off

#define XMBox_NOICON                0x00000000
#define XMBox_ERRORICON             0x00000001
#define XMBox_WARNINGICON           0x00000002
#define XMBox_ALERTICON             0x00000003

#define XMBox_PASSCODEMODE          0x00010000
#define XMBox_VERIFYPASSCODEMODE    0x00020000

#define XMBox_WAITANIMATION         0x00001000
#define XMBox_LIVEPASSCODEMODE      0x00030000
#define XMBox_MODEMASK              0x00030000

#define XMBox_OK                    1
#define XMBox_CANCEL                2

#define X_BUTTON_PASSCODE           0x00005802
#define Y_BUTTON_PASSCODE           0x00005803
#define RIGHT_BUMPER_PASSCODE       0x00005804
#define LEFT_BUMPER_PASSCODE        0x00005805
#define LEFT_TRIGGER_PASSCODE       0x00005806
#define RIGHT_TRIGGER_PASSCODE      0x00005807
#define DPAD_UP_PASSCODE            0x00005810
#define DPAD_DOWN_PASSCODE          0x00005811
#define DPAD_LEFT_PASSCODE          0x00005812
#define DPAD_RIGHT_PASSCODE         0x00005813

// clang-format on

}  // namespace xe

#endif  // XENIA_XBOX_H_
