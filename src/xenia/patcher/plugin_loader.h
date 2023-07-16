/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2023 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_PLUGIN_LOADER_H_
#define XENIA_PLUGIN_LOADER_H_

#include "xenia/kernel/kernel_state.h"
#include "xenia/memory.h"

namespace xe {
namespace patcher {

struct PluginInfoEntry {
  std::string name;
  std::string file;
  std::vector<uint64_t> hashes;
  uint32_t title_id;
  std::string desc;
  bool is_enabled;
};

class PluginLoader {
 public:
  PluginLoader(kernel::KernelState* kernel_state,
               const std::filesystem::path plugins_root);

  void LoadTitlePlugins(const uint32_t title_id);
  bool IsAnyPluginForTitleAvailable(const uint32_t title_id,
                                    const uint64_t module_hash) const;
  bool IsAnyPluginLoaded() { return is_any_plugin_loaded_; }

 private:
  void LoadConfigs();
  void LoadTitleConfig(const uint32_t title_id);
  void CreatePluginDevice(const uint32_t title_id);
  void LoadTitlePlugin(const PluginInfoEntry& entry);

  std::vector<uint64_t> GetHashes(
      const std::shared_ptr<cpptoml::base> toml_entry);

  kernel::KernelState* kernel_state_;
  std::filesystem::path plugins_root_;

  std::vector<PluginInfoEntry> plugin_configs_;

  bool is_any_plugin_loaded_;
};

}  // namespace patcher
}  // namespace xe
#endif
