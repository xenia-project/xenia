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

CmdVar(config, "", "Specifies the target config to load.");
namespace config {
std::string config_name = "xenia.config.toml";
std::filesystem::path config_folder;
std::filesystem::path config_path;
std::string game_config_suffix = ".config.toml";

bool sortCvar(cvar::IConfigVar* a, cvar::IConfigVar* b) {
  if (a->category() < b->category()) return true;
  if (a->category() > b->category()) return false;
  if (a->name() < b->name()) return true;
  return false;
}

std::shared_ptr<cpptoml::table> ParseConfig(
    const std::filesystem::path& config_path) {
  try {
    return ParseFile(config_path);
  } catch (cpptoml::parse_exception e) {
    xe::FatalError(fmt::format("Failed to parse config file '{}':\n\n{}",
                               xe::path_to_utf8(config_path), e.what()));
    return nullptr;
  }
}

void ReadConfig(const std::filesystem::path& file_path) {
  const auto config = ParseConfig(file_path);
  for (auto& it : *cvar::ConfigVars) {
    auto config_var = static_cast<cvar::IConfigVar*>(it.second);
    auto config_key = config_var->category() + "." + config_var->name();
    if (config->contains_qualified(config_key)) {
      config_var->LoadConfigValue(config->get_qualified(config_key));
    }
  }
  XELOGI("Loaded config: {}", xe::path_to_utf8(file_path));
}

void ReadGameConfig(const std::filesystem::path& file_path) {
  const auto config = ParseConfig(file_path);
  for (auto& it : *cvar::ConfigVars) {
    auto config_var = static_cast<cvar::IConfigVar*>(it.second);
    auto config_key = config_var->category() + "." + config_var->name();
    if (config->contains_qualified(config_key)) {
      config_var->LoadGameConfigValue(config->get_qualified(config_key));
    }
  }
  XELOGI("Loaded game config: {}", xe::path_to_utf8(file_path));
}

void SaveConfig() {
  std::vector<cvar::IConfigVar*> vars;
  for (const auto& s : *cvar::ConfigVars) vars.push_back(s.second);
  std::sort(vars.begin(), vars.end(), sortCvar);
  // we use our own write logic because cpptoml doesn't
  // allow us to specify comments :(
  std::ostringstream output;
  std::string last_category;
  for (auto config_var : vars) {
    if (config_var->is_transient()) {
      continue;
    }
    if (last_category != config_var->category()) {
      if (!last_category.empty()) {
        output << std::endl;
      }
      last_category = config_var->category();
      output << fmt::format("[{}]\n", last_category);
    }

    auto value = config_var->config_value();
    if (xe::utf8::find_any_of(value, "\n") == std::string_view::npos) {
      output << std::left << std::setw(40) << std::setfill(' ')
             << fmt::format("{} = {}", config_var->name(),
                            config_var->config_value());
    } else {
      auto lines = xe::utf8::split(value, "\n");
      auto first_it = lines.cbegin();
      output << fmt::format("{} = {}\n", config_var->name(), *first_it);
      auto last_it = std::prev(lines.cend());
      for (auto it = std::next(first_it); it != last_it; ++it) {
        output << *it << "\n";
      }
      output << std::left << std::setw(40) << std::setfill(' ') << *last_it;
    }
    output << fmt::format("\t# {}\n", config_var->description());
  }

  if (std::filesystem::exists(config_path)) {
    std::ifstream existingConfigStream(config_path);
    const std::string existingConfig(
        (std::istreambuf_iterator<char>(existingConfigStream)),
        std::istreambuf_iterator<char>());
    // if the config didn't change, no need to modify the file
    if (existingConfig == output.str()) return;
  }

  // save the config file
  xe::filesystem::CreateParentFolder(config_path);
  std::ofstream file;
  file.open(config_path, std::ios::out | std::ios::trunc);
  file << output.str();
  file.close();
}

void SetupConfig(const std::filesystem::path& config_folder) {
  config::config_folder = config_folder;
  // check if the user specified a specific config to load
  if (!cvars::config.empty()) {
    config_path = xe::to_path(cvars::config);
    if (std::filesystem::exists(config_path)) {
      ReadConfig(config_path);
      return;
    }
  }
  // if the user specified a --config argument, but the file doesn't exist,
  // let's also load the default config
  if (!config_folder.empty()) {
    config_path = config_folder / config_name;
    if (std::filesystem::exists(config_path)) {
      ReadConfig(config_path);
    }
    // we only want to save the config if the user is using the default
    // config, we don't want to override a user created specific config
    SaveConfig();
  }
}

void LoadGameConfig(const std::string_view title_id) {
  const auto game_config_folder = config_folder / "config";
  const auto game_config_path =
      game_config_folder / (std::string(title_id) + game_config_suffix);
  if (std::filesystem::exists(game_config_path)) {
    ReadGameConfig(game_config_path);
  }
}

}  // namespace config
