/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2019 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CVAR_H_
#define XENIA_CVAR_H_

#include <map>
#include <string>
#include <vector>

#include "cpptoml/include/cpptoml.h"
#include "cxxopts/include/cxxopts.hpp"
#include "xenia/base/string_util.h"

namespace cvar {

namespace toml {
std::string EscapeString(const std::string& str);
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
  virtual std::string config_value() const = 0;
  virtual void LoadConfigValue(std::shared_ptr<cpptoml::base> result) = 0;
  virtual void LoadGameConfigValue(std::shared_ptr<cpptoml::base> result) = 0;
};

template <class T>
class CommandVar : virtual public ICommandVar {
 public:
  CommandVar<T>(const char* name, T* default_value, const char* description);
  const std::string& name() const override;
  const std::string& description() const override;
  void AddToLaunchOptions(cxxopts::Options* options) override;
  void LoadFromLaunchOptions(cxxopts::ParseResult* result) override;
  T* current_value() { return current_value_; }

 protected:
  std::string name_;
  T default_value_;
  T* current_value_;
  std::unique_ptr<T> commandline_value_;
  std::string description_;
  T Convert(std::string val);
  static std::string ToString(T val);
  void SetValue(T val);
  void SetCommandLineValue(T val);
  void UpdateValue() override;
};
#pragma warning(push)
#pragma warning(disable : 4250)
template <class T>
class ConfigVar : public CommandVar<T>, virtual public IConfigVar {
 public:
  ConfigVar<T>(const char* name, T* default_value, const char* description,
               const char* category, bool is_transient);
  std::string config_value() const override;
  const std::string& category() const override;
  bool is_transient() const override;
  void AddToLaunchOptions(cxxopts::Options* options) override;
  void LoadConfigValue(std::shared_ptr<cpptoml::base> result) override;
  void LoadGameConfigValue(std::shared_ptr<cpptoml::base> result) override;
  void SetConfigValue(T val);
  void SetGameConfigValue(T val);

 private:
  std::string category_;
  bool is_transient_;
  std::unique_ptr<T> config_value_ = nullptr;
  std::unique_ptr<T> game_config_value_ = nullptr;
  void UpdateValue() override;
};

#pragma warning(pop)
template <class T>
const std::string& CommandVar<T>::name() const {
  return name_;
}
template <class T>
const std::string& CommandVar<T>::description() const {
  return description_;
}
template <class T>
void CommandVar<T>::AddToLaunchOptions(cxxopts::Options* options) {
  options->add_options()(name_, description_, cxxopts::value<T>());
}
template <class T>
void ConfigVar<T>::AddToLaunchOptions(cxxopts::Options* options) {
  options->add_options(category_)(this->name_, this->description_,
                                  cxxopts::value<T>());
}
template <class T>
void CommandVar<T>::LoadFromLaunchOptions(cxxopts::ParseResult* result) {
  T value = (*result)[name_].template as<T>();
  SetCommandLineValue(value);
}
template <class T>
void ConfigVar<T>::LoadConfigValue(std::shared_ptr<cpptoml::base> result) {
  SetConfigValue(*cpptoml::get_impl<T>(result));
}
template <class T>
void ConfigVar<T>::LoadGameConfigValue(std::shared_ptr<cpptoml::base> result) {
  SetGameConfigValue(*cpptoml::get_impl<T>(result));
}
template <class T>
CommandVar<T>::CommandVar(const char* name, T* default_value,
                          const char* description)
    : name_(name),
      default_value_(*default_value),
      description_(description),
      current_value_(default_value) {}

template <class T>
ConfigVar<T>::ConfigVar(const char* name, T* default_value,
                        const char* description, const char* category,
                        bool is_transient)
    : CommandVar<T>(name, default_value, description),
      category_(category),
      is_transient_(is_transient) {}

template <class T>
void CommandVar<T>::UpdateValue() {
  if (commandline_value_) return SetValue(*commandline_value_);
  return SetValue(default_value_);
}
template <class T>
void ConfigVar<T>::UpdateValue() {
  if (this->commandline_value_) {
    return this->SetValue(*this->commandline_value_);
  }
  if (game_config_value_) return this->SetValue(*game_config_value_);
  if (config_value_) return this->SetValue(*config_value_);
  return this->SetValue(this->default_value_);
}
template <class T>
T CommandVar<T>::Convert(std::string val) {
  return xe::string_util::from_string<T>(val);
}
template <>
inline std::string CommandVar<std::string>::Convert(std::string val) {
  return val;
}

template <>
inline std::string CommandVar<bool>::ToString(bool val) {
  return val ? "true" : "false";
}
template <>
inline std::string CommandVar<std::string>::ToString(std::string val) {
  return toml::EscapeString(val);
}

template <class T>
std::string CommandVar<T>::ToString(T val) {
  return std::to_string(val);
}

template <class T>
void CommandVar<T>::SetValue(T val) {
  *current_value_ = val;
}
template <class T>
const std::string& ConfigVar<T>::category() const {
  return category_;
}
template <class T>
bool ConfigVar<T>::is_transient() const {
  return is_transient_;
}
template <class T>
std::string ConfigVar<T>::config_value() const {
  if (config_value_) return this->ToString(*config_value_);
  return this->ToString(this->default_value_);
}
template <class T>
void CommandVar<T>::SetCommandLineValue(const T val) {
  commandline_value_ = std::make_unique<T>(val);
  UpdateValue();
}
template <class T>
void ConfigVar<T>::SetConfigValue(T val) {
  config_value_ = std::make_unique<T>(val);
  UpdateValue();
}
template <class T>
void ConfigVar<T>::SetGameConfigValue(T val) {
  game_config_value_ = std::make_unique<T>(val);
  UpdateValue();
}

extern std::map<std::string, ICommandVar*>* CmdVars;
extern std::map<std::string, IConfigVar*>* ConfigVars;

inline void AddConfigVar(IConfigVar* cv) {
  if (!ConfigVars) ConfigVars = new std::map<std::string, IConfigVar*>();
  ConfigVars->insert(std::pair<std::string, IConfigVar*>(cv->name(), cv));
}
inline void AddCommandVar(ICommandVar* cv) {
  if (!CmdVars) CmdVars = new std::map<std::string, ICommandVar*>();
  CmdVars->insert(std::pair<std::string, ICommandVar*>(cv->name(), cv));
}
void ParseLaunchArguments(int argc, char** argv,
                          const std::string& positional_help,
                          const std::vector<std::string>& positional_options);

template <typename T>
T* define_configvar(const char* name, T* default_value, const char* description,
                    const char* category, bool is_transient) {
  IConfigVar* cfgVar = new ConfigVar<T>(name, default_value, description,
                                        category, is_transient);
  AddConfigVar(cfgVar);
  return default_value;
}

template <typename T>
T* define_cmdvar(const char* name, T* default_value, const char* description) {
  ICommandVar* cmdVar = new CommandVar<T>(name, default_value, description);
  AddCommandVar(cmdVar);
  return default_value;
}

#define DEFINE_int32(name, default_value, description, category) \
  DEFINE_CVar(name, default_value, description, category, false, int32_t)

#define DEFINE_uint64(name, default_value, description, category) \
  DEFINE_CVar(name, default_value, description, category, false, uint64_t)

#define DEFINE_double(name, default_value, description, category) \
  DEFINE_CVar(name, default_value, description, category, false, double)

#define DEFINE_string(name, default_value, description, category) \
  DEFINE_CVar(name, default_value, description, category, false, std::string)

#define DEFINE_transient_string(name, default_value, description, category) \
  DEFINE_CVar(name, default_value, description, category, true, std::string)

#define DEFINE_bool(name, default_value, description, category) \
  DEFINE_CVar(name, default_value, description, category, false, bool)

#define DEFINE_CVar(name, default_value, description, category, is_transient, \
                    type)                                                     \
  namespace cvars {                                                           \
  type name = default_value;                                                  \
  }                                                                           \
  namespace cv {                                                              \
  static auto cv_##name = cvar::define_configvar(                             \
      #name, &cvars::name, description, category, is_transient);              \
  }

// CmdVars can only be strings for now, we don't need any others
#define CmdVar(name, default_value, description)             \
  namespace cvars {                                          \
  std::string name = default_value;                          \
  }                                                          \
  namespace cv {                                             \
  static auto cv_##name =                                    \
      cvar::define_cmdvar(#name, &cvars::name, description); \
  }

#define DECLARE_double(name) DECLARE_CVar(name, double)

#define DECLARE_bool(name) DECLARE_CVar(name, bool)

#define DECLARE_string(name) DECLARE_CVar(name, std::string)

#define DECLARE_int32(name) DECLARE_CVar(name, int32_t)

#define DECLARE_uint64(name) DECLARE_CVar(name, uint64_t)

#define DECLARE_CVar(name, type) \
  namespace cvars {              \
  extern type name;              \
  }

}  // namespace cvar

#endif  // XENIA_CVAR_H_
