/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CONFIG_H_
#define XENIA_CONFIG_H_

#include <filesystem>
#include <unordered_map>
#include "cxxopts/include/cxxopts.hpp"

namespace xe {

namespace cvar {

class IConfigVar;
class ICommandVar;

template <typename T>
class ConfigVar;
template <typename T>
class CommandVar;

}  // namespace cvar

enum class SaveStatus : int {
  Saved = 1 << 0,
  RequiresRestart = 1 << 0 | 1 << 1,
  CouldNotSave = 1 << 2
};

using IConfigVarRef = std::unique_ptr<cvar::IConfigVar>;
using ICommandVarRef = std::unique_ptr<cvar::ICommandVar>;

class Config {
 public:
  static Config& Instance();
  /**
   * Populates the global config variables from a config file
   */
  void SetupConfig(const std::filesystem::path& config_folder);

  /**
   * Save the current config variable state to the config file
   */
  SaveStatus SaveConfig();

  /**
   * Overrides config variables with those from a game-specific config file (if
   * any)
   */
  void LoadGameConfig(std::string_view title_id);

  /**
   * Find a config variable for a given config field
   */
  cvar::IConfigVar* FindConfigVar(const std::string& name);

  /**
   * Find a command variable for a given name
   */
  cvar::ICommandVar* FindCommandVar(const std::string& name);

  /**
   * Parse launch arguments and update cvar values based on provided launch
   * parameters
   */
  void ParseLaunchArguments(int& argc, char**& argv,
                            std::string_view positional_help,
                            const std::vector<std::string>& positional_options);

  /**
   * Register a config var to the config system
   * Claims ownership of the provided pointer
   */
  template <typename T>
  cvar::ConfigVar<T>* RegisterConfigVar(cvar::ConfigVar<T>* var);

  /**
   * Register a command var to the config system
   * Claims ownership of the provided pointer
   */
  template <typename T>
  cvar::CommandVar<T>* RegisterCommandVar(cvar::CommandVar<T>* var);

 private:
  Config();

  void ReadConfig(const std::filesystem::path& file_path);
  void ReadGameConfig(const std::filesystem::path& file_path);
  void PrintHelpAndExit();

  std::unordered_map<std::string, IConfigVarRef> config_vars_;
  std::unordered_map<std::string, ICommandVarRef> command_vars_;
  std::filesystem::path config_folder_;
  std::filesystem::path config_path_;
  std::string config_name_ = "xenia.config.toml";
  std::string game_config_suffix_ = ".config.toml";
  cxxopts::Options options_;
};

template <typename T>
cvar::ConfigVar<T>* Config::RegisterConfigVar(cvar::ConfigVar<T>* var) {
  const std::string& name = var->name();
  config_vars_[name] = std::unique_ptr<cvar::ConfigVar<T>>(var);
  return dynamic_cast<cvar::ConfigVar<T>*>(config_vars_[name].get());
}

template <typename T>
cvar::CommandVar<T>* Config::RegisterCommandVar(cvar::CommandVar<T>* var) {
  const std::string& name = var->name();
  command_vars_[name] = std::unique_ptr<cvar::CommandVar<T>>(var);
  return dynamic_cast<cvar::CommandVar<T>*>(command_vars_[name].get());
}

}  // namespace xe

#endif  // XENIA_CONFIG_H_
