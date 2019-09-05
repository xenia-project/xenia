/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2019 Ben Vanik. All rights reserved.                             *
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

const wchar_t* xeXamGetOnlineCountryString(uint8_t id) {
  static const wchar_t* const table[] = {
      L"ZZ", L"AE",   L"AL", L"AM", L"AR",   L"AT", L"AU",   L"AZ",   L"BE",
      L"BG", L"BH",   L"BN", L"BO", L"BR",   L"BY", L"BZ",   L"CA",   nullptr,
      L"CH", L"CL",   L"CN", L"CO", L"CR",   L"CZ", L"DE",   L"DK",   L"DO",
      L"DZ", L"EC",   L"EE", L"EG", L"ES",   L"FI", L"FO",   L"FR",   L"GB",
      L"GE", L"GR",   L"GT", L"HK", L"HN",   L"HR", L"HU",   L"ID",   L"IE",
      L"IL", L"IN",   L"IQ", L"IR", L"IS",   L"IT", L"JM",   L"JO",   L"JP",
      L"KE", L"KG",   L"KR", L"KW", L"KZ",   L"LB", L"LI",   L"LT",   L"LU",
      L"LV", L"LY",   L"MA", L"MC", L"MK",   L"MN", L"MO",   L"MV",   L"MX",
      L"MY", L"NI",   L"NL", L"NO", L"NZ",   L"OM", L"PA",   L"PE",   L"PH",
      L"PK", L"PL",   L"PR", L"PT", L"PY",   L"QA", L"RO",   L"RU",   L"SA",
      L"SE", L"SG",   L"SI", L"SK", nullptr, L"SV", L"SY",   L"TH",   L"TN",
      L"TR", L"TT",   L"TW", L"UA", L"US",   L"UY", L"UZ",   L"VE",   L"VN",
      L"YE", L"ZA",   L"ZW", L"AF", nullptr, L"AD", L"AO",   L"AI",   nullptr,
      L"AG", L"AW",   L"BS", L"BD", L"BB",   L"BJ", L"BM",   L"BT",   L"BA",
      L"BW", L"BF",   L"BI", L"KH", L"CM",   L"CV", L"KY",   L"CF",   L"TD",
      L"CX", L"CC",   L"KM", L"CG", L"CD",   L"CK", L"CI",   L"CY",   L"DJ",
      L"DM", nullptr, L"GQ", L"ER", L"ET",   L"FK", L"FJ",   L"GF",   L"PF",
      L"GA", L"GM",   L"GH", L"GI", L"GL",   L"GD", L"GP",   nullptr, L"GG",
      L"GN", L"GW",   L"GY", L"HT", L"JE",   L"KI", L"LA",   L"LS",   L"LR",
      L"MG", L"MW",   L"ML", L"MT", L"MH",   L"MQ", L"MR",   L"MU",   L"YT",
      L"FM", L"MD",   L"ME", L"MS", L"MZ",   L"MM", L"NA",   L"NR",   L"NP",
      L"AN", L"NC",   L"NE", L"NG", L"NU",   L"NF", nullptr, L"PW",   L"PS",
      L"PG", L"PN",   L"RE", L"RW", L"WS",   L"SM", L"ST",   L"SN",   L"RS",
      L"SC", L"SL",   L"SB", L"SO", L"LK",   L"SH", L"KN",   L"LC",   L"PM",
      L"VC", L"SR",   L"SZ", L"TJ", L"TZ",   L"TL", L"TG",   L"TK",   L"TO",
      L"TM", L"TC",   L"TV", L"UG", L"VU",   L"VA", nullptr, L"VG",   L"WF",
      L"EH", L"ZM",   L"ZZ",
  };
#pragma warning(suppress : 6385)
  return id < xe::countof(table) ? table[id] : nullptr;
}

const wchar_t* xeXamGetCountryString(uint8_t id) {
  static const wchar_t* const table[] = {
      L"ZZ", L"AE", L"AL", L"AM", L"AR",   L"AT", L"AU", L"AZ",   L"BE", L"BG",
      L"BH", L"BN", L"BO", L"BR", L"BY",   L"BZ", L"CA", nullptr, L"CH", L"CL",
      L"CN", L"CO", L"CR", L"CZ", L"DE",   L"DK", L"DO", L"DZ",   L"EC", L"EE",
      L"EG", L"ES", L"FI", L"FO", L"FR",   L"GB", L"GE", L"GR",   L"GT", L"HK",
      L"HN", L"HR", L"HU", L"ID", L"IE",   L"IL", L"IN", L"IQ",   L"IR", L"IS",
      L"IT", L"JM", L"JO", L"JP", L"KE",   L"KG", L"KR", L"KW",   L"KZ", L"LB",
      L"LI", L"LT", L"LU", L"LV", L"LY",   L"MA", L"MC", L"MK",   L"MN", L"MO",
      L"MV", L"MX", L"MY", L"NI", L"NL",   L"NO", L"NZ", L"OM",   L"PA", L"PE",
      L"PH", L"PK", L"PL", L"PR", L"PT",   L"PY", L"QA", L"RO",   L"RU", L"SA",
      L"SE", L"SG", L"SI", L"SK", nullptr, L"SV", L"SY", L"TH",   L"TN", L"TR",
      L"TT", L"TW", L"UA", L"US", L"UY",   L"UZ", L"VE", L"VN",   L"YE", L"ZA",
      L"ZW", L"ZZ",
  };
#pragma warning(suppress : 6385)
  return id < xe::countof(table) ? table[id] : nullptr;
}

const wchar_t* xeXamGetLanguageString(uint8_t id) {
  static const wchar_t* const table[] = {
      L"zz", L"en",   L"ja", L"de", L"fr", L"es", L"it", L"ko", L"zh",
      L"pt", nullptr, L"pl", L"ru", L"sv", L"tr", L"nb", L"nl", L"zh",
  };
#pragma warning(suppress : 6385)
  return id < xe::countof(table) ? table[id] : nullptr;
}

const wchar_t* xeXamGetLocaleString(uint8_t id) {
  static const wchar_t* const table[] = {
      L"ZZ", L"AU", L"AT", L"BE", L"BR", L"CA", L"CL", L"CN", L"CO",
      L"CZ", L"DK", L"FI", L"FR", L"DE", L"GR", L"HK", L"HU", L"IN",
      L"IE", L"IT", L"JP", L"KR", L"MX", L"NL", L"NZ", L"NO", L"PL",
      L"PT", L"SG", L"SK", L"ZA", L"ES", L"SE", L"CH", L"TW", L"GB",
      L"US", L"RU", L"ZZ", L"TR", L"AR", L"SA", L"IL", L"AE",
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

const wchar_t* xeXamGetOnlineLanguageString(uint8_t id) {
  static const wchar_t* const table[] = {
      L"zz", L"en", L"ja", L"de", L"fr", L"es", L"it", L"ko", L"zh",
      L"pt", L"zh", L"pl", L"ru", L"da", L"fi", L"nb", L"nl", L"sv",
      L"cs", L"el", L"hu", L"sk", L"id", L"ms", L"ar", L"bg", L"et",
      L"hr", L"he", L"is", L"kk", L"lt", L"lv", L"ro", L"sl", L"th",
      L"tr", L"uk", L"vi", L"ps", L"sq", L"hy", L"bn", L"be", L"km",
      L"am", L"fo", L"ka", L"kl", L"sw", L"ky", L"lb", L"mk", L"mt",
      L"mn", L"ne", L"ur", L"rw", L"wo", L"si", L"tk",
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

dword_result_t XamGetLocale() { return xeXamGetLocale(); }
DECLARE_XAM_EXPORT1(XamGetLocale, kLocale, kImplemented);

dword_result_t XamGetOnlineCountryFromLocale(dword_t id) {
  return xeXamGetOnlineCountryFromLocale(id);
}
DECLARE_XAM_EXPORT1(XamGetOnlineCountryFromLocale, kLocale, kImplemented);

dword_result_t XamGetOnlineCountryString(dword_t id, dword_t buffer_length,
                                         lpwstring_t buffer) {
  if (buffer_length >= 0x80000000u) {
    return X_E_INVALIDARG;
  }

  auto str = xeXamGetOnlineCountryString(static_cast<uint8_t>(id));
  if (!str) {
    return X_E_NOTFOUND;
  }

  const auto value = std::wstring(str);
  if (value.size() + 1 > buffer_length) {
    return X_HRESULT_FROM_WIN32(X_ERROR_INSUFFICIENT_BUFFER);
  }

  xe::store_and_swap<std::wstring>(buffer, value);
  static_cast<wchar_t*>(buffer)[value.size()] = 0;
  return X_E_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamGetOnlineCountryString, kLocale, kImplemented);

dword_result_t XamGetCountryString(dword_t id, dword_t buffer_length,
                                   lpwstring_t buffer) {
  if (buffer_length >= 0x80000000u) {
    return X_E_INVALIDARG;
  }

  auto str = xeXamGetCountryString(static_cast<uint8_t>(id));
  if (!str) {
    return X_E_NOTFOUND;
  }

  const auto value = std::wstring(str);
  if (value.size() + 1 > buffer_length) {
    return X_HRESULT_FROM_WIN32(X_ERROR_INSUFFICIENT_BUFFER);
  }

  xe::store_and_swap<std::wstring>(buffer, value);
  static_cast<wchar_t*>(buffer)[value.size()] = 0;
  return X_E_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamGetCountryString, kLocale, kImplemented);

dword_result_t XamGetLanguageString(dword_t id, dword_t buffer_length,
                                    lpwstring_t buffer) {
  if (buffer_length >= 0x80000000u) {
    return X_E_INVALIDARG;
  }

  auto str = xeXamGetLanguageString(static_cast<uint8_t>(id));
  if (!str) {
    return X_E_NOTFOUND;
  }

  const auto value = std::wstring(str);
  if (value.size() + 1 > buffer_length) {
    return X_HRESULT_FROM_WIN32(X_ERROR_INSUFFICIENT_BUFFER);
  }

  xe::store_and_swap<std::wstring>(buffer, value);
  static_cast<wchar_t*>(buffer)[value.size()] = 0;
  return X_E_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamGetLanguageString, kLocale, kImplemented);

dword_result_t XamGetLanguageLocaleString(dword_t language_id,
                                          dword_t locale_id,
                                          dword_t buffer_length,
                                          lpwstring_t buffer) {
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
      std::wstring(language_str) + L"-" + std::wstring(locale_str);
  if (value.size() + 1 > buffer_length) {
    return X_HRESULT_FROM_WIN32(X_ERROR_INSUFFICIENT_BUFFER);
  }

  xe::store_and_swap<std::wstring>(buffer, value);
  static_cast<wchar_t*>(buffer)[value.size()] = 0;
  return X_E_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamGetLanguageLocaleString, kLocale, kImplemented);

dword_result_t XamGetOnlineLanguageAndCountryString(dword_t language_id,
                                                    dword_t country_id,
                                                    dword_t buffer_length,
                                                    lpwstring_t buffer) {
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
      std::wstring(language_str) + L"-" + std::wstring(country_str);
  if (value.size() + 1 > buffer_length) {
    return X_HRESULT_FROM_WIN32(X_ERROR_INSUFFICIENT_BUFFER);
  }

  xe::store_and_swap<std::wstring>(buffer, value);
  static_cast<wchar_t*>(buffer)[value.size()] = 0;
  return X_E_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamGetOnlineLanguageAndCountryString, kLocale,
                    kImplemented);

dword_result_t XamGetLocaleString(dword_t id, dword_t buffer_length,
                                  lpwstring_t buffer) {
  if (buffer_length >= 0x80000000u) {
    return X_E_INVALIDARG;
  }

  auto str = xeXamGetLocaleString(static_cast<uint8_t>(id));
  if (!str) {
    return X_E_NOTFOUND;
  }

  const auto value = std::wstring(str);
  if (value.size() + 1 > buffer_length) {
    return X_HRESULT_FROM_WIN32(X_ERROR_INSUFFICIENT_BUFFER);
  }

  xe::store_and_swap<std::wstring>(buffer, value);
  static_cast<wchar_t*>(buffer)[value.size()] = 0;
  return X_E_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamGetLocaleString, kLocale, kImplemented);

dword_result_t XamGetLocaleFromOnlineCountry(dword_t id) {
  return xeXamGetLocaleFromOnlineCountry(static_cast<uint8_t>(id));
}
DECLARE_XAM_EXPORT1(XamGetLocaleFromOnlineCountry, kLocale, kImplemented);

dword_result_t XamGetLanguageFromOnlineLanguage(dword_t id) {
  return xeXamGetLanguageFromOnlineLanguage(static_cast<uint8_t>(id));
}
DECLARE_XAM_EXPORT1(XamGetLanguageFromOnlineLanguage, kLocale, kImplemented);

dword_result_t XamGetOnlineLanguageString(dword_t id, dword_t buffer_length,
                                          lpwstring_t buffer) {
  if (buffer_length >= 0x80000000u) {
    return X_E_INVALIDARG;
  }

  auto str = xeXamGetOnlineLanguageString(static_cast<uint8_t>(id));
  if (!str) {
    return X_E_NOTFOUND;
  }

  const auto value = std::wstring(str);
  if (value.size() + 1 > buffer_length) {
    return X_HRESULT_FROM_WIN32(X_ERROR_INSUFFICIENT_BUFFER);
  }

  xe::store_and_swap<std::wstring>(buffer, value);
  static_cast<wchar_t*>(buffer)[value.size()] = 0;
  return X_E_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamGetOnlineLanguageString, kLocale, kImplemented);

dword_result_t XamGetCountryFromOnlineCountry(dword_t id) {
  return xeXamGetCountryFromOnlineCountry(static_cast<uint8_t>(id));
}
DECLARE_XAM_EXPORT1(XamGetCountryFromOnlineCountry, kLocale, kImplemented);

dword_result_t XamGetLocaleEx(dword_t max_country_id, dword_t max_locale_id) {
  return xeXamGetLocaleEx(static_cast<uint8_t>(max_country_id),
                          static_cast<uint8_t>(max_locale_id));
}
DECLARE_XAM_EXPORT1(XamGetLocaleEx, kLocale, kImplemented);

}  // namespace xam
}  // namespace kernel
}  // namespace xe

void xe::kernel::xam::RegisterLocaleExports(
    xe::cpu::ExportResolver* export_resolver, KernelState* kernel_state) {}
