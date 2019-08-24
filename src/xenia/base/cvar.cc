/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2019 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "cvar.h"

namespace cvar {

cxxopts::Options options("xenia", "Xbox 360 Emulator");
std::map<std::string, ICommandVar*>* CmdVars;
std::map<std::string, IConfigVar*>* ConfigVars;

void PrintHelpAndExit() {
  std::cout << options.help({""}) << std::endl;
  std::cout << "For the full list of command line arguments, see xenia.cfg."
            << std::endl;
  exit(0);
}

void ParseLaunchArguments(int argc, char** argv,
                          const std::string& positional_help,
                          const std::vector<std::string>& positional_options) {
  options.add_options()("help", "Prints help and exit.");
  if (!CmdVars) CmdVars = new std::map<std::string, ICommandVar*>();
  if (!ConfigVars) ConfigVars = new std::map<std::string, IConfigVar*>();
  for (auto& it : *CmdVars) {
    auto cmdVar = it.second;
    cmdVar->AddToLaunchOptions(&options);
  }
  std::vector<IConfigVar*> vars;
  for (const auto& s : *ConfigVars) vars.push_back(s.second);

  for (auto& it : *ConfigVars) {
    auto configVar = it.second;
    configVar->AddToLaunchOptions(&options);
  }
  try {
    options.positional_help(positional_help);
    options.parse_positional(positional_options);

    auto result = options.parse(argc, argv);
    if (result.count("help")) {
      PrintHelpAndExit();
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
  } catch (const cxxopts::OptionException& e) {
    std::cout << e.what() << std::endl;
    PrintHelpAndExit();
  }
}

namespace toml {

std::string EscapeBasicString(const std::string& str) {
  std::string result;
  for (auto c : str) {
    if (c == '\b') {
      result += "\\b";
    } else if (c == '\t') {
      result += "\\t";
    } else if (c == '\n') {
      result += "\\n";
    } else if (c == '\f') {
      result += "\\f";
    } else if (c == '\r') {
      result += "\\r";
    } else if (c == '"') {
      result += "\\\"";
    } else if (c == '\\') {
      result += "\\\\";
    } else if (static_cast<uint32_t>(c) < 0x20 ||
               static_cast<uint32_t>(c) == 0x7F) {
      auto v = static_cast<uint32_t>(c);
      int w;
      if (v <= 0xFFFF) {
        result += "\\u";
        w = 4;
      } else {
        result += "\\U";
        w = 8;
      }
      std::stringstream ss;
      ss << std::hex << std::setw(w) << std::setfill('0') << v;
      result += ss.str();
    } else {
      result += c;
    }
  }
  return result;
}

std::string EscapeMultilineBasicString(const std::string& str) {
  std::string result;
  int quote_run = 0;
  for (char c : str) {
    if (quote_run > 0) {
      if (c == '"') {
        ++quote_run;
        continue;
      }
      for (int i = 0; i < quote_run; ++i) {
        if ((i % 3) == 2) {
          result += "\\";
        }
        result += '"';
      }
      quote_run = 0;
    }
    if (c == '\b') {
      result += "\\b";
    } else if (c == '\t' || c == '\n') {
      result += c;
    } else if (c == '\f') {
      result += "\\f";
    } else if (c == '\r') {
      // Silently drop \r.
      // result += c;
    } else if (c == '"') {
      quote_run = 1;
    } else if (c == '\\') {
      result += "\\\\";
    } else if (static_cast<uint32_t>(c) < 0x20 ||
               static_cast<uint32_t>(c) == 0x7F) {
      auto v = static_cast<uint32_t>(c);
      int w;
      if (v <= 0xFFFF) {
        result += "\\u";
        w = 4;
      } else {
        result += "\\U";
        w = 8;
      }
      std::stringstream ss;
      ss << std::hex << std::setw(w) << std::setfill('0') << v;
      result += ss.str();
    } else {
      result += c;
    }
  }
  for (int i = 0; i < quote_run; ++i) {
    if ((i % 3) == 2) {
      result += "\\";
    }
    result += '"';
  }
  return result;
}

std::string EscapeString(const std::string& val) {
  const char multiline_chars[] = "\r\n";
  const char escape_chars[] =
      "\0\b\v\f"
      "\x01\x02\x03\x04\x05\x06\x07\x0E\x0F"
      "\x10\x11\x12\x13\x14\x15\x16\x17\x18\x19\x1A\x1B\x1C\x1D\x1E\x1F"
      "'"
      "\x7F";
  if (val.find_first_of(multiline_chars) == std::string::npos) {
    // single line
    if (val.find_first_of(escape_chars) == std::string::npos) {
      return "'" + val + "'";
    } else {
      return "\"" + toml::EscapeBasicString(val) + "\"";
    }
  } else {
    // multi line
    if (val.find_first_of(escape_chars) == std::string::npos &&
        val.find("'''") == std::string::npos) {
      return "'''\n" + val + "'''";
    } else {
      return "\"\"\"\n" + toml::EscapeMultilineBasicString(val) + "\"\"\"";
    }
  }
}

}  // namespace toml

}  // namespace cvar
