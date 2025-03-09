/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XAM_USER_SETTINGS_H_
#define XENIA_KERNEL_XAM_USER_SETTINGS_H_

#include <array>
#include <set>
#include <variant>
#include <vector>

#include "xenia/kernel/xam/profile_manager.h"
#include "xenia/kernel/xam/user_data.h"
#include "xenia/kernel/xam/user_profile.h"

#include "xenia/xbox.h"

namespace xe {
namespace kernel {
namespace xam {

constexpr uint32_t SettingKey(X_USER_DATA_TYPE type, uint16_t size,
                              uint16_t id) {
  return static_cast<uint32_t>(type) << 28 | size << 16 | id;
}

enum class UserSettingId : uint32_t {
  XPROFILE_PERMISSIONS =
      SettingKey(X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 0),
  XPROFILE_GAMER_TYPE = SettingKey(X_USER_DATA_TYPE::INT32, sizeof(uint32_t),
                                   1),  // 0x10040001,
  XPROFILE_GAMER_YAXIS_INVERSION =
      SettingKey(X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 2),  // 0x10040002,
  XPROFILE_OPTION_CONTROLLER_VIBRATION =
      SettingKey(X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 3),  // 0x10040003,
  XPROFILE_GAMERCARD_ZONE =
      SettingKey(X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 4),  // 0x10040004,
  XPROFILE_GAMERCARD_REGION =
      SettingKey(X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 5),  // 0x10040005,
  XPROFILE_GAMERCARD_CRED =
      SettingKey(X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 6),  // 0x10040006,
  XPROFILE_GAMER_PRESENCE_USER_STATE =
      SettingKey(X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 7),  // 0x10040007,
  XPROFILE_GAMERCARD_HAS_VISION =
      SettingKey(X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 8),  // 0x10040008,

  XPROFILE_OPTION_VOICE_MUTED = SettingKey(
      X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 0xC),  // 0x1004000C,
  XPROFILE_OPTION_VOICE_THRU_SPEAKERS = SettingKey(
      X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 0xD),  // 0x1004000D,
  XPROFILE_OPTION_VOICE_VOLUME = SettingKey(
      X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 0xE),  // 0x1004000E,

  XPROFILE_GAMERCARD_TITLES_PLAYED = SettingKey(
      X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 0x12),  // 0x10040012,
  XPROFILE_GAMERCARD_ACHIEVEMENTS_EARNED = SettingKey(
      X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 0x13),  // 0x10040013,
  XPROFILE_GAMER_DIFFICULTY = SettingKey(
      X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 0x15),  // 0x10040015,
  XPROFILE_GAMER_CONTROL_SENSITIVITY = SettingKey(
      X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 0x18),  // 0x10040018,
  XPROFILE_GAMER_PREFERRED_COLOR_FIRST = SettingKey(
      X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 0x1D),  // 0x1004001D,
  XPROFILE_GAMER_PREFERRED_COLOR_SECOND = SettingKey(
      X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 0x1E),  // 0x1004001E,
  XPROFILE_GAMER_ACTION_AUTO_AIM = SettingKey(
      X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 0x22),  // 0x10040022,
  XPROFILE_GAMER_ACTION_AUTO_CENTER = SettingKey(
      X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 0x23),  // 0x10040023,
  XPROFILE_GAMER_ACTION_MOVEMENT_CONTROL = SettingKey(
      X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 0x24),  // 0x10040024,
  XPROFILE_GAMER_RACE_TRANSMISSION = SettingKey(
      X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 0x26),  // 0x10040026,
  XPROFILE_GAMER_RACE_CAMERA_LOCATION = SettingKey(
      X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 0x27),  // 0x10040027,
  XPROFILE_GAMER_RACE_BRAKE_CONTROL = SettingKey(
      X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 0x28),  // 0x10040028,
  XPROFILE_GAMER_RACE_ACCELERATOR_CONTROL = SettingKey(
      X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 0x29),  // 0x10040029,
  XPROFILE_GAMERCARD_TITLE_CRED_EARNED = SettingKey(
      X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 0x38),  // 0x10040038,
  XPROFILE_GAMERCARD_TITLE_ACHIEVEMENTS_EARNED = SettingKey(
      X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 0x39),  // 0x10040039,
  XPROFILE_GAMER_TIER = SettingKey(X_USER_DATA_TYPE::INT32, sizeof(uint32_t),
                                   0x3A),  // 0x1004003A,
  XPROFILE_MESSENGER_SIGNUP_STATE = SettingKey(
      X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 0x3B),  // 0x1004003B,
  XPROFILE_MESSENGER_AUTO_SIGNIN = SettingKey(
      X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 0x3C),  // 0x1004003C,
  XPROFILE_SAVE_WINDOWS_LIVE_PASSWORD = SettingKey(
      X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 0x3D),  // 0x1004003D,
  XPROFILE_FRIENDSAPP_SHOW_BUDDIES = SettingKey(
      X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 0x3E),  // 0x1004003E,
  XPROFILE_GAMERCARD_SERVICE_TYPE_FLAGS = SettingKey(
      X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 0x3F),  // 0x1004003F,
  XPROFILE_TENURE_LEVEL = SettingKey(X_USER_DATA_TYPE::INT32, sizeof(uint32_t),
                                     0x47),  // 0x10040047,
  XPROFILE_TENURE_MILESTONE = SettingKey(
      X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 0x48),  // 0x10040048,

  XPROFILE_SUBSCRIPTION_TYPE_LENGTH_IN_MONTHS = SettingKey(
      X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 0x4B),  // 0x1004004B,
  XPROFILE_SUBSCRIPTION_PAYMENT_TYPE = SettingKey(
      X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 0x4C),  // 0x1004004C,
  XPROFILE_PEC_INFO = SettingKey(X_USER_DATA_TYPE::INT32, sizeof(uint32_t),
                                 0x4D),  // 0x1004004D,
  XPROFILE_NUI_BIOMETRIC_SIGNIN =
      SettingKey(X_USER_DATA_TYPE::INT32, sizeof(uint32_t),
                 0x4E),  // 0x1004004E, set by XamUserNuiEnableBiometric
  XPROFILE_GFWL_VADNORMAL = SettingKey(X_USER_DATA_TYPE::INT32,
                                       sizeof(uint32_t), 0x4F),  // 0x1004004F,
  XPROFILE_BEACONS_SOCIAL_NETWORK_SHARING = SettingKey(
      X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 0x52),  // 0x10040052,
  XPROFILE_USER_PREFERENCES = SettingKey(
      X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 0x53),  // 0x10040053,
  XPROFILE_XBOXONE_GAMERSCORE =
      SettingKey(X_USER_DATA_TYPE::INT32, sizeof(uint32_t),
                 0x57),  // 0x10040057, "XboxOneGamerscore" inside dash.xex

  WEB_EMAIL_FORMAT = SettingKey(X_USER_DATA_TYPE::INT32, sizeof(uint32_t),
                                0x2000),  // 0x10042000,
  WEB_FLAGS = SettingKey(X_USER_DATA_TYPE::INT32, sizeof(uint32_t),
                         0x2001),  // 0x10042001,
  WEB_SPAM = SettingKey(X_USER_DATA_TYPE::INT32, sizeof(uint32_t),
                        0x2002),  // 0x10042002,
  WEB_FAVORITE_GENRE = SettingKey(X_USER_DATA_TYPE::INT32, sizeof(uint32_t),
                                  0x2003),  // 0x10042003,
  WEB_FAVORITE_GAME = SettingKey(X_USER_DATA_TYPE::INT32, sizeof(uint32_t),
                                 0x2004),  // 0x10042004,
  WEB_FAVORITE_GAME1 = SettingKey(X_USER_DATA_TYPE::INT32, sizeof(uint32_t),
                                  0x2005),  // 0x10042005,
  WEB_FAVORITE_GAME2 = SettingKey(X_USER_DATA_TYPE::INT32, sizeof(uint32_t),
                                  0x2006),  // 0x10042006,
  WEB_FAVORITE_GAME3 = SettingKey(X_USER_DATA_TYPE::INT32, sizeof(uint32_t),
                                  0x2007),  // 0x10042007,
  WEB_FAVORITE_GAME4 = SettingKey(X_USER_DATA_TYPE::INT32, sizeof(uint32_t),
                                  0x2008),  // 0x10042008,
  WEB_FAVORITE_GAME5 = SettingKey(X_USER_DATA_TYPE::INT32, sizeof(uint32_t),
                                  0x2009),  // 0x10042009,
  WEB_PLATFORMS_OWNED = SettingKey(X_USER_DATA_TYPE::INT32, sizeof(uint32_t),
                                   0x200A),  // 0x1004200A,
  WEB_CONNECTION_SPEED = SettingKey(X_USER_DATA_TYPE::INT32, sizeof(uint32_t),
                                    0x200B),  // 0x1004200B,
  WEB_FLASH = SettingKey(X_USER_DATA_TYPE::INT32, sizeof(uint32_t),
                         0x200C),  // 0x1004200C,
  WEB_VIDEO_PREFERENCE = SettingKey(X_USER_DATA_TYPE::INT32, sizeof(uint32_t),
                                    0x200D),  // 0x1004200D,
  XPROFILE_CRUX_MEDIA_STYLE1 = SettingKey(
      X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 0x3EA),  // 0x100403EA,
  XPROFILE_CRUX_MEDIA_STYLE2 = SettingKey(
      X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 0x3EB),  // 0x100403EB,
  XPROFILE_CRUX_MEDIA_STYLE3 = SettingKey(
      X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 0x3EC),  // 0x100403EC,
  XPROFILE_CRUX_TOP_ALBUM1 = SettingKey(
      X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 0x3ED),  // 0x100403ED,
  XPROFILE_CRUX_TOP_ALBUM2 = SettingKey(
      X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 0x3EE),  // 0x100403EE,
  XPROFILE_CRUX_TOP_ALBUM3 = SettingKey(
      X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 0x3EF),  // 0x100403EF,
  XPROFILE_CRUX_TOP_ALBUM4 = SettingKey(
      X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 0x3F0),  // 0x100403F0,
  XPROFILE_CRUX_TOP_ALBUM5 = SettingKey(
      X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 0x3F1),  // 0x100403F1,
  XPROFILE_CRUX_BKGD_IMAGE = SettingKey(
      X_USER_DATA_TYPE::INT32, sizeof(uint32_t), 0x3F3),  // 0x100403F3,

  XPROFILE_GAMERCARD_USER_LOCATION =
      SettingKey(X_USER_DATA_TYPE::WSTRING, 0x52, 0x41),  // 0x40520041,

  XPROFILE_GAMERCARD_USER_NAME =
      SettingKey(X_USER_DATA_TYPE::WSTRING, 0x104, 0x40),  // 0x41040040,

  XPROFILE_GAMERCARD_USER_URL =
      SettingKey(X_USER_DATA_TYPE::WSTRING, 0x190, 0x42),  // 0x41900042,
  XPROFILE_GAMERCARD_USER_BIO = SettingKey(
      X_USER_DATA_TYPE::WSTRING, kMaxUserDataSize, 0x43),  // 0x43E80043,

  XPROFILE_CRUX_BIO = SettingKey(X_USER_DATA_TYPE::WSTRING, kMaxUserDataSize,
                                 0x3FA),  // 0x43E803FA,
  XPROFILE_CRUX_BG_SMALL_PRIVATE =
      SettingKey(X_USER_DATA_TYPE::WSTRING, 0x64, 0x3FB),  // 0x406403FB,
  XPROFILE_CRUX_BG_LARGE_PRIVATE =
      SettingKey(X_USER_DATA_TYPE::WSTRING, 0x64, 0x3FC),  // 0x406403FC,
  XPROFILE_CRUX_BG_SMALL_PUBLIC =
      SettingKey(X_USER_DATA_TYPE::WSTRING, 0x64, 0x3FD),  // 0x406403FD,
  XPROFILE_CRUX_BG_LARGE_PUBLIC =
      SettingKey(X_USER_DATA_TYPE::WSTRING, 0x64, 0x3FE),  // 0x406403FE

  XPROFILE_GAMERCARD_PICTURE_KEY =
      SettingKey(X_USER_DATA_TYPE::WSTRING, 0x64, 0xF),  // 0x4064000F,
  XPROFILE_GAMERCARD_PERSONAL_PICTURE =
      SettingKey(X_USER_DATA_TYPE::WSTRING, 0x64, 0x10),  // 0x40640010,
  XPROFILE_GAMERCARD_MOTTO =
      SettingKey(X_USER_DATA_TYPE::WSTRING, 0x2C, 0x11),  // 0x402C0011,
  XPROFILE_GFWL_RECDEVICEDESC =
      SettingKey(X_USER_DATA_TYPE::WSTRING, 200, 0x49),  // 0x40C80049,

  XPROFILE_GFWL_PLAYDEVICEDESC =
      SettingKey(X_USER_DATA_TYPE::WSTRING, 200, 0x4B),  // 0x40C8004B,
  XPROFILE_CRUX_MEDIA_PICTURE =
      SettingKey(X_USER_DATA_TYPE::WSTRING, 0x64, 0x3E8),  // 0x406403E8,
  XPROFILE_CRUX_MEDIA_MOTTO =
      SettingKey(X_USER_DATA_TYPE::WSTRING, 0x100, 0x3F6),  // 0x410003F6,

  XPROFILE_GAMERCARD_REP =
      SettingKey(X_USER_DATA_TYPE::FLOAT, sizeof(float), 0xB),  // 0x5004000B,
  XPROFILE_GFWL_VOLUMELEVEL =
      SettingKey(X_USER_DATA_TYPE::FLOAT, sizeof(float), 0x4C),  // 0x5004004C,

  XPROFILE_GFWL_RECLEVEL = SettingKey(X_USER_DATA_TYPE::FLOAT, sizeof(float),
                                      0x4D),  // 0x5004004D,
  XPROFILE_GFWL_PLAYDEVICE =
      SettingKey(X_USER_DATA_TYPE::BINARY, 0x10, 0x4A),  // 0x6010004A,

  XPROFILE_VIDEO_METADATA =
      SettingKey(X_USER_DATA_TYPE::BINARY, 0x20, 0x4A),  // 0x6020004A,

  XPROFILE_CRUX_OFFLINE_ID =
      SettingKey(X_USER_DATA_TYPE::BINARY, 0x34, 0x3F2),  // 0x603403F2,

  XPROFILE_UNK_61180050 =
      SettingKey(X_USER_DATA_TYPE::BINARY, 280, 0x50),  // 0x61180050,

  XPROFILE_JUMP_IN_LIST = SettingKey(X_USER_DATA_TYPE::BINARY, kMaxUserDataSize,
                                     0x51),  // 0x63E80051,

  XPROFILE_GAMERCARD_PARTY_ADDR =
      SettingKey(X_USER_DATA_TYPE::BINARY, 0x62, 0x54),  // 0x60620054,

  XPROFILE_CRUX_TOP_MUSIC =
      SettingKey(X_USER_DATA_TYPE::BINARY, 0xA8, 0x3F5),  // 0x60A803F5,

  XPROFILE_CRUX_TOP_MEDIAID1 =
      SettingKey(X_USER_DATA_TYPE::BINARY, 0x10, 0x3F7),  // 0x601003F7,
  XPROFILE_CRUX_TOP_MEDIAID2 =
      SettingKey(X_USER_DATA_TYPE::BINARY, 0x10, 0x3F8),  // 0x601003F8,
  XPROFILE_CRUX_TOP_MEDIAID3 =
      SettingKey(X_USER_DATA_TYPE::BINARY, 0x10, 0x3F9),  // 0x601003F9,

  XPROFILE_GAMERCARD_AVATAR_INFO_1 = SettingKey(
      X_USER_DATA_TYPE::BINARY, kMaxUserDataSize, 0x44),  // 0x63E80044,
  XPROFILE_GAMERCARD_AVATAR_INFO_2 = SettingKey(
      X_USER_DATA_TYPE::BINARY, kMaxUserDataSize, 0x45),  // 0x63E80045,
  XPROFILE_GAMERCARD_PARTY_INFO =
      SettingKey(X_USER_DATA_TYPE::BINARY, 0x100, 0x46),  // 0x61000046,

  XPROFILE_TITLE_SPECIFIC1 = SettingKey(
      X_USER_DATA_TYPE::BINARY, kMaxUserDataSize, 0x3FFF),  // 0x63E83FFF,
  XPROFILE_TITLE_SPECIFIC2 = SettingKey(
      X_USER_DATA_TYPE::BINARY, kMaxUserDataSize, 0x3FFE),  // 0x63E83FFE,
  XPROFILE_TITLE_SPECIFIC3 = SettingKey(
      X_USER_DATA_TYPE::BINARY, kMaxUserDataSize, 0x3FFD),  // 0x63E83FFD,

  XPROFILE_CRUX_LAST_CHANGE_TIME = SettingKey(
      X_USER_DATA_TYPE::DATETIME, sizeof(uint64_t), 0x3F4),  // 0x700803F4,
  XPROFILE_TENURE_NEXT_MILESTONE_DATE =
      SettingKey(X_USER_DATA_TYPE::DATETIME, sizeof(uint64_t),
                 0x49),  // 0x70080049, aka ProfileDateTimeCreated?
  XPROFILE_LAST_LIVE_SIGNIN =
      SettingKey(X_USER_DATA_TYPE::DATETIME, sizeof(uint64_t),
                 0x4F),  // 0x7008004F, named "LastOnLIVE" in Velocity
};

constexpr static std::array<UserSettingId, 104> known_settings = {
    UserSettingId::XPROFILE_PERMISSIONS,
    UserSettingId::XPROFILE_GAMER_TYPE,
    UserSettingId::XPROFILE_GAMER_YAXIS_INVERSION,
    UserSettingId::XPROFILE_OPTION_CONTROLLER_VIBRATION,
    UserSettingId::XPROFILE_GAMERCARD_ZONE,
    UserSettingId::XPROFILE_GAMERCARD_REGION,
    UserSettingId::XPROFILE_GAMERCARD_CRED,
    UserSettingId::XPROFILE_GAMER_PRESENCE_USER_STATE,
    UserSettingId::XPROFILE_GAMERCARD_HAS_VISION,
    UserSettingId::XPROFILE_OPTION_VOICE_MUTED,
    UserSettingId::XPROFILE_OPTION_VOICE_THRU_SPEAKERS,
    UserSettingId::XPROFILE_OPTION_VOICE_VOLUME,
    UserSettingId::XPROFILE_GAMERCARD_TITLES_PLAYED,
    UserSettingId::XPROFILE_GAMERCARD_ACHIEVEMENTS_EARNED,
    UserSettingId::XPROFILE_GAMER_DIFFICULTY,
    UserSettingId::XPROFILE_GAMER_CONTROL_SENSITIVITY,
    UserSettingId::XPROFILE_GAMER_PREFERRED_COLOR_FIRST,
    UserSettingId::XPROFILE_GAMER_PREFERRED_COLOR_SECOND,
    UserSettingId::XPROFILE_GAMER_ACTION_AUTO_AIM,
    UserSettingId::XPROFILE_GAMER_ACTION_AUTO_CENTER,
    UserSettingId::XPROFILE_GAMER_ACTION_MOVEMENT_CONTROL,
    UserSettingId::XPROFILE_GAMER_RACE_TRANSMISSION,
    UserSettingId::XPROFILE_GAMER_RACE_CAMERA_LOCATION,
    UserSettingId::XPROFILE_GAMER_RACE_BRAKE_CONTROL,
    UserSettingId::XPROFILE_GAMER_RACE_ACCELERATOR_CONTROL,
    UserSettingId::XPROFILE_GAMERCARD_TITLE_CRED_EARNED,
    UserSettingId::XPROFILE_GAMERCARD_TITLE_ACHIEVEMENTS_EARNED,
    UserSettingId::XPROFILE_GAMER_TIER,
    UserSettingId::XPROFILE_MESSENGER_SIGNUP_STATE,
    UserSettingId::XPROFILE_MESSENGER_AUTO_SIGNIN,
    UserSettingId::XPROFILE_SAVE_WINDOWS_LIVE_PASSWORD,
    UserSettingId::XPROFILE_FRIENDSAPP_SHOW_BUDDIES,
    UserSettingId::XPROFILE_GAMERCARD_SERVICE_TYPE_FLAGS,
    UserSettingId::XPROFILE_TENURE_LEVEL,
    UserSettingId::XPROFILE_TENURE_MILESTONE,
    UserSettingId::XPROFILE_SUBSCRIPTION_TYPE_LENGTH_IN_MONTHS,
    UserSettingId::XPROFILE_SUBSCRIPTION_PAYMENT_TYPE,
    UserSettingId::XPROFILE_PEC_INFO,
    UserSettingId::XPROFILE_NUI_BIOMETRIC_SIGNIN,
    UserSettingId::XPROFILE_GFWL_VADNORMAL,
    UserSettingId::XPROFILE_BEACONS_SOCIAL_NETWORK_SHARING,
    UserSettingId::XPROFILE_USER_PREFERENCES,
    UserSettingId::XPROFILE_XBOXONE_GAMERSCORE,
    UserSettingId::WEB_EMAIL_FORMAT,
    UserSettingId::WEB_FLAGS,
    UserSettingId::WEB_SPAM,
    UserSettingId::WEB_FAVORITE_GENRE,
    UserSettingId::WEB_FAVORITE_GAME,
    UserSettingId::WEB_FAVORITE_GAME1,
    UserSettingId::WEB_FAVORITE_GAME2,
    UserSettingId::WEB_FAVORITE_GAME3,
    UserSettingId::WEB_FAVORITE_GAME4,
    UserSettingId::WEB_FAVORITE_GAME5,
    UserSettingId::WEB_PLATFORMS_OWNED,
    UserSettingId::WEB_CONNECTION_SPEED,
    UserSettingId::WEB_FLASH,
    UserSettingId::WEB_VIDEO_PREFERENCE,
    UserSettingId::XPROFILE_CRUX_MEDIA_STYLE1,
    UserSettingId::XPROFILE_CRUX_MEDIA_STYLE2,
    UserSettingId::XPROFILE_CRUX_MEDIA_STYLE3,
    UserSettingId::XPROFILE_CRUX_TOP_ALBUM1,
    UserSettingId::XPROFILE_CRUX_TOP_ALBUM2,
    UserSettingId::XPROFILE_CRUX_TOP_ALBUM3,
    UserSettingId::XPROFILE_CRUX_TOP_ALBUM4,
    UserSettingId::XPROFILE_CRUX_TOP_ALBUM5,
    UserSettingId::XPROFILE_CRUX_BKGD_IMAGE,
    UserSettingId::XPROFILE_GAMERCARD_USER_LOCATION,
    UserSettingId::XPROFILE_GAMERCARD_USER_NAME,
    UserSettingId::XPROFILE_GAMERCARD_USER_URL,
    UserSettingId::XPROFILE_GAMERCARD_USER_BIO,
    UserSettingId::XPROFILE_CRUX_BIO,
    UserSettingId::XPROFILE_CRUX_BG_SMALL_PRIVATE,
    UserSettingId::XPROFILE_CRUX_BG_LARGE_PRIVATE,
    UserSettingId::XPROFILE_CRUX_BG_SMALL_PUBLIC,
    UserSettingId::XPROFILE_CRUX_BG_LARGE_PUBLIC,
    UserSettingId::XPROFILE_GAMERCARD_PICTURE_KEY,
    UserSettingId::XPROFILE_GAMERCARD_PERSONAL_PICTURE,
    UserSettingId::XPROFILE_GAMERCARD_MOTTO,
    UserSettingId::XPROFILE_GFWL_RECDEVICEDESC,
    UserSettingId::XPROFILE_GFWL_PLAYDEVICEDESC,
    UserSettingId::XPROFILE_CRUX_MEDIA_PICTURE,
    UserSettingId::XPROFILE_CRUX_MEDIA_MOTTO,
    UserSettingId::XPROFILE_GAMERCARD_REP,
    UserSettingId::XPROFILE_GFWL_VOLUMELEVEL,
    UserSettingId::XPROFILE_GFWL_RECLEVEL,
    UserSettingId::XPROFILE_GFWL_PLAYDEVICE,
    UserSettingId::XPROFILE_VIDEO_METADATA,
    UserSettingId::XPROFILE_CRUX_OFFLINE_ID,
    UserSettingId::XPROFILE_UNK_61180050,
    UserSettingId::XPROFILE_JUMP_IN_LIST,
    UserSettingId::XPROFILE_GAMERCARD_PARTY_ADDR,
    UserSettingId::XPROFILE_CRUX_TOP_MUSIC,
    UserSettingId::XPROFILE_CRUX_TOP_MEDIAID1,
    UserSettingId::XPROFILE_CRUX_TOP_MEDIAID2,
    UserSettingId::XPROFILE_CRUX_TOP_MEDIAID3,
    UserSettingId::XPROFILE_GAMERCARD_AVATAR_INFO_1,
    UserSettingId::XPROFILE_GAMERCARD_AVATAR_INFO_2,
    UserSettingId::XPROFILE_GAMERCARD_PARTY_INFO,
    UserSettingId::XPROFILE_TITLE_SPECIFIC1,
    UserSettingId::XPROFILE_TITLE_SPECIFIC2,
    UserSettingId::XPROFILE_TITLE_SPECIFIC3,
    UserSettingId::XPROFILE_CRUX_LAST_CHANGE_TIME,
    UserSettingId::XPROFILE_TENURE_NEXT_MILESTONE_DATE,
    UserSettingId::XPROFILE_LAST_LIVE_SIGNIN,
};

const static std::set<UserSettingId> title_writable_settings = {
    UserSettingId::XPROFILE_TITLE_SPECIFIC1,
    UserSettingId::XPROFILE_TITLE_SPECIFIC2,
    UserSettingId::XPROFILE_TITLE_SPECIFIC3};

enum PREFERRED_COLOR_OPTIONS : uint32_t {
  PREFERRED_COLOR_NONE,
  PREFERRED_COLOR_BLACK,
  PREFERRED_COLOR_WHITE,
  PREFERRED_COLOR_YELLOW,
  PREFERRED_COLOR_ORANGE,
  PREFERRED_COLOR_PINK,
  PREFERRED_COLOR_RED,
  PREFERRED_COLOR_PURPLE,
  PREFERRED_COLOR_BLUE,
  PREFERRED_COLOR_GREEN,
  PREFERRED_COLOR_BROWN,
  PREFERRED_COLOR_SILVER
};

class UserSetting : public UserData {
 public:
  UserSetting(UserSetting& setting);
  UserSetting(const UserSetting& setting);
  // Ctor for writing from host
  UserSetting(UserSettingId setting_id, UserDataTypes setting_data);
  // Ctor for writing to GPD
  UserSetting(const X_USER_PROFILE_SETTING* profile_setting);
  // Ctor for reading from GPD
  UserSetting(const X_XDBF_GPD_SETTING_HEADER* profile_setting,
              std::span<const uint8_t> extended_data);

  static std::optional<UserSetting> GetDefaultSetting(const UserProfile* user,
                                                      uint32_t setting_id);
  void WriteToGuest(X_USER_PROFILE_SETTING* setting_ptr,
                    uint32_t& extended_data_address);
  std::vector<uint8_t> Serialize() const;

  uint32_t get_setting_id() const { return static_cast<uint32_t>(setting_id_); }
  X_USER_PROFILE_SETTING_SOURCE get_setting_source() const {
    return setting_source_;
  }

  static bool is_setting_valid(uint32_t setting_id) {
    return std::find(known_settings.cbegin(), known_settings.cend(),
                     static_cast<UserSettingId>(setting_id)) !=
           known_settings.cend();
  }

 private:
  UserSettingId setting_id_;
  X_USER_PROFILE_SETTING_SOURCE setting_source_;

  static bool is_valid_setting(uint32_t title_id,
                               const UserSettingId setting_id) {
    if (title_id != kDashboardID && title_writable_settings.count(setting_id)) {
      return true;
    }

    if (title_id == kDashboardID &&
        !title_writable_settings.count(setting_id)) {
      return true;
    }
    return false;
  }

  static bool is_title_specific(uint32_t setting_id) {
    return (setting_id & 0x3F00) == 0x3F00;
  }

  bool is_title_specific() const {
    return is_title_specific(static_cast<uint32_t>(setting_id_));
  }
};

}  // namespace xam
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XAM_USER_PROFILE_H_
