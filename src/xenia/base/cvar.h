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

class ICommandVar {
 public:
  virtual ~ICommandVar() = default;
  virtual const std::string& name() const = 0;
  virtual const std::string& description() const = 0;
  virtual void UpdateValue() = 0;
  virtual void AddToLaunchOptions(cxxopts::Options* options) = 0;
  virtual void LoadFromLaunchOptions(cxxopts::ParseResult* result) = 0;
};

class IConfigVar : virtual public ICommandVar {
 public:
  virtual const std::string& category() const = 0;
  virtual bool is_transient() const = 0;
  virtual bool requires_restart() const = 0;
  virtual std::string config_value() const = 0;
  virtual void LoadConfigValue(std::shared_ptr<cpptoml::base> result) = 0;
  virtual void LoadGameConfigValue(std::shared_ptr<cpptoml::base> result) = 0;
};

template <class T>
class CommandVar : virtual public ICommandVar {
 public:
  CommandVar<T>(std::string_view name, T* default_value,
                std::string_view description);

  const std::string& name() const override { return name_; }
  const std::string& description() const override { return description_; }

  void AddToLaunchOptions(cxxopts::Options* options) override;
  void LoadFromLaunchOptions(cxxopts::ParseResult* result) override;

  T* current_value() { return current_value_; }
  T* command_line_value() { return commandline_value_.get(); }
  const T& default_value() const { return default_value_; }

 protected:
  void set_current_value(const T& val) { *current_value_ = val; }

  void set_command_line_value(const T& val) {
    commandline_value_ = std::make_unique<T>(val);
  }

  T Convert(std::string_view val) const {
    return xe::string_util::from_string<T>(val);
  }

  static std::string ToString(const T& val) { return std::to_string(val); }
  void UpdateValue() override;

 private:
  std::string name_;
  T default_value_;
  T* current_value_;
  std::unique_ptr<T> commandline_value_;
  std::string description_;
};
#pragma warning(push)
#pragma warning(disable : 4250)

template <class T>
class ConfigVar final : public CommandVar<T>, virtual public IConfigVar {
 public:
  ConfigVar<T>(std::string_view name, T* default_value,
               std::string_view description, std::string_view category,
               bool is_transient, bool requires_restart);

  std::string config_value() const override {
    if (config_value_) return this->ToString(*config_value_);
    return this->ToString(this->default_value());
  }

  const std::string& category() const override { return category_; }
  bool is_transient() const override { return is_transient_; }
  bool requires_restart() const override { return requires_restart_; }

  void set_config_value(const T& val) {
    config_value_ = std::make_unique<T>(val);
    UpdateValue();
  }

  void set_game_config_value(const T& val) {
    game_config_value_ = std::make_unique<T>(val);
    UpdateValue();
  }

  void AddToLaunchOptions(cxxopts::Options* options) override;
  void LoadConfigValue(std::shared_ptr<cpptoml::base> result) override;
  void LoadGameConfigValue(std::shared_ptr<cpptoml::base> result) override;

 private:
  void UpdateValue() override;

  std::string category_;
  bool is_transient_;
  bool requires_restart_;
  std::unique_ptr<T> config_value_ = nullptr;
  std::unique_ptr<T> game_config_value_ = nullptr;
};

#pragma warning(pop)

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

template <typename T>
CommandVar<T>* register_commandvar(std::string_view name, T* default_value,
                                   std::string_view description) {
  // config instance claims ownership of this pointer
  CommandVar<T>* var = new CommandVar<T>{name, default_value, description};
  return Config::Instance().RegisterCommandVar<T>(var);
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
  DEFINE_CVar(name, default_value, description, category, true, bool)

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

// CmdVars can only be strings for now, we don't need any others
#define CmdVar(name, default_value, description)                          \
  namespace xe::cvars {                                                   \
  std::string name = default_value;                                       \
  static auto cmdvar_##name = xe::cvar::register_commandvar<std::string>( \
      #name, &cvars::name, description);                                  \
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
