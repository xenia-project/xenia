/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/cvar.h"
#include <iostream>
#define UTF_CPP_CPLUSPLUS 201703L
#include "third_party/utfcpp/source/utf8.h"

#include "xenia/base/console.h"
#include "xenia/base/logging.h"
#include "xenia/base/system.h"
#include "xenia/base/utf8.h"

namespace utfcpp = utf8;

using u8_citer = utfcpp::iterator<std::string_view::const_iterator>;

namespace cvar {

cxxopts::Options options("xenia", "Xbox 360 Emulator");
std::map<std::string, ICommandVar*>* CmdVars;
std::map<std::string, IConfigVar*>* ConfigVars;
std::multimap<uint32_t, const IConfigVarUpdate*>* IConfigVarUpdate::updates_;

void PrintHelpAndExit() {
  std::cout << options.help({""}) << std::endl;
  std::cout << "For the full list of command line arguments, see xenia.cfg."
            << std::endl;
  exit(0);
}

void ParseLaunchArguments(int& argc, char**& argv,
                          const std::string_view positional_help,
                          const std::vector<std::string>& positional_options) {
  options.add_options()("help", "Prints help and exit.");

  if (!CmdVars) {
    CmdVars = new std::map<std::string, ICommandVar*>();
  }

  if (!ConfigVars) {
    ConfigVars = new std::map<std::string, IConfigVar*>();
  }

  for (auto& it : *CmdVars) {
    auto cmdVar = it.second;
    cmdVar->AddToLaunchOptions(&options);
  }

  for (const auto& it : *ConfigVars) {
    auto configVar = it.second;
    configVar->AddToLaunchOptions(&options);
  }

  try {
    options.positional_help(std::string(positional_help));
    options.parse_positional(positional_options);

    auto result = options.parse(argc, argv);
    if (result.count("help")) {
      xe::AttachConsole();
      if (xe::has_console_attached()) {
        PrintHelpAndExit();
      } else {
        xe::ShowSimpleMessageBox(xe::SimpleMessageBoxType::Help,
                                 options.help({""}));
        exit(0);
      }
    }

    for (auto& it : *CmdVars) {
      auto cmdVar = static_cast<ICommandVar*>(it.second);
      if (result.count(cmdVar->name())) {
        cmdVar->LoadFromLaunchOptions(&result);
      }
    }

    for (auto& it : *ConfigVars) {
      auto configVar = static_cast<IConfigVar*>(it.second);
      if (result.count(configVar->name())) {
        configVar->LoadFromLaunchOptions(&result);
      }
    }
  } catch (const cxxopts::exceptions::exception& e) {
    xe::AttachConsole();
    if (xe::has_console_attached()) {
      std::cout << e.what() << std::endl;
      PrintHelpAndExit();
    } else {
      std::string m =
          "Invalid launch options were given.\n" + options.help({""});
      xe::ShowSimpleMessageBox(xe::SimpleMessageBoxType::Error, m);
      exit(0);
    }
  }
}

namespace toml {

std::string EscapeBasicString(const std::string_view view) {
  std::string result;
  auto begin = u8_citer(view.cbegin(), view.cbegin(), view.cend());
  auto end = u8_citer(view.cend(), view.cbegin(), view.cend());
  for (auto it = begin; it != end; ++it) {
    auto c = *it;
    if (c == '\b') {
      result += u8"\\b";
    } else if (c == '\t') {
      result += u8"\\t";
    } else if (c == '\n') {
      result += u8"\\n";
    } else if (c == '\f') {
      result += u8"\\f";
    } else if (c == '\r') {
      result += u8"\\r";
    } else if (c == '"') {
      result += u8"\\\"";
    } else if (c == '\\') {
      result += u8"\\\\";
    } else if (c < 0x20 || c == 0x7F) {
      if (c <= 0xFFFF) {
        result += fmt::format(u8"\\u{:04X}", c);
      } else {
        result += fmt::format(u8"\\u{:08X}", c);
      }
    } else {
      utfcpp::append(static_cast<char32_t>(c), result);
    }
  }
  return result;
}

std::string EscapeMultilineBasicString(const std::string_view view) {
  std::string result;
  int quote_run = 0;
  auto begin = u8_citer(view.cbegin(), view.cbegin(), view.cend());
  auto end = u8_citer(view.cend(), view.cbegin(), view.cend());
  for (auto it = begin; it != end; ++it) {
    auto c = *it;
    if (quote_run > 0) {
      if (c == '"') {
        ++quote_run;
        continue;
      }
      for (int i = 0; i < quote_run; ++i) {
        if ((i % 3) == 2) {
          result += u8"\\";
        }
        result += u8"\"";
      }
      quote_run = 0;
    }
    if (c == '\b') {
      result += u8"\\b";
    } else if (c == '\t' || c == '\n') {
      result += c;
    } else if (c == '\f') {
      result += u8"\\f";
    } else if (c == '\r') {
      // Silently drop \r.
      // result += c;
    } else if (c == '"') {
      quote_run = 1;
    } else if (c == '\\') {
      result += u8"\\\\";
    } else if (c < 0x20 || c == 0x7F) {
      if (c <= 0xFFFF) {
        result += fmt::format(u8"\\u{:04X}", c);
      } else {
        result += fmt::format(u8"\\u{:08X}", c);
      }
    } else {
      utfcpp::append(static_cast<char32_t>(c), result);
    }
  }
  for (int i = 0; i < quote_run; ++i) {
    if ((i % 3) == 2) {
      result += u8"\\";
    }
    result += u8"\"";
  }
  return result;
}

std::string EscapeString(const std::string_view view) {
  const auto multiline_chars = std::string_view("\r\n");
  const auto escape_chars = std::string_view(
      "\0\b\v\f"
      "\x01\x02\x03\x04\x05\x06\x07\x0E\x0F"
      "\x10\x11\x12\x13\x14\x15\x16\x17\x18\x19\x1A\x1B\x1C\x1D\x1E\x1F"
      "'"
      "\x7F");

  if (xe::utf8::find_any_of(view, multiline_chars) == std::string_view::npos) {
    // single line
    if (xe::utf8::find_any_of(view, escape_chars) == std::string_view::npos) {
      return "'" + std::string(view) + "'";
    } else {
      return "\"" + toml::EscapeBasicString(view) + "\"";
    }
  } else {
    // multi line
    if (xe::utf8::find_any_of(view, escape_chars) == std::string_view::npos &&
        xe::utf8::find_first_of(view, u8"'''") == std::string_view::npos) {
      return "'''\n" + std::string(view) + "'''";
    } else {
      return u8"\"\"\"\n" + toml::EscapeMultilineBasicString(view) + u8"\"\"\"";
    }
  }
}

}  // namespace toml

}  // namespace cvar
