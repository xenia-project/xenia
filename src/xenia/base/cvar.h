/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CVAR_H_
#define XENIA_CVAR_H_

#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "cpptoml/include/cpptoml.h"
#include "cxxopts/include/cxxopts.hpp"

#include "xenia/base/filesystem.h"
#include "xenia/base/string_util.h"
#include "xenia/config.h"

namespace xe {
namespace cvar {

namespace toml {
std::string EscapeString(const std::string_view view);
}

class IConfigVar {
 public:
  virtual ~IConfigVar() = default;

  const std::string& name() const { return name_; }
  const std::string& description() const { return description_; }
  const std::string& category() const { return category_; }
  bool is_transient() const { return is_transient_; }
  bool requires_restart() const { return requires_restart_; }

  virtual std::string ConfigString() const = 0;
  virtual std::string ToString() const = 0;

  virtual void AddToLaunchOptions(cxxopts::Options* options) = 0;
  virtual void LoadFromLaunchOptions(cxxopts::ParseResult* result) = 0;
  virtual void LoadConfigValue(std::shared_ptr<cpptoml::base> result) = 0;
  virtual void LoadGameConfigValue(std::shared_ptr<cpptoml::base> result) = 0;

 protected:
  IConfigVar(std::string_view name, std::string_view description,
             std::string_view category, bool is_transient,
             bool requires_restart);

  std::string name_;
  std::string description_;
  std::string category_;
  bool is_transient_;
  bool requires_restart_;
};

template <class T>
class ConfigVar final : public IConfigVar {
 public:
  ConfigVar<T>(std::string_view name, T* default_value,
               std::string_view description, std::string_view category,
               bool is_transient, bool requires_restart);

  T default_value() const { return default_value_; }

  T current_value() const { return *current_value_; }

  void set_current_value(const T& val) {
    assert(current_value_ != nullptr);
    *current_value_ = val;
  }

  T* config_value() const { return config_value_.get(); }

  void set_config_value(const T& val) {
    config_value_ = std::make_unique<T>(val);
    UpdateValue();
  }

  void set_game_config_value(const T& val) {
    game_config_value_ = std::make_unique<T>(val);
    UpdateValue();
  }

  T* command_line_value() const { return command_line_value_.get(); }

  void set_command_line_value(const T& val) {
    command_line_value_ = std::make_unique<T>(val);
    UpdateValue();
  }

  T Convert(std::string_view val) const {
    return xe::string_util::from_string<T>(val);
  }

  std::string ConfigString() const override {
    if (config_value_) return this->ToString(*config_value_);
    return this->ToString(this->default_value_);
  }

  std::string ToString() const override { return ToString(*current_value_); }

  static std::string ToString(const T& val) { return std::to_string(val); }

  void AddToLaunchOptions(cxxopts::Options* options) override;
  void LoadFromLaunchOptions(cxxopts::ParseResult* result) override;
  void LoadConfigValue(std::shared_ptr<cpptoml::base> result) override;
  void LoadGameConfigValue(std::shared_ptr<cpptoml::base> result) override;

 private:
  void UpdateValue();

  T default_value_;
  T* current_value_;  // points to a global var defined with DEFINE_CVar

  // optional values acquired from the various means of setting cvar values
  std::unique_ptr<T> command_line_value_ = nullptr;
  std::unique_ptr<T> config_value_ = nullptr;
  std::unique_ptr<T> game_config_value_ = nullptr;
};

template <typename T>
ConfigVar<T>* register_configvar(std::string_view name, T* default_value,
                                 std::string_view description,
                                 std::string_view category, bool is_transient,
                                 bool requires_restart) {
  // config instance claims ownership of this pointer
  ConfigVar<T>* var =
      new ConfigVar<T>{name,     default_value, description,
                       category, is_transient,  requires_restart};
  return Config::Instance().RegisterConfigVar<T>(var);
}

#define DEFINE_bool(name, default_value, description, category) \
  DEFINE_CVar(name, default_value, description, category, false, false, bool)

#define DEFINE_int32(name, default_value, description, category) \
  DEFINE_CVar(name, default_value, description, category, false, false, int32_t)

#define DEFINE_uint64(name, default_value, description, category)       \
  DEFINE_CVar(name, default_value, description, category, false, false, \
              uint64_t)

#define DEFINE_double(name, default_value, description, category) \
  DEFINE_CVar(name, default_value, description, category, false, false, double)

#define DEFINE_string(name, default_value, description, category)       \
  DEFINE_CVar(name, default_value, description, category, false, false, \
              std::string)

#define DEFINE_path(name, default_value, description, category)         \
  DEFINE_CVar(name, default_value, description, category, false, false, \
              std::filesystem::path)

#define DEFINE_unrestartable_bool(name, default_value, description, category) \
  DEFINE_CVar(name, default_value, description, category, false, true, bool)

#define DEFINE_unrestartable_int32(name, default_value, description, category) \
  DEFINE_CVar(name, default_value, description, category, false, true, int32_t)

#define DEFINE_unrestartable_uint64(name, default_value, description, \
                                    category)                         \
  DEFINE_CVar(name, default_value, description, category, false, true, uint64_t)

#define DEFINE_unrestartable_double(name, default_value, description, \
                                    category)                         \
  DEFINE_CVar(name, default_value, description, category, false, true, double)

#define DEFINE_unrestartable_string(name, default_value, description,  \
                                    category)                          \
  DEFINE_CVar(name, default_value, description, category, false, true, \
              std::string)

#define DEFINE_unrestartable_path(name, default_value, description, category) \
  DEFINE_CVar(name, default_value, description, category, false, true,        \
              std::filesystem::path)

#define DEFINE_transient_bool(name, default_value, description, category) \
  DEFINE_CVar(name, default_value, description, category, true, false, bool)

#define DEFINE_transient_string(name, default_value, description, category) \
  DEFINE_CVar(name, default_value, description, category, true, false,      \
              std::string)

#define DEFINE_transient_path(name, default_value, description, category) \
  DEFINE_CVar(name, default_value, description, category, true, false,    \
              std::filesystem::path)

#define DEFINE_CVar(name, default_value, description, category, is_transient, \
                    requires_restart, type)                                   \
  namespace xe::cvars {                                                       \
  type name = default_value;                                                  \
  static auto cv_##name = xe::cvar::register_configvar<type>(                 \
      #name, &cvars::name, description, category, is_transient,               \
      requires_restart);                                                      \
  }  // namespace cvars

#define DECLARE_bool(name) DECLARE_CVar(name, bool)

#define DECLARE_int32(name) DECLARE_CVar(name, int32_t)

#define DECLARE_uint64(name) DECLARE_CVar(name, uint64_t)

#define DECLARE_double(name) DECLARE_CVar(name, double)

#define DECLARE_string(name) DECLARE_CVar(name, std::string)

#define DECLARE_path(name) DECLARE_CVar(name, std::filesystem::path)

#define DECLARE_CVar(name, type) \
  namespace xe::cvars {          \
  extern type name;              \
  }

// load template implementations
#include "cvar.inc"

}  // namespace cvar
}  // namespace xe

#endif  // XENIA_CVAR_H_
