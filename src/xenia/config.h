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
#include "third_party/tomlplusplus/toml.hpp"

toml::parse_result ParseFile(const std::filesystem::path& filename);

namespace config {
void SetupConfig(const std::filesystem::path& config_folder);
void LoadGameConfig(const std::string_view title_id);
void SaveConfig();
}  // namespace config

#endif  // XENIA_CONFIG_H_
