/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

template <class T>
void CommandVar<T>::AddToLaunchOptions(cxxopts::Options* options) {
  options->add_options()(name_, description_, cxxopts::value<T>());
}
template <>
inline void CommandVar<std::filesystem::path>::AddToLaunchOptions(
    cxxopts::Options* options) {
  options->add_options()(name_, description_, cxxopts::value<std::string>());
}
template <class T>
void ConfigVar<T>::AddToLaunchOptions(cxxopts::Options* options) {
  options->add_options(category_)(this->name(), this->description(),
                                  cxxopts::value<T>());
}
template <>
inline void ConfigVar<std::filesystem::path>::AddToLaunchOptions(
    cxxopts::Options* options) {
  options->add_options(category_)(this->name(), this->description(),
                                  cxxopts::value<std::string>());
}
template <class T>
void CommandVar<T>::LoadFromLaunchOptions(cxxopts::ParseResult* result) {
  T value = (*result)[name_].as<T>();
  set_command_line_value(value);
}
template <>
inline void CommandVar<std::filesystem::path>::LoadFromLaunchOptions(
    cxxopts::ParseResult* result) {
  std::string value = (*result)[name_].as<std::string>();
  set_command_line_value(value);
}
template <class T>
void ConfigVar<T>::LoadConfigValue(std::shared_ptr<cpptoml::base> result) {
  set_config_value(*cpptoml::get_impl<T>(result));
}
template <>
inline void ConfigVar<std::filesystem::path>::LoadConfigValue(
    std::shared_ptr<cpptoml::base> result) {
  set_config_value(
      xe::utf8::fix_path_separators(*cpptoml::get_impl<std::string>(result)));
}
template <class T>
void ConfigVar<T>::LoadGameConfigValue(std::shared_ptr<cpptoml::base> result) {
  set_game_config_value(*cpptoml::get_impl<T>(result));
}
template <>
inline void ConfigVar<std::filesystem::path>::LoadGameConfigValue(
    std::shared_ptr<cpptoml::base> result) {
  set_game_config_value(
      xe::utf8::fix_path_separators(*cpptoml::get_impl<std::string>(result)));
}
template <class T>
CommandVar<T>::CommandVar(std::string_view name, T* default_value,
                          std::string_view description)
    : name_(name),
      default_value_(*default_value),
      description_(description),
      current_value_(default_value) {}

template <class T>
ConfigVar<T>::ConfigVar(std::string_view name, T* default_value,
                        std::string_view description, std::string_view category,
                        bool is_transient, bool requires_restart)
    : CommandVar<T>(name, default_value, description),
      category_(category),
      is_transient_(is_transient),
      requires_restart_(requires_restart) {}

template <class T>
void CommandVar<T>::UpdateValue() {
  if (commandline_value_) return set_current_value(*commandline_value_);
  return set_current_value(default_value_);
}
template <class T>
void ConfigVar<T>::UpdateValue() {
  if (this->command_line_value()) {
    return this->set_config_value(*this->command_line_value());
  }
  if (game_config_value_) return this->set_current_value(*game_config_value_);
  if (config_value_) return this->set_current_value(*config_value_);
  return this->set_current_value(this->default_value());
}

template <>
inline std::string CommandVar<std::string>::Convert(std::string_view val) const {
  return std::string(val);
}
template <>
inline std::filesystem::path CommandVar<std::filesystem::path>::Convert(
    std::string_view val) const {
  return xe::to_path(val);
}

template <>
inline std::string CommandVar<bool>::ToString(const bool& val) {
  return val ? "true" : "false";
}
template <>
inline std::string CommandVar<std::string>::ToString(const std::string& val) {
  return toml::EscapeString(val);
}
template <>
inline std::string CommandVar<std::filesystem::path>::ToString(const 
    std::filesystem::path& val) {
  return toml::EscapeString(
      xe::utf8::fix_path_separators(xe::path_to_utf8(val), '/'));
}

