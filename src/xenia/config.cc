#include "xenia/config.h"
#include "base/filesystem.h"
#pragma warning(push, 0)  // the library has warnings but we can't disable it on
                          // project level since it's a header only library
#include "cxxopts/include/cxxopts.hpp"
#pragma warning(pop)
#include "kernel/xboxkrnl/xboxkrnl_error.h"
#include "third_party/cpptoml/include/cpptoml.h"
#include "xenia/base/string.h"

namespace config {
cxxopts::Options options("xenia", "Xbox 360 Emulator");
std::wstring config_name = L"xenia.cfg";
std::wstring config_path;
std::map<std::string, CommandVar*>* CmdVars;
std::map<std::string, ConfigVar*>* ConfigVars;
char** data;
void AddConfigVar(ConfigVar* cv) {
  if (!ConfigVars) ConfigVars = new std::map<std::string, ConfigVar*>();
  ConfigVars->insert(std::pair<std::string, ConfigVar*>(cv->GetName(), cv));
}
void AddCommandVar(CommandVar* cv) {
  if (!CmdVars) CmdVars = new std::map<std::string, CommandVar*>();
  CmdVars->insert(std::pair<std::string, CommandVar*>(cv->GetName(), cv));
}

void PrintHelpAndExit() {
  std::cout << options.help() << std::endl;
  exit(0);
}

ConfigVar GetConfigVar(const char* name) {
  auto it = ConfigVars->find(name);
  return *it->second;
}

void ParseLaunchArguments(int argc, char** argv) {
  for (auto& it : *CmdVars) {
    auto cmdVar = static_cast<CommandVar*>(it.second);
    options.add_options()(cmdVar->GetName(), cmdVar->GetDescription(),
                          cxxopts::value<std::string>());
  }
  try {
    options.positional_help("[Path to .iso/.xex]");
    options.parse_positional({"target"});
    auto result = options.parse(argc, argv);
    if (result.count("help")) {
      PrintHelpAndExit();
    }
    for (auto& it : *CmdVars) {
      auto cmdVar = static_cast<CommandVar*>(it.second);
      if (result.count(cmdVar->GetName())) {
        auto value = result[cmdVar->GetName()].as<std::string>();
        cmdVar->SetCommandLineValue(value);
      }
    }
  } catch (const cxxopts::OptionException&) {
    PrintHelpAndExit();
  }
}

void ReadConfig(const std::wstring& file_path) {
  const auto config = cpptoml::parse_file(xe::to_string(file_path));
  for (auto& it : *ConfigVars) {
    auto configVar = static_cast<ConfigVar*>(it.second);
    auto configKey = configVar->GetCategory() + "." + configVar->GetName();
    if (config->contains_qualified(configKey)) {
      configVar->SetConfigValue(
          *config->get_qualified_as<std::string>(configKey));
    }
  }
}

void ReadGameConfig(std::wstring file_path) {
  const auto config = cpptoml::parse_file(xe::to_string(file_path));
  for (auto& it : *ConfigVars) {
    auto configVar = static_cast<ConfigVar*>(it.second);
    auto configKey = configVar->GetName();
    // game config variables don't have to be in a category
    if (config->contains(configVar->GetName())) {
      configVar->SetGameConfigValue(
          *config->get_as<std::string>(configVar->GetName()));
    }
  }
}

bool sortCvar(ConfigVar* a, ConfigVar* b) {
  if (a->GetCategory() < b->GetCategory()) return true;
  if (a->GetName() < b->GetName()) return true;
  return false;
}

void SaveConfig() {
  const auto file_path = xe::to_string(config_path);

  std::vector<ConfigVar*> vars;
  for (const auto& s : *ConfigVars) vars.push_back(s.second);
  std::sort(vars.begin(), vars.end(), sortCvar);
  // we use our own write logic because cpptoml doesn't
  // allow us to specify comments :(
  std::ofstream outfile;
  outfile.open(file_path, std::ios::out | std::ios::trunc);
  outfile << "#This is the Xenia config file, in toml format\n";

  std::string lastCategory;
  for (auto configVar : vars) {
    if (lastCategory != configVar->GetCategory()) {
      lastCategory = configVar->GetCategory();
      outfile << xe::format_string("\n[%s]\n", lastCategory.c_str());
    }
    outfile << xe::format_string("%s = \"%s\" \t\t#%s\n",
                                 configVar->GetName().c_str(),
                                 configVar->GetConfigValue().c_str(),
                                 configVar->GetDescription().c_str());
  }
  outfile.close();
}

void SetupConfig(const std::wstring& config_folder) {
  if (!config_folder.empty()) {
    config_path = xe::join_paths(config_folder, config_name);
    if (xe::filesystem::PathExists(config_path)) {
      ReadConfig(config_path);
    }
    SaveConfig();
  }
}

void LoadGameConfig(const std::wstring& config_folder,
                    const std::wstring& title_id) {
  const auto game_config_path =
      xe::join_paths(config_folder, title_id + L".cfg");
  if (xe::filesystem::PathExists(game_config_path)) {
    ReadGameConfig(game_config_path);
  }
}

}  // namespace config

CommandVar::CommandVar(const char* name, const char* defaultValue,
               const char* description)
    : name_(name), defaultValue_(defaultValue), description_(description) {
  config::AddCommandVar(this);
}
ConfigVar::ConfigVar(const char* name, const char* defaultValue,
                const char* description,
           const char* category)
    : CommandVar(name, defaultValue, description), category_(category) {
  config::AddConfigVar(this);
}
std::string CommandVar::GetValue() {
  if (!this->commandLineValue_.empty()) return this->commandLineValue_;
  return defaultValue_;
}
std::string ConfigVar::GetValue() {
  if (!this->commandLineValue_.empty()) return this->commandLineValue_;
  if (!this->gameConfigValue_.empty()) return this->gameConfigValue_;
  if (!this->configValue_.empty()) return this->configValue_;
  return defaultValue_;
}
std::string ConfigVar::GetConfigValue() {
  if (!this->configValue_.empty()) return this->configValue_;
  return defaultValue_;
}
void CommandVar::SetCommandLineValue(const std::string val) {
  this->commandLineValue_ = val;
}
void ConfigVar::SetConfigValue(const std::string val) {
  this->configValue_ = val;
}
void ConfigVar::SetGameConfigValue(const std::string val) {
  this->gameConfigValue_ = val;
}
bool CommandVar::GetBool() {
  if (!_conversionDone) {
    _valAsBool = GetValue()[0] == '1';
    _conversionDone = true;
  }
  return _valAsBool;
}

float CommandVar::GetFloat() {
  if (!_conversionDone) {
    _valAsFloat = std::stof(GetValue());
    _conversionDone = true;
  }
  return _valAsFloat;
}
int CommandVar::GetInt() {
  if (!_conversionDone) {
    _valAsInt = std::stoi(GetValue());
    _conversionDone = true;
  }
  return _valAsInt;
}
