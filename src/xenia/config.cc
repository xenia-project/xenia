#include "config.h"
#include "cpptoml/include/cpptoml.h"
#include "xenia/base/cvar.h"
#include "xenia/base/filesystem.h"
#include "xenia/base/logging.h"
#include "xenia/base/string.h"

CmdVar(config, "", "Specifies the target config to load.");
namespace config {
std::wstring config_name = L"xenia.config.toml";
std::wstring config_folder;
std::wstring config_path;

bool sortCvar(cvar::IConfigVar* a, cvar::IConfigVar* b) {
  if (a->category() < b->category()) return true;
  if (a->category() > b->category()) return false;
  if (a->name() < b->name()) return true;
  return false;
}

std::shared_ptr<cpptoml::table> ParseConfig(std::string config_path) {
  try {
    return cpptoml::parse_file(config_path);
  } catch (cpptoml::parse_exception e) {
    xe::FatalError("Failed to parse config file '%s':\n\n%s",
                   config_path.c_str(), e.what());
    return nullptr;
  }
}

void ReadConfig(const std::wstring& file_path) {
  const auto config = ParseConfig(xe::to_string(file_path));
  for (auto& it : *cvar::ConfigVars) {
    auto configVar = static_cast<cvar::IConfigVar*>(it.second);
    auto configKey = configVar->category() + "." + configVar->name();
    if (config->contains_qualified(configKey)) {
      configVar->LoadConfigValue(config->get_qualified(configKey));
    }
  }
  XELOGI("Loaded config: %S", file_path.c_str());
}

void ReadGameConfig(std::wstring file_path) {
  const auto config = ParseConfig(xe::to_string(file_path));
  for (auto& it : *cvar::ConfigVars) {
    auto configVar = static_cast<cvar::IConfigVar*>(it.second);
    auto configKey = configVar->category() + "." + configVar->name();
    if (config->contains_qualified(configKey)) {
      configVar->LoadGameConfigValue(config->get_qualified(configKey));
    }
  }
  XELOGI("Loaded game config: %S", file_path.c_str());
}

void SaveConfig() {
  std::vector<cvar::IConfigVar*> vars;
  for (const auto& s : *cvar::ConfigVars) vars.push_back(s.second);
  std::sort(vars.begin(), vars.end(), sortCvar);
  // we use our own write logic because cpptoml doesn't
  // allow us to specify comments :(
  std::ostringstream output;
  std::string lastCategory;
  for (auto configVar : vars) {
    if (configVar->is_transient()) {
      continue;
    }
    if (lastCategory != configVar->category()) {
      if (!lastCategory.empty()) {
        output << std::endl;
      }
      lastCategory = configVar->category();
      output << xe::format_string("[%s]\n", lastCategory.c_str());
    }
    output << std::left << std::setw(40) << std::setfill(' ')
           << xe::format_string("%s = %s", configVar->name().c_str(),
                                configVar->config_value().c_str());
    output << xe::format_string("\t# %s\n", configVar->description().c_str());
  }

  if (xe::filesystem::PathExists(config_path)) {
    std::ifstream existingConfigStream(xe::to_string(config_path).c_str());
    const std::string existingConfig(
        (std::istreambuf_iterator<char>(existingConfigStream)),
        std::istreambuf_iterator<char>());
    // if the config didn't change, no need to modify the file
    if (existingConfig == output.str()) return;
  }

  // save the config file
  xe::filesystem::CreateParentFolder(config_path);
  std::ofstream file;
  file.open(xe::to_string(config_path), std::ios::out | std::ios::trunc);
  file << output.str();
  file.close();
}

void SetupConfig(const std::wstring& config_folder) {
  config::config_folder = config_folder;
  // check if the user specified a specific config to load
  if (!cvars::config.empty()) {
    config_path = xe::to_wstring(cvars::config);
    if (xe::filesystem::PathExists(config_path)) {
      ReadConfig(config_path);
      return;
    }
  }
  // if the user specified a --config argument, but the file doesn't exist,
  // let's also load the default config
  if (!config_folder.empty()) {
    config_path = xe::join_paths(config_folder, config_name);
    if (xe::filesystem::PathExists(config_path)) {
      ReadConfig(config_path);
    }
    // we only want to save the config if the user is using the default
    // config, we don't want to override a user created specific config
    SaveConfig();
  }
}

void LoadGameConfig(const std::wstring& title_id) {
  const auto content_folder = xe::join_paths(config_folder, L"content");
  const auto game_folder = xe::join_paths(content_folder, title_id);
  const auto game_config_path = xe::join_paths(game_folder, config_name);
  if (xe::filesystem::PathExists(game_config_path)) {
    ReadGameConfig(game_config_path);
  }
}

}  // namespace config
