#include "config.h"
#include "cpptoml/include/cpptoml.h"
#include "xenia/base/cvar.h"
#include "xenia/base/filesystem.h"
#include "xenia/base/logging.h"
#include "xenia/base/string.h"

namespace cpptoml {
inline std::shared_ptr<table> parse_file(const std::wstring& filename) {
  std::ifstream file(filename);
  if (!file.is_open()) {
    throw parse_exception(xe::to_string(filename) +
                          " could not be opened for parsing");
  }
  parser p(file);
  return p.parse();
}
}  // namespace cpptoml

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

std::shared_ptr<cpptoml::table> ParseConfig(const std::wstring& config_path) {
  try {
    return cpptoml::parse_file(config_path);
  } catch (cpptoml::parse_exception e) {
    xe::FatalError(L"Failed to parse config file '%s':\n\n%s",
                   config_path.c_str(), xe::to_wstring(e.what()).c_str());
    return nullptr;
  }
}

void ReadConfig(const std::wstring& file_path) {
  const auto config = ParseConfig(file_path);
  for (auto& it : *cvar::ConfigVars) {
    auto config_var = static_cast<cvar::IConfigVar*>(it.second);
    auto config_key = config_var->category() + "." + config_var->name();
    if (config->contains_qualified(config_key)) {
      config_var->LoadConfigValue(config->get_qualified(config_key));
    }
  }
  XELOGI("Loaded config: %S", file_path.c_str());
}

void ReadGameConfig(std::wstring file_path) {
  const auto config = ParseConfig(file_path);
  for (auto& it : *cvar::ConfigVars) {
    auto config_var = static_cast<cvar::IConfigVar*>(it.second);
    auto config_key = config_var->category() + "." + config_var->name();
    if (config->contains_qualified(config_key)) {
      config_var->LoadGameConfigValue(config->get_qualified(config_key));
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
      output << xe::format_string("[%s]\n", last_category.c_str());
    }

    auto value = config_var->config_value();
    if (value.find('\n') == std::string::npos) {
      output << std::left << std::setw(40) << std::setfill(' ')
             << xe::format_string("%s = %s", config_var->name().c_str(),
                                  config_var->config_value().c_str());
    } else {
      auto lines = xe::split_string(value, "\n");
      auto first_it = lines.cbegin();
      output << xe::format_string("%s = %s\n", config_var->name().c_str(),
                                  *first_it);
      auto last_it = std::prev(lines.cend());
      for (auto it = std::next(first_it); it != last_it; ++it) {
        output << *it << "\n";
      }
      output << std::left << std::setw(40) << std::setfill(' ') << *last_it;
    }
    output << xe::format_string("\t# %s\n", config_var->description().c_str());
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
  file.open(config_path, std::ios::out | std::ios::trunc);
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
