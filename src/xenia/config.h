#ifndef XENIA_CONFIG_H_
#define XENIA_CONFIG_H_
#include <string>

namespace config {
void SetupConfig(const std::wstring& config_folder);
void LoadGameConfig(const std::wstring& title_id);
}  // namespace config

#endif  // XENIA_CONFIG_H_
