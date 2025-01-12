/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/logging.h"
#include "xenia/cpu/processor.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/user_module.h"
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

namespace xe {
namespace kernel {
namespace xboxkrnl {

X_STATUS xeExGetXConfigSetting(uint16_t category, uint16_t setting,
                               void* buffer, uint16_t buffer_size,
                               uint16_t* required_size) {
  uint16_t setting_size = 0;
  alignas(uint32_t) uint8_t value[4];

  // TODO(benvanik): have real structs here that just get copied from.
  // https://free60project.github.io/wiki/XConfig.html
  // https://github.com/oukiar/freestyledash/blob/master/Freestyle/Tools/Generic/ExConfig.h
  switch (category) {
    case 0x0002:
      // XCONFIG_SECURED_CATEGORY
      switch (setting) {
        case 0x0002:  // XCONFIG_SECURED_AV_REGION
          setting_size = 4;
          switch (cvars::video_standard) {
            case 1:  // NTSCM
              xe::store_and_swap<uint32_t>(value, 0x00400100);
              break;
            case 2:  // NTSCJ
              xe::store_and_swap<uint32_t>(value, 0x00400200);
              break;
            case 3:  // PAL
              xe::store_and_swap<uint32_t>(
                  value, cvars::use_50Hz_mode ? 0x00800300 : 0x00400400);
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
    case 0x0003:
      // XCONFIG_USER_CATEGORY
      switch (setting) {
        case 0x0001:  // XCONFIG_USER_TIME_ZONE_BIAS
        case 0x0002:  // XCONFIG_USER_TIME_ZONE_STD_NAME
        case 0x0003:  // XCONFIG_USER_TIME_ZONE_DLT_NAME
        case 0x0004:  // XCONFIG_USER_TIME_ZONE_STD_DATE
        case 0x0005:  // XCONFIG_USER_TIME_ZONE_DLT_DATE
        case 0x0006:  // XCONFIG_USER_TIME_ZONE_STD_BIAS
        case 0x0007:  // XCONFIG_USER_TIME_ZONE_DLT_BIAS
          setting_size = 4;
          // TODO(benvanik): get this value.
          xe::store_and_swap<uint32_t>(value, 0);
          break;
        case 0x0009:  // XCONFIG_USER_LANGUAGE
          setting_size = 4;
          xe::store_and_swap<uint32_t>(value, cvars::user_language);
          break;
        case 0x000A:  // XCONFIG_USER_VIDEO_FLAGS
          setting_size = 4;
          // 0x00040000 normal
          // 0x00050000 widescreen
          xe::store_and_swap<uint32_t>(
              value, cvars::widescreen ? 0x00050000 : 0x00040000);
          break;
        case 0x000B:  // XCONFIG_USER_AUDIO_FLAGS
          setting_size = 4;
          xe::store_and_swap<uint32_t>(value, cvars::audio_flag);
          break;
        case 0x000C:  // XCONFIG_USER_RETAIL_FLAGS
          setting_size = 4;
          // TODO(benvanik): get this value.
          xe::store_and_swap<uint32_t>(value, 0);
          break;
        case 0x000E:  // XCONFIG_USER_COUNTRY
          setting_size = 1;
          value[0] = static_cast<uint8_t>(cvars::user_country);
          break;
        default:
          XELOGW("An unimplemented setting 0x{:04X} in XCONFIG USER CATEGORY",
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
  X_STATUS result = xeExGetXConfigSetting(category, setting, buffer_ptr,
                                          buffer_size, &required_size);

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
