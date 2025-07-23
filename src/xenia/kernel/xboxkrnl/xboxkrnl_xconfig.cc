/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/xboxkrnl/xboxkrnl_xconfig.h"
#include "xenia/base/logging.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xboxkrnl/xboxkrnl_private.h"
#include "xenia/xbox.h"

DEFINE_int32(user_language, 1,
             "User language ID.\n"
             "  1=en  2=ja  3=de  4=fr  5=es  6=it  7=ko  8=zh\n"
             "  9=pt 11=pl 12=ru 13=sv 14=tr 15=nb 16=nl 17=zh",
             "XConfig");

DEFINE_int32(user_country, 103,
             "User country ID.\n"
             "   1=AE   2=AL   3=AM   4=AR   5=AT   6=AU   7=AZ   8=BE   9=BG\n"
             "  10=BH  11=BN  12=BO  13=BR  14=BY  15=BZ  16=CA  18=CH  19=CL\n"
             "  20=CN  21=CO  22=CR  23=CZ  24=DE  25=DK  26=DO  27=DZ  28=EC\n"
             "  29=EE  30=EG  31=ES  32=FI  33=FO  34=FR  35=GB  36=GE  37=GR\n"
             "  38=GT  39=HK  40=HN  41=HR  42=HU  43=ID  44=IE  45=IL  46=IN\n"
             "  47=IQ  48=IR  49=IS  50=IT  51=JM  52=JO  53=JP  54=KE  55=KG\n"
             "  56=KR  57=KW  58=KZ  59=LB  60=LI  61=LT  62=LU  63=LV  64=LY\n"
             "  65=MA  66=MC  67=MK  68=MN  69=MO  70=MV  71=MX  72=MY  73=NI\n"
             "  74=NL  75=NO  76=NZ  77=OM  78=PA  79=PE  80=PH  81=PK  82=PL\n"
             "  83=PR  84=PT  85=PY  86=QA  87=RO  88=RU  89=SA  90=SE  91=SG\n"
             "  92=SI  93=SK  95=SV  96=SY  97=TH  98=TN  99=TR 100=TT 101=TW\n"
             " 102=UA 103=US 104=UY 105=UZ 106=VE 107=VN 108=YE 109=ZA\n",
             "XConfig");

DEFINE_uint32(
    audio_flag, 0x00010001,
    "Audio Mode Analog.\n"
    " 0x00000001 = Dolby Pro Logic\n"
    " 0x00000002 = Analog Mono\n"
    "Audio Mode Digital.\n"
    " 0x00000000 = Digital Stereo (choose one of the above by itself)\n"
    " 0x00010000 = Dolby Digital\n"
    " 0x00030000 = Dolby Digital with WMA PRO\n"
    "Special Flags.\n"
    " 0x00000003 = Stereo Bypass\n"
    " 0x80000000 = Low Latency\n"
    " This Config requires you to pair an analog and digitial flag together\n"
    " while digital stereo only requires an analog flag. Bonus flags are\n"
    " optional. Ex) 0x00010001\n",
    "XConfig");

DECLARE_bool(widescreen);
DECLARE_bool(use_50Hz_mode);
DECLARE_int32(video_standard);
DECLARE_uint32(internal_display_resolution);

namespace xe {
namespace kernel {
namespace xboxkrnl {

X_STATUS xeExGetXConfigSetting(X_CONFIG_CATEGORY category, uint16_t setting,
                               void* buffer, uint16_t buffer_size,
                               uint16_t* required_size) {
  uint16_t setting_size = 0;
  alignas(uint32_t) uint8_t value[4];

  // TODO(benvanik): have real structs here that just get copied from.
  // https://free60project.github.io/wiki/XConfig.html
  // https://github.com/oukiar/freestyledash/blob/master/Freestyle/Tools/Generic/ExConfig.h
  switch (category) {
    case XCONFIG_SECURED_CATEGORY:
      switch (setting) {
        case XCONFIG_SECURED_AV_REGION:
          setting_size = 4;
          switch (cvars::video_standard) {
            case 1:  // NTSCM
              xe::store_and_swap<uint32_t>(value, X_AV_REGION::NTSCM);
              break;
            case 2:  // NTSCJ
              xe::store_and_swap<uint32_t>(value, X_AV_REGION::NTSCJ);
              break;
            case 3:  // PAL
              xe::store_and_swap<uint32_t>(value, cvars::use_50Hz_mode
                                                      ? X_AV_REGION::PAL_50
                                                      : X_AV_REGION::PAL);
              break;
            default:
              xe::store_and_swap<uint32_t>(value, 0);
              break;
          }
          break;
        default:
          XELOGW(
              "An unimplemented setting 0x{:04X} in XCONFIG SECURED CATEGORY",
              static_cast<uint16_t>(setting));
          assert_unhandled_case(setting);
          return X_STATUS_INVALID_PARAMETER_2;
      }
      break;
    case XCONFIG_USER_CATEGORY:
      switch (setting) {
        case XCONFIG_USER_TIME_ZONE_BIAS:
        case XCONFIG_USER_TIME_ZONE_STD_NAME:
        case XCONFIG_USER_TIME_ZONE_DLT_NAME:
        case XCONFIG_USER_TIME_ZONE_STD_DATE:
        case XCONFIG_USER_TIME_ZONE_DLT_DATE:
        case XCONFIG_USER_TIME_ZONE_STD_BIAS:
        case XCONFIG_USER_TIME_ZONE_DLT_BIAS:
          setting_size = 4;
          // TODO(benvanik): get this value.
          xe::store_and_swap<uint32_t>(value, 0);
          break;
        case XCONFIG_USER_LANGUAGE:
          setting_size = 4;
          xe::store_and_swap<uint32_t>(value, cvars::user_language);
          break;
        case XCONFIG_USER_VIDEO_FLAGS:
          setting_size = 4;
          xe::store_and_swap<uint32_t>(value, cvars::widescreen
                                                  ? X_VIDEO_FLAGS::Widescreen
                                                  : X_VIDEO_FLAGS::RatioNormal);
          break;
        case XCONFIG_USER_AUDIO_FLAGS:
          setting_size = 4;
          xe::store_and_swap<uint32_t>(value, cvars::audio_flag);
          break;
        case XCONFIG_USER_RETAIL_FLAGS:
          setting_size = 4;
          // TODO(benvanik): get this value.
          xe::store_and_swap<uint32_t>(value, 0x40);
          break;
        case XCONFIG_USER_COUNTRY:
          setting_size = 1;
          value[0] = static_cast<uint8_t>(cvars::user_country);
          break;
        case XCONFIG_USER_PC_FLAGS:
          setting_size = 1;
          // All related flags must be set for PC to function
          // Both flags set even when PC are off
          value[0] =
              X_PC_FLAGS::XBLAllowed | X_PC_FLAGS::XBLMembershipCreationAllowed;
          break;
        case XCONFIG_USER_AV_COMPONENT_SCREENSZ:
          setting_size = 4;
          // int16_t* value[2];
          if (XHDTVResolution.find(cvars::internal_display_resolution) !=
              XHDTVResolution.cend()) {
            xe::store_and_swap<int32_t>(
                value, XHDTVResolution.at(cvars::internal_display_resolution));
          } else {
            XELOGW("Resolution not supported for AV Component");
            xe::store_and_swap<int32_t>(value, 0);
          }
          break;
        case XCONFIG_USER_AV_VGA_SCREENSZ:
          setting_size = 4;
          // int16_t* value[2];
          if (XVGAResolution.find(cvars::internal_display_resolution) !=
              XVGAResolution.cend()) {
            xe::store_and_swap<int32_t>(
                value, XVGAResolution.at(cvars::internal_display_resolution));
          } else {
            XELOGW("Resolution not supported for VGA");
            xe::store_and_swap<int32_t>(value, 0);
          }
          break;
        case XCONFIG_USER_PC_GAME:
          setting_size = 4;
          xe::store_and_swap<uint32_t>(value,
                                       X_PC_GAMES_FLAGS::NoGameRestrictions);
          break;
        case XCONFIG_USER_PC_PASSWORD:
          setting_size = 4;
          std::memset(value, 0, 4);
          break;
        case XCONFIG_USER_PC_MOVIE:
          setting_size = 4;
          xe::store_and_swap<uint32_t>(value,
                                       X_PC_MOVIE_FLAGS::NoMovieRestrictions);
          break;
        case XCONFIG_USER_PC_GAME_RATING:
          setting_size = 4;
          xe::store_and_swap<uint32_t>(value,
                                       X_PC_GAME_RATING_FLAGS::DefaultGame);
          break;
        case XCONFIG_USER_PC_MOVIE_RATING:
          setting_size = 4;
          xe::store_and_swap<uint32_t>(value,
                                       X_PC_MOVIE_RATING_FLAGS::DefaultMovie);
          break;
        case XCONFIG_USER_PC_HINT:
          // ExSetXConfigSetting and ExGetXConfigSetting request size of 0x40
          setting_size = 0x40;
          store_and_swap<std::string>(value, "");
          break;
        case XCONFIG_USER_PC_HINT_ANSWER:
          setting_size = 0x20;
          store_and_swap<std::string>(value, "");
          break;
        case XCONFIG_USER_ARCADE_FLAGS:
          setting_size = 4;
          xe::store_and_swap<uint32_t>(value, X_ARCADE_FLAGS::AutoDownloadOff);
          break;
        case XCONFIG_USER_PC_VERSION:
          setting_size = 4;
          xe::store_and_swap<uint32_t>(value, X_PC_VERSION::VersionOne);
          break;
        case XCONFIG_USER_PC_TV:
          setting_size = 4;
          xe::store_and_swap<uint32_t>(value, X_PC_TV::NoTVRestrictions);
          break;
        case XCONFIG_USER_PC_TV_RATING:
          setting_size = 4;
          xe::store_and_swap<uint32_t>(value, X_PC_TV_RATING::DefaultTV);
          break;
        case XCONFIG_USER_PC_EXPLICIT_VIDEO:
          setting_size = 4;
          xe::store_and_swap<uint32_t>(
              value, X_PC_EXPLICIT_VIDEO::ExplicitVideoAllowed);
          break;
        case XCONFIG_USER_PC_EXPLICIT_VIDEO_RATING:
          setting_size = 4;
          xe::store_and_swap<uint32_t>(
              value, X_PC_EXPLICIT_VIDEO_RATING::ExplicitAllowed);
          break;
        case XCONFIG_USER_PC_UNRATED_VIDEO:
          setting_size = 4;
          xe::store_and_swap<uint32_t>(value,
                                       X_PC_EXPLICIT_UNRATED::UnratedALL);
          break;
        case XCONFIG_USER_PC_UNRATED_VIDEO_RATING:
          setting_size = 4;
          xe::store_and_swap<uint32_t>(
              value, X_PC_EXPLICIT_UNRATED_RATING::DefaultExplicitUnrated);
          break;
        case XCONFIG_USER_VIDEO_OUTPUT_BLACK_LEVELS:
          setting_size = 4;
          xe::store_and_swap<uint32_t>(value, X_BLACK_LEVEL::LevelNormal);
          break;
        default:
          XELOGW("An unimplemented setting 0x{:04X} in XCONFIG USER CATEGORY",
                 static_cast<uint16_t>(setting));
          assert_unhandled_case(setting);
          return X_STATUS_INVALID_PARAMETER_2;
      }
      break;
    case XCONFIG_CONSOLE_CATEGORY:
      switch (setting) {
        case XCONFIG_CONSOLE_SCREEN_SAVER:
          setting_size = 2;
          xe::store_and_swap<int16_t>(value, X_SCREENSAVER::ScreensaverOff);
          break;
        case XCONFIG_CONSOLE_AUTO_SHUT_OFF:
          setting_size = 2;
          xe::store_and_swap<int16_t>(value, X_AUTO_SHUTDOWN::AutoShutdownOff);
          break;
        case XCONFIG_CONSOLE_CAMERA_SETTINGS:
          // Camera Flags are added together and last byte is always 0x1
          setting_size = 4;
          xe::store_and_swap<uint32_t>(value, X_CAMERA_FLAGS::AutoAll);
          break;
        case XCONFIG_CONSOLE_KEYBOARD_LAYOUT:
          setting_size = 2;
          xe::store_and_swap<int16_t>(value,
                                      X_KEYBOARD_LAYOUT::KeyboardDefault);
          break;
        default:
          XELOGW(
              "An unimplemented setting 0x{:04X} in XCONFIG CONSOLE CATEGORY",
              static_cast<uint16_t>(setting));
          assert_unhandled_case(setting);
          return X_STATUS_INVALID_PARAMETER_2;
      }
      break;
    default:
      XELOGW("An unimplemented category 0x{:04X}",
             static_cast<uint16_t>(category));
      assert_unhandled_case(category);
      return X_STATUS_INVALID_PARAMETER_1;
  }

  if (buffer) {
    if (buffer_size < setting_size) {
      return X_STATUS_BUFFER_TOO_SMALL;
    }
    std::memcpy(buffer, value, setting_size);
  } else {
    if (buffer_size) {
      return X_STATUS_INVALID_PARAMETER_3;
    }
  }

  if (required_size) {
    *required_size = setting_size;
  }

  return X_STATUS_SUCCESS;
}

dword_result_t ExGetXConfigSetting_entry(word_t category, word_t setting,
                                         lpvoid_t buffer_ptr,
                                         word_t buffer_size,
                                         lpword_t required_size_ptr) {
  uint16_t required_size = 0;
  X_STATUS result =
      xeExGetXConfigSetting(static_cast<X_CONFIG_CATEGORY>(category.value()),
                            setting, buffer_ptr, buffer_size, &required_size);

  if (required_size_ptr) {
    *required_size_ptr = required_size;
  }

  return result;
}
DECLARE_XBOXKRNL_EXPORT1(ExGetXConfigSetting, kModules, kImplemented);

dword_result_t ExSetXConfigSetting_entry(word_t category, word_t setting,
                                         lpvoid_t buffer_ptr,
                                         dword_t buffer_size) {
  /* Notes:
      Handles settings the only have a single flag/value like
     XCONFIG_USER_VIDEO_FLAGS to swap
  */
  XELOGI("ExSetXConfigSetting: category: 0X{:04x}, setting: 0X{:04x}",
         static_cast<uint16_t>(category), static_cast<uint16_t>(setting));
  return X_STATUS_SUCCESS;
}
DECLARE_XBOXKRNL_EXPORT1(ExSetXConfigSetting, kModules, kStub);

dword_result_t ExReadModifyWriteXConfigSettingUlong_entry(word_t category,
                                                          word_t setting,
                                                          dword_t bit_affected,
                                                          dword_t flag) {
  /* Notes:
      Handles settings with multiple flags like XCONFIG_USER_RETAIL_FLAGS and
     XCONFIG_CONSOLE_RETAIL_EX_FLAGS
  */
  XELOGI(
      "ExReadModifyWriteXConfigSettingUlong: category: 0x{:04x}, setting: "
      "{:04x}, changed bits: 0X{:08x}, setting flag 0X{:08x}",
      static_cast<uint16_t>(category), static_cast<uint16_t>(setting),
      static_cast<uint32_t>(bit_affected), static_cast<uint32_t>(flag));
  return X_STATUS_SUCCESS;
}
DECLARE_XBOXKRNL_EXPORT1(ExReadModifyWriteXConfigSettingUlong, kModules, kStub);

}  // namespace xboxkrnl
}  // namespace kernel
}  // namespace xe

DECLARE_XBOXKRNL_EMPTY_REGISTER_EXPORTS(XConfig);
