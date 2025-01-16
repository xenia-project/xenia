/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2023 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */
#include <string>

#include "xenia/base/logging.h"
#include "xenia/config.h"
#include "xenia/kernel/user_module.h"
#include "xenia/kernel/xthread.h"
#include "xenia/patcher/plugin_loader.h"
#include "xenia/vfs/devices/host_path_device.h"

DEFINE_bool(
    allow_plugins, false,
    "Allows loading of plugins/trainers from plugins\\title_id\\plugin.xex. "
    "Plugin are homebrew xex modules which can be used for making mods. "
    "This feature is experimental.",
    "General");

namespace xe {
namespace patcher {

PluginLoader::PluginLoader(kernel::KernelState* kernel_state,
                           const std::filesystem::path plugins_root) {
  kernel_state_ = kernel_state;
  plugins_root_ = plugins_root;
  is_any_plugin_loaded_ = false;

  if (!cvars::allow_plugins) {
    return;
  }

  plugin_configs_.clear();
  LoadConfigs();
}

void PluginLoader::LoadConfigs() {
  // Iterate through directory and check if .toml file exists
  std::vector<xe::filesystem::FileInfo> dir_files =
      xe::filesystem::ListDirectories(plugins_root_);

  dir_files =
      xe::filesystem::FilterByName(dir_files, std::regex("[A-Fa-f0-9]{8}"));

  for (const auto& entry : dir_files) {
    const uint32_t title_id = string_util::from_string<uint32_t>(
        entry.name.filename().string(), true);

    LoadTitleConfig(title_id);
  }

  XELOGI("Plugins: Loaded plugins for {} titles", dir_files.size());
}

void PluginLoader::LoadTitleConfig(const uint32_t title_id) {
  const std::filesystem::path title_plugins_config =
      plugins_root_ / fmt::format("{:08X}\\plugins.toml", title_id);

  if (!std::filesystem::exists(title_plugins_config)) {
    return;
  }

  toml::parse_result plugins_config;

  try {
    plugins_config = ParseFile(title_plugins_config);
  } catch (...) {
    XELOGE("Plugins: Cannot load plugin file {}", title_plugins_config);
    return;
  }

  const std::string title_name =
      plugins_config.get_as<std::string>("title_name")->get();

  const std::string plugin_title_id =
      plugins_config.get_as<std::string>("title_id")->get();

  if (!plugins_config.contains("plugin")) {
    XELOGE("Plugins: Cannot find [[plugin]] table in {}", title_plugins_config);
    return;
  }

  const auto plugin_array = plugins_config.get_as<toml::array>("plugin");
  if (!plugin_array) {
    return;
  }

  for (const auto& plugin_entry : *plugin_array) {
    PluginInfoEntry entry;

    if (!plugin_entry.is_table()) {
      return;
    }
    entry.title_id =
        xe::string_util::from_string<uint32_t>(plugin_title_id, true);

    if (plugin_entry.as_table()->contains("name")) {
      entry.name = plugin_entry.as_table()->get_as<std::string>("name")->get();
    }
    if (plugin_entry.as_table()->contains("file")) {
      entry.file = xe::string_util::trim(
          plugin_entry.as_table()->get_as<std::string>("file")->get());
    }
    if (plugin_entry.as_table()->contains("desc")) {
      entry.desc = plugin_entry.as_table()->get_as<std::string>("desc")->get();
    }
    if (plugin_entry.as_table()->contains("is_enabled")) {
      entry.is_enabled =
          plugin_entry.as_table()->get_as<bool>("is_enabled")->get();
    }

    entry.hashes = GetHashes(plugin_entry.as_table()->get("hash"));

    if (!plugin_entry.as_table()->contains("hash") || entry.hashes.empty()) {
      const std::string file =
          fmt::format("plugins\\{:08X}\\plugins.toml", title_id);

      XELOGE("Hash error! skipping plugin {} in: {}", entry.name, file);
      continue;
    }

    plugin_configs_.push_back(entry);
  }
}

std::vector<uint64_t> PluginLoader::GetHashes(
    const toml::node* toml_entry) const {
  std::vector<uint64_t> hashes;

  if (!toml_entry) {
    return hashes;
  }

  if (toml_entry->is_value()) {
    const std::string hash = toml_entry->as_string()->get();

    if (hash.empty()) {
      return hashes;
    }

    hashes.push_back(xe::string_util::from_string<uint64_t>(hash, true));
  }

  if (toml_entry->is_array()) {
    for (const auto& hash_entry : *toml_entry->as_array()) {
      if (hash_entry.as_string()->get().empty()) {
        continue;
      }

      hashes.push_back(xe::string_util::from_string<uint64_t>(
          hash_entry.as_string()->get(), true));
    }
  }
  return hashes;
}

bool PluginLoader::IsAnyPluginForTitleAvailable(
    const uint32_t title_id, const uint64_t module_hash) const {
  const auto result = std::find_if(
      plugin_configs_.cbegin(), plugin_configs_.cend(),
      [title_id, module_hash](const PluginInfoEntry& entry) {
        const auto hash_exists =
            std::find(entry.hashes.cbegin(), entry.hashes.cend(),
                      module_hash) != entry.hashes.cend();

        return entry.is_enabled && entry.title_id == title_id && hash_exists;
      });

  return result != plugin_configs_.cend();
}

void PluginLoader::LoadTitlePlugins(const uint32_t title_id) {
  std::vector<PluginInfoEntry> title_plugins;

  std::copy_if(plugin_configs_.cbegin(), plugin_configs_.cend(),
               std::back_inserter(title_plugins),
               [title_id](const PluginInfoEntry& entry) {
                 return entry.is_enabled && entry.title_id == title_id;
               });

  if (title_plugins.empty()) {
    return;
  }

  CreatePluginDevice(title_id);

  for (const auto& entry : title_plugins) {
    kernel::object_ref<kernel::XHostThread> plugin_thread =
        kernel::object_ref<kernel::XHostThread>(new xe::kernel::XHostThread(
            kernel_state_, 128 * 1024, 0, [this, entry]() {
              LoadTitlePlugin(entry);
              return 0;
            }));

    plugin_thread->set_name(
        fmt::format("Plugin Loader: {} Thread", entry.name));
    plugin_thread->Create();

    xe::threading::Wait(plugin_thread->thread(), false);

    is_any_plugin_loaded_ = true;
  }
}

void PluginLoader::LoadTitlePlugin(const PluginInfoEntry& entry) {
  auto user_module =
      kernel_state_->LoadUserModule(fmt::format("plugins:\\{}", entry.file));

  if (!user_module) {
    return;
  }

  kernel_state_->FinishLoadingUserModule(user_module);
  const uint32_t hmodule = user_module->hmodule_ptr();

  if (hmodule == 0) {
    return;
  }

  kernel_state_->memory()
      ->TranslateVirtual<xe::kernel::X_LDR_DATA_TABLE_ENTRY*>(hmodule)
      ->load_count++;

  XELOGI("Plugin Loader: {} Plugin successfully loaded!", entry.name);
}

void PluginLoader::CreatePluginDevice(const uint32_t title_id) {
  const std::string mount_plugins = "\\Device\\Plugins";

  const std::filesystem::path plugins_host_path =
      plugins_root_ / fmt::format("{:08X}", title_id);

  kernel_state_->file_system()->RegisterSymbolicLink("plugins:", mount_plugins);
  auto device_plugins = std::make_unique<xe::vfs::HostPathDevice>(
      mount_plugins, plugins_host_path, false);

  if (!device_plugins->Initialize()) {
    xe::FatalError("Unable to mount {}; file not found or corrupt.");
    return;
  }

  if (!kernel_state_->file_system()->RegisterDevice(
          std::move(device_plugins))) {
    xe::FatalError("Unable to register {}.");
    return;
  }

  return;
}

}  // namespace patcher
}  // namespace xe