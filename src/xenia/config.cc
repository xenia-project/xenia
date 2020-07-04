/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "config.h"

#include "third_party/cpptoml/include/cpptoml.h"
#include "third_party/fmt/include/fmt/format.h"
#include "xenia/base/cvar.h"
#include "xenia/base/filesystem.h"
#include "xenia/base/logging.h"
#include "xenia/base/string.h"
#include "xenia/base/string_buffer.h"

CmdVar(config, "", "Specifies the target config to load.");

std::shared_ptr<cpptoml::table> ParseFile(
    const std::filesystem::path& filename) {
  std::ifstream file(filename);
  if (!file.is_open()) {
    throw cpptoml::parse_exception(xe::path_to_utf8(filename) +
                                   " could not be opened for parsing");
  }
  cpptoml::parser p(file);
  return p.parse();
}

namespace xe {

Config& Config::Instance() {
  static Config config;
  return config;
}

Config::Config() : options_("xenia", "Xbox 30 Emulator") {
  
}

std::shared_ptr<cpptoml::table> ParseConfig(
    const std::filesystem::path& config_path) {
  try {
    return ParseFile(config_path);
  } catch (const cpptoml::parse_exception& e) {
    xe::FatalError(fmt::format("Failed to parse config file '{}':\n\n{}",
                               xe::path_to_utf8(config_path), e.what()));
    return nullptr;
  }
}

void Config::SetupConfig(const std::filesystem::path& config_folder) {
  Config::config_folder_ = config_folder;
  // check if the user specified a specific config to load
  if (!cvars::config.empty()) {
    config_path_ = xe::to_path(cvars::config);
    if (std::filesystem::exists(config_path_)) {
      ReadConfig(config_path_);
      return;
    }
  }
  // if the user specified a --config argument, but the file doesn't exist,
  // let's also load the default config
  if (!config_folder.empty()) {
    config_path_ = config_folder / config_name_;
    if (std::filesystem::exists(config_path_)) {
      ReadConfig(config_path_);
    }
    // we only want to save the config if the user is using the default
    // config, we don't want to override a user created specific config
    SaveConfig();
  }
}

SaveStatus Config::SaveConfig() {
  std::vector<cvar::IConfigVar*> vars;

  SaveStatus status = SaveStatus::Saved;

  for (const auto& s : config_vars_) {
    vars.push_back(s.second.get());
  }
  std::sort(vars.begin(), vars.end(), [](auto a, auto b)
  {
    if (a->category() < b->category()) return true;
    if (a->category() > b->category()) return false;
    if (a->name() < b->name()) return true;
    return false;
  });

  // we use our own write logic because cpptoml doesn't
  // allow us to specify comments :(
  std::string last_category;
  bool last_multiline_description = false;
  xe::StringBuffer sb;
  for (const cvar::IConfigVar* config_var : vars) {
    if (config_var->is_transient()) {
      continue;
    }

    if (config_var->requires_restart()) {
      status = SaveStatus::RequiresRestart;
    }

    if (last_category != config_var->category()) {
      if (!last_category.empty()) {
        sb.Append('\n', 2);
      }
      last_category = config_var->category();
      last_multiline_description = false;
      sb.AppendFormat("[{}]\n", last_category);
    } else if (last_multiline_description) {
      last_multiline_description = false;
      sb.Append('\n');
    }

    auto value = config_var->config_value();
    size_t line_count;
    if (xe::utf8::find_any_of(value, "\n") == std::string_view::npos) {
      auto line = fmt::format("{} = {}", config_var->name(),
                              config_var->config_value());
      sb.Append(line);
      line_count = xe::utf8::count(line);
    } else {
      auto lines = xe::utf8::split(value, "\n");
      auto first = lines.cbegin();
      sb.AppendFormat("{} = {}\n", config_var->name(), *first);
      auto last = std::prev(lines.cend());
      for (auto it = std::next(first); it != last; ++it) {
        sb.Append(*it);
        sb.Append('\n');
      }
      sb.Append(*last);
      line_count = xe::utf8::count(*last);
    }

    const size_t value_alignment = 50;
    const auto& description = config_var->description();
    if (!description.empty()) {
      if (line_count < value_alignment) {
        sb.Append(' ', value_alignment - line_count);
      }
      if (xe::utf8::find_any_of(description, "\n") == std::string_view::npos) {
        sb.AppendFormat("\t# {}\n", config_var->description());
      } else {
        auto lines = xe::utf8::split(description, "\n");
        auto first = lines.cbegin();
        sb.Append("\t# ");
        sb.Append(*first);
        sb.Append('\n');
        for (auto it = std::next(first); it != lines.cend(); ++it) {
          sb.Append(' ', value_alignment);
          sb.Append("\t# ");
          sb.Append(*it);
          sb.Append('\n');
        }
        last_multiline_description = true;
      }
    }
  }

  // save the config file
  xe::filesystem::CreateParentFolder(config_path_);

  auto handle = xe::filesystem::OpenFile(config_path_, "wb");
  if (!handle) {
    XELOGE("Failed to open '{}' for writing.", xe::path_to_utf8(config_path_));
    return SaveStatus::CouldNotSave;
  }
  fwrite(sb.buffer(), 1, sb.length(), handle);
  fclose(handle);

  return status;
}

void Config::LoadGameConfig(const std::string_view title_id) {
  const auto game_config_folder = Config::config_folder_ / "config";
  const auto game_config_path =
      game_config_folder / (std::string(title_id) + Config::game_config_suffix_);
  if (std::filesystem::exists(game_config_path)) {
    ReadGameConfig(game_config_path);
  }
}

cvar::IConfigVar* Config::FindConfigVar(const std::string& name) {
  const auto it = config_vars_.find(name);
  if (it != config_vars_.end()) {
    return it->second.get();    
  }
  return nullptr;
}

cvar::ICommandVar* Config::FindCommandVar(const std::string& name) {
  const auto it = command_vars_.find(name);
  if (it != command_vars_.end()) {
    return it->second.get();
  }
  return nullptr;
}

void Config::ParseLaunchArguments(int& argc, char**& argv,
                          const std::string_view positional_help,
                          const std::vector<std::string>& positional_options) {
  options_.add_options()("help", "Prints help and exit.");

  for (auto& it : command_vars_) {
    const auto& cmdVar = it.second;
    cmdVar->AddToLaunchOptions(&options_);
  }

  for (const auto& it : config_vars_) {
    const auto& configVar = it.second;
    configVar->AddToLaunchOptions(&options_);
  }

  try {
    options_.positional_help(std::string(positional_help));
    options_.parse_positional(positional_options);

    auto result = options_.parse(argc, argv);
    if (result.count("help")) {
      PrintHelpAndExit();
    }

    for (auto& it : command_vars_) {
      const auto& cmdVar = it.second;
      if (result.count(cmdVar->name())) {
        cmdVar->LoadFromLaunchOptions(&result);
      }
    }

    for (auto& it : config_vars_) {
      const auto& configVar = it.second;
      if (result.count(configVar->name())) {
        configVar->LoadFromLaunchOptions(&result);
      }
    }
  } catch (const cxxopts::OptionException& e) {
    std::cout << e.what() << std::endl;
    PrintHelpAndExit();
  }
}

void Config::ReadConfig(const std::filesystem::path& file_path) {
  const auto config = ParseConfig(file_path);

  for (auto& it : config_vars_) {
    const auto& config_var = it.second;
    auto config_key = config_var->category() + "." + config_var->name();

    if (config->contains_qualified(config_key)) {
      config_var->LoadConfigValue(config->get_qualified(config_key));
    }
  }
  XELOGI("Loaded config: {}", xe::path_to_utf8(file_path));
}

void Config::ReadGameConfig(const std::filesystem::path& file_path) {
  const auto config = ParseConfig(file_path);

  for (auto& it : config_vars_) {
    const auto& config_var = it.second;
    auto config_key = config_var->category() + "." + config_var->name();

    if (config->contains_qualified(config_key)) {
      config_var->LoadGameConfigValue(config->get_qualified(config_key));
    }
  }
  XELOGI("Loaded game config: {}", xe::path_to_utf8(file_path));
}

void Config::PrintHelpAndExit() {
  std::cout << options_.help({""}) << std::endl;
  std::cout << "For the full list of command line arguments, see xenia.cfg."
            << std::endl;
  exit(0);
}

} // namespace xe
