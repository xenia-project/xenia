/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <cstring>

#include "xenia/base/logging.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xam/xam_private.h"
#include "xenia/kernel/xenumerator.h"
#include "xenia/kernel/xthread.h"
#include "xenia/xbox.h"

DECLARE_int32(user_country);

// TODO(gibbed): put these forward decls in a header somewhere.

namespace xe {
namespace kernel {
namespace xam {
uint32_t xeXGetGameRegion();
}  // namespace xam
}  // namespace kernel
}  // namespace xe

namespace xe {
namespace kernel {
namespace xam {

// Table lookups.

uint8_t xeXamGetOnlineCountryFromLocale(uint8_t id) {
  static uint8_t const table[] = {
      2,   6,  5,  8,  13,  16, 19,  20, 21,  23, 25, 32, 34, 24, 37,
      39,  42, 46, 44, 50,  53, 56,  71, 74,  76, 75, 82, 84, 91, 93,
      109, 31, 90, 18, 101, 35, 103, 88, 236, 99, 4,  89, 45, 1,
  };
#pragma warning(suppress : 6385)
  return id < xe::countof(table) ? table[id] : 0;
}

const char16_t* xeXamGetOnlineCountryString(uint8_t id) {
  static const char16_t* const table[] = {
      u"ZZ", u"AE",   u"AL", u"AM", u"AR",   u"AT", u"AU",   u"AZ",   u"BE",
      u"BG", u"BH",   u"BN", u"BO", u"BR",   u"BY", u"BZ",   u"CA",   nullptr,
      u"CH", u"CL",   u"CN", u"CO", u"CR",   u"CZ", u"DE",   u"DK",   u"DO",
      u"DZ", u"EC",   u"EE", u"EG", u"ES",   u"FI", u"FO",   u"FR",   u"GB",
      u"GE", u"GR",   u"GT", u"HK", u"HN",   u"HR", u"HU",   u"ID",   u"IE",
      u"IL", u"IN",   u"IQ", u"IR", u"IS",   u"IT", u"JM",   u"JO",   u"JP",
      u"KE", u"KG",   u"KR", u"KW", u"KZ",   u"LB", u"LI",   u"LT",   u"LU",
      u"LV", u"LY",   u"MA", u"MC", u"MK",   u"MN", u"MO",   u"MV",   u"MX",
      u"MY", u"NI",   u"NL", u"NO", u"NZ",   u"OM", u"PA",   u"PE",   u"PH",
      u"PK", u"PL",   u"PR", u"PT", u"PY",   u"QA", u"RO",   u"RU",   u"SA",
      u"SE", u"SG",   u"SI", u"SK", nullptr, u"SV", u"SY",   u"TH",   u"TN",
      u"TR", u"TT",   u"TW", u"UA", u"US",   u"UY", u"UZ",   u"VE",   u"VN",
      u"YE", u"ZA",   u"ZW", u"AF", nullptr, u"AD", u"AO",   u"AI",   nullptr,
      u"AG", u"AW",   u"BS", u"BD", u"BB",   u"BJ", u"BM",   u"BT",   u"BA",
      u"BW", u"BF",   u"BI", u"KH", u"CM",   u"CV", u"KY",   u"CF",   u"TD",
      u"CX", u"CC",   u"KM", u"CG", u"CD",   u"CK", u"CI",   u"CY",   u"DJ",
      u"DM", nullptr, u"GQ", u"ER", u"ET",   u"FK", u"FJ",   u"GF",   u"PF",
      u"GA", u"GM",   u"GH", u"GI", u"GL",   u"GD", u"GP",   nullptr, u"GG",
      u"GN", u"GW",   u"GY", u"HT", u"JE",   u"KI", u"LA",   u"LS",   u"LR",
      u"MG", u"MW",   u"ML", u"MT", u"MH",   u"MQ", u"MR",   u"MU",   u"YT",
      u"FM", u"MD",   u"ME", u"MS", u"MZ",   u"MM", u"NA",   u"NR",   u"NP",
      u"AN", u"NC",   u"NE", u"NG", u"NU",   u"NF", nullptr, u"PW",   u"PS",
      u"PG", u"PN",   u"RE", u"RW", u"WS",   u"SM", u"ST",   u"SN",   u"RS",
      u"SC", u"SL",   u"SB", u"SO", u"LK",   u"SH", u"KN",   u"LC",   u"PM",
      u"VC", u"SR",   u"SZ", u"TJ", u"TZ",   u"TL", u"TG",   u"TK",   u"TO",
      u"TM", u"TC",   u"TV", u"UG", u"VU",   u"VA", nullptr, u"VG",   u"WF",
      u"EH", u"ZM",   u"ZZ",
  };
#pragma warning(suppress : 6385)
  return id < xe::countof(table) ? table[id] : nullptr;
}

const char16_t* xeXamGetCountryString(uint8_t id) {
  static const char16_t* const table[] = {
      u"ZZ", u"AE", u"AL", u"AM", u"AR",   u"AT", u"AU", u"AZ",   u"BE", u"BG",
      u"BH", u"BN", u"BO", u"BR", u"BY",   u"BZ", u"CA", nullptr, u"CH", u"CL",
      u"CN", u"CO", u"CR", u"CZ", u"DE",   u"DK", u"DO", u"DZ",   u"EC", u"EE",
      u"EG", u"ES", u"FI", u"FO", u"FR",   u"GB", u"GE", u"GR",   u"GT", u"HK",
      u"HN", u"HR", u"HU", u"ID", u"IE",   u"IL", u"IN", u"IQ",   u"IR", u"IS",
      u"IT", u"JM", u"JO", u"JP", u"KE",   u"KG", u"KR", u"KW",   u"KZ", u"LB",
      u"LI", u"LT", u"LU", u"LV", u"LY",   u"MA", u"MC", u"MK",   u"MN", u"MO",
      u"MV", u"MX", u"MY", u"NI", u"NL",   u"NO", u"NZ", u"OM",   u"PA", u"PE",
      u"PH", u"PK", u"PL", u"PR", u"PT",   u"PY", u"QA", u"RO",   u"RU", u"SA",
      u"SE", u"SG", u"SI", u"SK", nullptr, u"SV", u"SY", u"TH",   u"TN", u"TR",
      u"TT", u"TW", u"UA", u"US", u"UY",   u"UZ", u"VE", u"VN",   u"YE", u"ZA",
      u"ZW", u"ZZ",
  };
#pragma warning(suppress : 6385)
  return id < xe::countof(table) ? table[id] : nullptr;
}

const char16_t* xeXamGetLanguageString(uint8_t id) {
  static const char16_t* const table[] = {
      u"zz", u"en",   u"ja", u"de", u"fr", u"es", u"it", u"ko", u"zh",
      u"pt", nullptr, u"pl", u"ru", u"sv", u"tr", u"nb", u"nl", u"zh",
  };
#pragma warning(suppress : 6385)
  return id < xe::countof(table) ? table[id] : nullptr;
}

const char16_t* xeXamGetLocaleString(uint8_t id) {
  static const char16_t* const table[] = {
      u"ZZ", u"AU", u"AT", u"BE", u"BR", u"CA", u"CL", u"CN", u"CO",
      u"CZ", u"DK", u"FI", u"FR", u"DE", u"GR", u"HK", u"HU", u"IN",
      u"IE", u"IT", u"JP", u"KR", u"MX", u"NL", u"NZ", u"NO", u"PL",
      u"PT", u"SG", u"SK", u"ZA", u"ES", u"SE", u"CH", u"TW", u"GB",
      u"US", u"RU", u"ZZ", u"TR", u"AR", u"SA", u"IL", u"AE",
  };
#pragma warning(suppress : 6385)
  return id < xe::countof(table) ? table[id] : nullptr;
}

uint8_t xeXamGetLocaleFromOnlineCountry(uint8_t id) {
  static uint8_t const table[] = {
      0,  43, 0, 0, 40, 2,  1,  0,  3,  0, 0, 0, 0,  4,  0,  0,  5,  0,  33,
      6,  7,  8, 0, 9,  13, 10, 0,  0,  0, 0, 0, 31, 11, 0,  12, 35, 0,  14,
      0,  15, 0, 0, 16, 0,  18, 42, 17, 0, 0, 0, 19, 0,  0,  20, 0,  0,  21,
      0,  0,  0, 0, 0,  0,  0,  0,  0,  0, 0, 0, 0,  0,  22, 0,  0,  23, 25,
      24, 0,  0, 0, 0,  0,  26, 0,  27, 0, 0, 0, 37, 41, 32, 28, 0,  29, 0,
      0,  0,  0, 0, 39, 0,  34, 0,  36, 0, 0, 0, 0,  0,  30, 0,  0,  0,  0,
      0,  0,  0, 0, 0,  0,  0,  0,  0,  0, 0, 0, 0,  0,  0,  0,  0,  0,  0,
      0,  0,  0, 0, 0,  0,  0,  0,  0,  0, 0, 0, 0,  0,  0,  0,  0,  0,  0,
      0,  0,  0, 0, 0,  0,  0,  0,  0,  0, 0, 0, 0,  0,  0,  0,  0,  0,  0,
      0,  0,  0, 0, 0,  0,  0,  0,  0,  0, 0, 0, 0,  0,  0,  0,  0,  0,  0,
      0,  0,  0, 0, 0,  0,  0,  0,  0,  0, 0, 0, 0,  0,  0,  0,  0,  0,  0,
      0,  0,  0, 0, 0,  0,  0,  0,  0,  0, 0, 0, 0,  0,  0,  0,  0,  0,  0,
      0,  0,  0, 0, 0,  0,  0,  0,  38,
  };
#pragma warning(suppress : 6385)
  return id < xe::countof(table) ? table[id] : 0;
}

uint8_t xeXamGetLanguageFromOnlineLanguage(uint8_t id) {
  static uint8_t const table[] = {
      0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 17, 11, 12, 1, 1, 15, 16, 13, 1, 1, 1,
      1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  1,  1,  1, 1, 14, 1,  1,  1, 1, 1,
      1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  1,  1,  1, 1, 1,  1,  1,  1,
  };
#pragma warning(suppress : 6385)
  return id < xe::countof(table) ? table[id] : 0;
}

const char16_t* xeXamGetOnlineLanguageString(uint8_t id) {
  static const char16_t* const table[] = {
      u"zz", u"en", u"ja", u"de", u"fr", u"es", u"it", u"ko", u"zh",
      u"pt", u"zh", u"pl", u"ru", u"da", u"fi", u"nb", u"nl", u"sv",
      u"cs", u"el", u"hu", u"sk", u"id", u"ms", u"ar", u"bg", u"et",
      u"hr", u"he", u"is", u"kk", u"lt", u"lv", u"ro", u"sl", u"th",
      u"tr", u"uk", u"vi", u"ps", u"sq", u"hy", u"bn", u"be", u"km",
      u"am", u"fo", u"ka", u"kl", u"sw", u"ky", u"lb", u"mk", u"mt",
      u"mn", u"ne", u"ur", u"rw", u"wo", u"si", u"tk",
  };
#pragma warning(suppress : 6385)
  return id < xe::countof(table) ? table[id] : nullptr;
}

uint8_t xeXamGetCountryFromOnlineCountry(uint8_t id) {
  static uint8_t const table[] = {
      0,  1,  2,  3,  4,   5,   6,   7,   8,   9,   10,  11,  12,  13,  14,  15,
      16, 0,  18, 19, 20,  21,  22,  23,  24,  25,  26,  27,  28,  29,  30,  31,
      32, 33, 34, 35, 36,  37,  38,  39,  40,  41,  42,  43,  44,  45,  46,  47,
      48, 49, 50, 51, 52,  53,  54,  55,  56,  57,  58,  59,  60,  61,  62,  63,
      64, 65, 66, 67, 68,  69,  70,  71,  72,  73,  74,  75,  76,  77,  78,  79,
      80, 81, 82, 83, 84,  85,  86,  87,  88,  89,  90,  91,  92,  93,  0,   95,
      96, 97, 98, 99, 100, 101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 0,
      0,  0,  0,  0,  0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
      0,  0,  0,  0,  0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
      0,  0,  0,  0,  0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
      0,  0,  0,  0,  0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
      0,  0,  0,  0,  0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
      0,  0,  0,  0,  0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
      0,  0,  0,  0,  0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
      0,  0,  0,  0,  0,   0,   0,   0,   0,   0,   0,   0,   111,
  };
#pragma warning(suppress : 6385)
  return id < xe::countof(table) ? table[id] : 0;
}

uint8_t xeXamGetLocaleFromCountry(uint8_t id) {
  static uint8_t const table[] = {
      0,  43, 0, 0, 40, 2,  1,  0,  3,  0, 0, 0, 0,  4,  0,  0,  5,  0,  33,
      6,  7,  8, 0, 9,  13, 10, 0,  0,  0, 0, 0, 31, 11, 0,  12, 35, 0,  14,
      0,  15, 0, 0, 16, 0,  18, 42, 17, 0, 0, 0, 19, 0,  0,  20, 0,  0,  21,
      0,  0,  0, 0, 0,  0,  0,  0,  0,  0, 0, 0, 0,  0,  22, 0,  0,  23, 25,
      24, 0,  0, 0, 0,  0,  26, 0,  27, 0, 0, 0, 37, 41, 32, 28, 0,  29, 0,
      0,  0,  0, 0, 39, 0,  34, 0,  36, 0, 0, 0, 0,  0,  30, 0,  38,
  };
#pragma warning(suppress : 6385)
  return id < xe::countof(table) ? table[id] : 0;
}

// Helpers.

uint8_t xeXamGetLocaleEx(uint8_t max_country_id, uint8_t max_locale_id) {
  // TODO(gibbed): rework when XConfig is cleanly implemented.
  uint8_t country_id = static_cast<uint8_t>(cvars::user_country);
  /*if (XSUCCEEDED(xboxkrnl::xeExGetXConfigSetting(
          3, 14, &country_id, sizeof(country_id), nullptr))) {*/
  if (country_id <= max_country_id) {
    uint8_t locale_id = xeXamGetLocaleFromCountry(country_id);
    if (locale_id <= max_locale_id) {
      return locale_id;
    }
  }
  /*}*/

  // couldn't find locale, fallback from game region.
  auto game_region = xeXGetGameRegion();
  auto region = static_cast<uint8_t>((game_region & 0xFF00u) >> 8);
  if (region == 1) {
    return game_region == 0x101 ? 20 /* JP */ : 21 /* KR */;
  }
  return region == 2 ? 35 /* GB */ : 36 /* US */;
}

uint8_t xeXamGetLocale() { return xeXamGetLocaleEx(111, 43); }

// Exports.

dword_result_t XamGetLocale_entry() {
  return static_cast<uint8_t>(xeXamGetLocale());
}
DECLARE_XAM_EXPORT1(XamGetLocale, kLocale, kImplemented);

dword_result_t XamGetOnlineCountryFromLocale_entry(dword_t id) {
  return xeXamGetOnlineCountryFromLocale(id);
}
DECLARE_XAM_EXPORT1(XamGetOnlineCountryFromLocale, kLocale, kImplemented);

dword_result_t XamGetOnlineCountryString_entry(dword_t id,
                                               dword_t buffer_length,
                                               lpu16string_t buffer) {
  if (buffer_length >= 0x80000000u) {
    return X_E_INVALIDARG;
  }

  auto str = xeXamGetOnlineCountryString(static_cast<uint8_t>(id));
  if (!str) {
    return X_E_NOTFOUND;
  }

  const auto value = std::u16string(str);
  if (value.size() + 1 > buffer_length) {
    return X_HRESULT_FROM_WIN32(X_ERROR_INSUFFICIENT_BUFFER);
  }

  xe::store_and_swap<std::u16string>(buffer, value);
  static_cast<char16_t*>(buffer)[value.size()] = 0;
  return X_E_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamGetOnlineCountryString, kLocale, kImplemented);

dword_result_t XamGetCountryString_entry(dword_t id, dword_t buffer_length,
                                         lpu16string_t buffer) {
  if (buffer_length >= 0x80000000u) {
    return X_E_INVALIDARG;
  }

  auto str = xeXamGetCountryString(static_cast<uint8_t>(id));
  if (!str) {
    return X_E_NOTFOUND;
  }

  const auto value = std::u16string(str);
  if (value.size() + 1 > buffer_length) {
    return X_HRESULT_FROM_WIN32(X_ERROR_INSUFFICIENT_BUFFER);
  }

  xe::store_and_swap<std::u16string>(buffer, value);
  static_cast<char16_t*>(buffer)[value.size()] = 0;
  return X_E_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamGetCountryString, kLocale, kImplemented);

dword_result_t XamGetLanguageString_entry(dword_t id, dword_t buffer_length,
                                          lpu16string_t buffer) {
  if (buffer_length >= 0x80000000u) {
    return X_E_INVALIDARG;
  }

  auto str = xeXamGetLanguageString(static_cast<uint8_t>(id));
  if (!str) {
    return X_E_NOTFOUND;
  }

  const auto value = std::u16string(str);
  if (value.size() + 1 > buffer_length) {
    return X_HRESULT_FROM_WIN32(X_ERROR_INSUFFICIENT_BUFFER);
  }

  xe::store_and_swap<std::u16string>(buffer, value);
  static_cast<char16_t*>(buffer)[value.size()] = 0;
  return X_E_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamGetLanguageString, kLocale, kImplemented);

dword_result_t XamGetLanguageLocaleString_entry(dword_t language_id,
                                                dword_t locale_id,
                                                dword_t buffer_length,
                                                lpu16string_t buffer) {
  if (buffer_length >= 0x80000000u) {
    return X_E_INVALIDARG;
  }

  auto language_str = xeXamGetLanguageString(static_cast<uint8_t>(language_id));
  if (!language_str) {
    return X_E_NOTFOUND;
  }

  auto locale_str = xeXamGetLocaleString(static_cast<uint8_t>(locale_id));
  if (!locale_str) {
    return X_E_NOTFOUND;
  }

  const auto value =
      std::u16string(language_str) + u"-" + std::u16string(locale_str);
  if (value.size() + 1 > buffer_length) {
    return X_HRESULT_FROM_WIN32(X_ERROR_INSUFFICIENT_BUFFER);
  }

  xe::store_and_swap<std::u16string>(buffer, value);
  static_cast<char16_t*>(buffer)[value.size()] = 0;
  return X_E_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamGetLanguageLocaleString, kLocale, kImplemented);

dword_result_t XamGetOnlineLanguageAndCountryString_entry(
    dword_t language_id, dword_t country_id, dword_t buffer_length,
    lpu16string_t buffer) {
  if (buffer_length >= 0x80000000u) {
    return X_E_INVALIDARG;
  }

  auto language_str =
      xeXamGetOnlineLanguageString(static_cast<uint8_t>(language_id));
  if (!language_str) {
    return X_E_NOTFOUND;
  }

  auto country_str =
      xeXamGetOnlineCountryString(static_cast<uint8_t>(country_id));
  if (!country_str) {
    return X_E_NOTFOUND;
  }

  const auto value =
      std::u16string(language_str) + u"-" + std::u16string(country_str);
  if (value.size() + 1 > buffer_length) {
    return X_HRESULT_FROM_WIN32(X_ERROR_INSUFFICIENT_BUFFER);
  }

  xe::store_and_swap<std::u16string>(buffer, value);
  static_cast<char16_t*>(buffer)[value.size()] = 0;
  return X_E_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamGetOnlineLanguageAndCountryString, kLocale,
                    kImplemented);

dword_result_t XamGetLocaleString_entry(dword_t id, dword_t buffer_length,
                                        lpu16string_t buffer) {
  if (buffer_length >= 0x80000000u) {
    return X_E_INVALIDARG;
  }

  auto str = xeXamGetLocaleString(static_cast<uint8_t>(id));
  if (!str) {
    return X_E_NOTFOUND;
  }

  const auto value = std::u16string(str);
  if (value.size() + 1 > buffer_length) {
    return X_HRESULT_FROM_WIN32(X_ERROR_INSUFFICIENT_BUFFER);
  }

  xe::store_and_swap<std::u16string>(buffer, value);
  static_cast<char16_t*>(buffer)[value.size()] = 0;
  return X_E_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamGetLocaleString, kLocale, kImplemented);

dword_result_t XamGetLocaleFromOnlineCountry_entry(dword_t id) {
  return xeXamGetLocaleFromOnlineCountry(static_cast<uint8_t>(id));
}
DECLARE_XAM_EXPORT1(XamGetLocaleFromOnlineCountry, kLocale, kImplemented);

dword_result_t XamGetLanguageFromOnlineLanguage_entry(dword_t id) {
  return xeXamGetLanguageFromOnlineLanguage(static_cast<uint8_t>(id));
}
DECLARE_XAM_EXPORT1(XamGetLanguageFromOnlineLanguage, kLocale, kImplemented);

dword_result_t XamGetOnlineLanguageString_entry(dword_t id,
                                                dword_t buffer_length,
                                                lpu16string_t buffer) {
  if (buffer_length >= 0x80000000u) {
    return X_E_INVALIDARG;
  }

  auto str = xeXamGetOnlineLanguageString(static_cast<uint8_t>(id));
  if (!str) {
    return X_E_NOTFOUND;
  }

  const auto value = std::u16string(str);
  if (value.size() + 1 > buffer_length) {
    return X_HRESULT_FROM_WIN32(X_ERROR_INSUFFICIENT_BUFFER);
  }

  xe::store_and_swap<std::u16string>(buffer, value);
  static_cast<char16_t*>(buffer)[value.size()] = 0;
  return X_E_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamGetOnlineLanguageString, kLocale, kImplemented);

dword_result_t XamGetCountryFromOnlineCountry_entry(dword_t id) {
  return xeXamGetCountryFromOnlineCountry(static_cast<uint8_t>(id));
}
DECLARE_XAM_EXPORT1(XamGetCountryFromOnlineCountry, kLocale, kImplemented);

dword_result_t XamGetLocaleEx_entry(dword_t max_country_id,
                                    dword_t max_locale_id) {
  return xeXamGetLocaleEx(static_cast<uint8_t>(max_country_id),
                          static_cast<uint8_t>(max_locale_id));
}
DECLARE_XAM_EXPORT1(XamGetLocaleEx, kLocale, kImplemented);
//originally a switch table, wrote a script to extract the values for all possible cases

static constexpr uint8_t XamLocaleDateFmtTable[] = {
    2, 1, 3, 1, 3, 3, 3, 3, 3, 3, 3, 2, 3, 2, 1, 4, 2, 3, 1, 2, 2, 3,
    3, 3, 3, 3, 2, 1, 3, 2, 2, 3, 0, 3, 0, 3, 3, 5, 3, 1, 3, 2, 3, 3,
    3, 2, 3, 3, 5, 3, 3, 5, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
    2, 3, 3, 0, 2, 1, 3, 3, 3, 3, 3, 5, 3, 2, 3, 3, 3, 2, 3, 5, 0, 3,
    1, 3, 3, 3, 3, 3, 3, 3, 4, 3, 3, 3, 3, 3, 3, 3, 5, 1, 1, 1, 1};

dword_result_t XamGetLocaleDateFormat_entry(dword_t locale) {
  uint32_t biased_locale = locale - 5;
  int result = 3;
  if (biased_locale > 0x68) {
    return result;
  } else {
    return XamLocaleDateFmtTable[biased_locale];
  }
}

DECLARE_XAM_EXPORT1(XamGetLocaleDateFormat, kLocale, kImplemented);

}  // namespace xam
}  // namespace kernel
}  // namespace xe

DECLARE_XAM_EMPTY_REGISTER_EXPORTS(Locale);
