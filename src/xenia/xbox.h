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

#include "xenia/base/assert.h"
// clang-format off
namespace xe {

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
#define X_STATUS_DISK_FULL                              ((X_STATUS)0xC000007FL)
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
#define X_ERROR_ABORTED                         X_RESULT_FROM_WIN32(0x000004D3L)
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

constexpr uint8_t XUserMaxUserCount = 4;

constexpr uint8_t XUserIndexLatest = 0xFD;
constexpr uint8_t XUserIndexNone = 0xFE;
constexpr uint8_t XUserIndexAny = 0xFF;

// https://github.com/ThirteenAG/Ultimate-ASI-Loader/blob/master/source/xlive/xliveless.h
typedef uint32_t XNotificationID;

struct X_NOTIFICATION_ID {
  uint32_t reserved : 1;  // Always one
  uint32_t area : 6;
  uint32_t version : 9;
  uint32_t message_id : 16;
};
static_assert_size(X_NOTIFICATION_ID, 4);

enum : XNotificationID {
  // Notification Areas
  kXNotifySystem = 0x00000001,
  kXNotifyLive = 0x00000002,
  kXNotifyFriends = 0x00000004,
  kXNotifyCustom = 0x00000008,
  kXNotifyXmp = 0x00000020,
  kXNotifyMsgr = 0x00000040,
  kXNotifyParty = 0x00000080,
  kXNotifyAll = 0x000000EF,

  // XNotification System
  /* System Notes:
     - for some functions if XamIsNuiUIActive returns false then
     XNotifyBroadcast(kXNotificationSystemNUIPause, unk data) is called
     - XNotifyBroadcast(kXNotificationSystemNUIHardwareStatusChanged,
     device_state)
  */
  kXNotificationSystemTitleLoad = 0x80000001,
  kXNotificationSystemTimeZone = 0x80000002,
  kXNotificationSystemLanguage = 0x80000003,
  kXNotificationSystemVideoFlags = 0x80000004,
  kXNotificationSystemAudioFlags = 0x80000005,
  kXNotificationSystemParentalControlGames = 0x80000006,
  kXNotificationSystemParentalControlPassword = 0x80000007,
  kXNotificationSystemParentalControlMovies = 0x80000008,
  kXNotificationSystemUI = 0x00000009,
  kXNotificationSystemSignInChanged = 0x0000000A,
  kXNotificationSystemStorageDevicesChanged = 0x0000000B,
  kXNotificationSystemDashContextChanged = 0x8000000C,
  kXNotificationSystemTrayStateChanged = 0x8000000D,
  kXNotificationSystemProfileSettingChanged = 0x0000000E,
  kXNotificationSystemThemeChanged = 0x8000000F,
  kXNotificationSystemSystemUpdateChanged = 0x80000010,
  kXNotificationSystemMuteListChanged = 0x00000011,
  kXNotificationSystemInputDevicesChanged = 0x00000012,
  kXNotificationSystemXLiveTitleUpdate = 0x00000015,
  kXNotificationSystemXLiveSystemUpdate = 0x00000016,
  kXNotificationSystemInputDeviceConfigChanged = 0x00010013,
  /* Dvd Drive? Notes:
     - after XamLoaderGetMediaInfoEx(media_type?, title_id?, unk) is used for
     some funcs the first param is used with
     XNotifyBroadcast(kXNotificationSystemTrayStateChanged, media_type?)
     - after XamLoaderGetMediaInfoEx(media_type?, title_id?, unk) is used for
     some funcs the third param is used with
     XNotifyBroadcast(kXNotificationUnkUnknown2, unk)
  */
  kXNotificationSystemUnknown = 0x80010014,
  kXNotificationSystemPlayerTimerNotice = 0x00030015,
  kXNotificationSystemAvatarChanged = 0x00040017,
  kXNotificationSystemNUIHardwareStatusChanged = 0x00060019,
  kXNotificationSystemNUIPause = 0x0006001A,
  kXNotificationSystemNUIUIApproach = 0x0006001B,
  kXNotificationSystemDeviceRemap = 0x0006001C,
  kXNotificationSystemNUIBindingChanged = 0x0006001D,
  kXNotificationSystemAudioLatencyChanged = 0x0008001E,
  kXNotificationSystemNUIChatBindingChanged = 0x0008001F,
  kXNotificationSystemInputActivityChanged = 0x00090020,

  // XNotification Live
  kXNotificationLiveConnectionChanged = 0x02000001,
  kXNotificationLiveInviteAccepted = 0x02000002,
  kXNotificationLiveLinkStateChanged = 0x02000003,
  kXNotificationLiveInvitedRecieved = 0x82000004,
  kXNotificationLiveInvitedAnswerRecieved = 0x82000005,
  kXNotificationLiveMessageListChanged = 0x82000006,
  kXNotificationLiveContentInstalled = 0x02000007,
  kXNotificationLiveMembershipPurchased = 0x02000008,
  kXNotificationLiveVoicechatAway = 0x02000009,
  kXNotificationLivePresenceChanged = 0x0200000A,
  kXNotificationLivePointsBalanceChanged = 0x8200000B,
  kXNotificationLivePlayerListChanged = 0x8200000C,
  kXNotificationLiveItemPurchased = 0x8200000D,

  // XNotification Friends
  kXNotificationFriendsPresenceChanged = 0x04000001,
  kXNotificationFriendsFriendAdded = 0x04000002,
  kXNotificationFriendsFriendRemoved = 0x04000003,
  kXNotificationFriendsFriendRequestReceived = 0x84000004,
  kXNotificationFriendsFriendAnswerReceived = 0x84000005,
  kXNotificationFriendsFriendRequestResult = 0x84000006,

  // XNotification Custom
  kXNotificationCustomActionPressed = 0x06000003,
  kXNotificationCustomGamercard = 0x06010004,

  // XNotification XMP
  kXNotificationXmpStateChanged = 0x0A000001,
  kXNotificationXmpPlaybackBehaviorChanged = 0x0A000002,
  kXNotificationXmpPlaybackControllerChanged = 0x0A000003,
  kXNotificationXmpMediaSourceConnectionChanged = 0x8A000004,
  kXNotificationXmpTitlePlayListContentChanged = 0x8A000005,
  kXNotificationXmpLocalMediaContentChanged = 0x8A000006,
  kXNotificationXmpDashNowPlayingQueueModeChanged = 0x8A000007,

  // XNotification Party
  kXNotificationPartyMembersChanged = 0x0E040002,

  // XNotification Msgr
  kXNotificationMsgrUnknown = 0x0C00000E,
};

enum FIRMWARE_REENTRY {
  HalHaltRoutine = 0x0,
  HalRebootRoutine = 0x1,
  HalKdRebootRoutine = 0x2,
  HalFatalErrorRebootRoutine = 0x3,
  HalResetSMCRoutine = 0x4,
  HalPowerDownRoutine = 0x5,
  HalRebootQuiesceRoutine = 0x6,
  HalForceShutdownRoutine = 0x7,
  HalPowerCycleQuiesceRoutine = 0x8,
};

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

enum class XOnlineCountry : uint32_t {
  kUnitedArabEmirates = 1,
  kAlbania = 2,
  kArmenia = 3,
  kArgentina = 4,
  kAustria = 5,
  kAustralia = 6,
  kAzerbaijan = 7,
  kBelgium = 8,
  kBulgaria = 9,
  kBahrain = 10,
  kBruneiDarussalam = 11,
  kBolivia = 12,
  kBrazil = 13,
  kBelarus = 14,
  kBelize = 15,
  kCanada = 16,
  kSwitzerland = 18,
  kChile = 19,
  kChina = 20,
  kColombia = 21,
  kCostaRica = 22,
  kCzechRepublic = 23,
  kGermany = 24,
  kDenmark = 25,
  kDominicanRepublic = 26,
  kAlgeria = 27,
  kEcuador = 28,
  kEstonia = 29,
  kEgypt = 30,
  kSpain = 31,
  kFinland = 32,
  kFaroeIslands = 33,
  kFrance = 34,
  kGreatBritain = 35,
  kGeorgia = 36,
  kGreece = 37,
  kGuatemala = 38,
  kHongKong = 39,
  kHonduras = 40,
  kCroatia = 41,
  kHungary = 42,
  kIndonesia = 43,
  kIreland = 44,
  kIsrael = 45,
  kIndia = 46,
  kIraq = 47,
  kIran = 48,
  kIceland = 49,
  kItaly = 50,
  kJamaica = 51,
  kJordan = 52,
  kJapan = 53,
  kKenya = 54,
  kKyrgyzstan = 55,
  kKorea = 56,
  kKuwait = 57,
  kKazakhstan = 58,
  kLebanon = 59,
  kLiechtenstein = 60,
  kLithuania = 61,
  kLuxembourg = 62,
  kLatvia = 63,
  kLibya = 64,
  kMorocco = 65,
  kMonaco = 66,
  kMacedonia = 67,
  kMongolia = 68,
  kMacau = 69,
  kMaldives = 70,
  kMexico = 71,
  kMalaysia = 72,
  kNicaragua = 73,
  kNetherlands = 74,
  kNorway = 75,
  kNewZealand = 76,
  kOman = 77,
  kPanama = 78,
  kPeru = 79,
  kPhilippines = 80,
  kPakistan = 81,
  kPoland = 82,
  kPuertoRico = 83,
  kPortugal = 84,
  kParaguay = 85,
  kQatar = 86,
  kRomania = 87,
  kRussianFederation = 88,
  kSaudiArabia = 89,
  kSweden = 90,
  kSingapore = 91,
  kSlovenia = 92,
  kSlovakRepublic = 93,
  kElSalvador = 95,
  kSyria = 96,
  kThailand = 97,
  kTunisia = 98,
  kTurkey = 99,
  kTrinidadAndTobago = 100,
  kTaiwan = 101,
  kUkraine = 102,
  kUnitedStates = 103,
  kUruguay = 104,
  kUzbekistan = 105,
  kVenezuela = 106,
  kVietNam = 107,
  kYemen = 108,
  kSouthAfrica = 109,
  kZimbabwe = 110
};

enum class XContentType : uint32_t {
  kInvalid = 0x00000000,
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

inline const std::map<XContentType, std::string> XContentTypeMap = {
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

enum class X_MARKETPLACE_OFFERING_TYPE : uint32_t {
  Content = 0x00000002,
  GameDemo = 0x00000020,
  GameTrailer = 0x00000040,
  Theme = 0x00000080,
  Tile = 0x00000800,
  Arcade = 0x00002000,
  Video = 0x00004000,
  Consumable = 0x00010000,
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

enum X_MARKETPLACE_DOWNLOAD_ITEMS_ENTRYPOINTS : uint32_t {
  FREEITEMS = 1000,
  PAIDITEMS,
};

enum class XDeploymentType : uint32_t {
  kOpticalDisc = 0,
  kInstalledToHDD = 1,  // Like extracted?
  kDownload = 2,
  kOther = 3,
};

}  // namespace xe

#endif  // XENIA_XBOX_H_
