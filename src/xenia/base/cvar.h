#ifndef XENIA_CVAR_H_
#define XENIA_CVAR_H_
#include <map>
#include <string>
#include "cpptoml/include/cpptoml.h"
#include "cxxopts/include/cxxopts.hpp"
#include "xenia/base/string_util.h"
namespace cvar {

class ICommandVar {
 public:
  virtual ~ICommandVar() = default;
  virtual std::string GetName() = 0;
  virtual std::string GetDescription() = 0;
  virtual void UpdateValue() = 0;
  virtual void AddToLaunchOptions(cxxopts::Options* options) = 0;
  virtual void LoadFromLaunchOptions(cxxopts::ParseResult* result) = 0;
};

class IConfigVar : virtual public ICommandVar {
 public:
  virtual std::string GetCategory() = 0;
  virtual std::string GetConfigValue() = 0;
  virtual void LoadConfigValue(std::shared_ptr<cpptoml::base> result) = 0;
  virtual void LoadGameConfigValue(std::shared_ptr<cpptoml::base> result) = 0;
};

template <class T>
class CommandVar : virtual public ICommandVar {
 public:
  CommandVar<T>(const char* name, T* defaultValue, const char* description);
  std::string GetName() override;
  std::string GetDescription() override;
  void AddToLaunchOptions(cxxopts::Options* options) override;
  void LoadFromLaunchOptions(cxxopts::ParseResult* result) override;
  T* GetCurrentValue() { return currentValue_; }

 protected:
  std::string name_;
  T defaultValue_;
  T* currentValue_;
  std::unique_ptr<T> commandLineValue_;
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
  ConfigVar<T>(const char* name, T* defaultValue, const char* description,
               const char* category);
  std::string GetConfigValue() override;
  std::string GetCategory() override;
  void AddToLaunchOptions(cxxopts::Options* options) override;
  void LoadConfigValue(std::shared_ptr<cpptoml::base> result) override;
  void LoadGameConfigValue(std::shared_ptr<cpptoml::base> result) override;
  void SetConfigValue(T val);
  void SetGameConfigValue(T val);

 private:
  std::string category_;
  std::unique_ptr<T> configValue_ = nullptr;
  std::unique_ptr<T> gameConfigValue_ = nullptr;
  void UpdateValue() override;
};

#pragma warning(pop)
template <class T>
std::string CommandVar<T>::GetName() {
  return name_;
}
template <class T>
std::string CommandVar<T>::GetDescription() {
  return description_;
}
template <class T>
void CommandVar<T>::AddToLaunchOptions(cxxopts::Options* options) {
  options->add_options()(this->name_, this->description_, cxxopts::value<T>());
}
template <class T>
void ConfigVar<T>::AddToLaunchOptions(cxxopts::Options* options) {
  options->add_options(category_)(this->name_, this->description_,
                                  cxxopts::value<T>());
}
template <class T>
void CommandVar<T>::LoadFromLaunchOptions(cxxopts::ParseResult* result) {
  T value = (*result)[this->name_].template as<T>();
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
CommandVar<T>::CommandVar(const char* name, T* defaultValue,
                          const char* description)
    : name_(name),
      defaultValue_(*defaultValue),
      description_(description),
      currentValue_(defaultValue) {}

template <class T>
ConfigVar<T>::ConfigVar(const char* name, T* defaultValue,
                        const char* description, const char* category)
    : CommandVar<T>(name, defaultValue, description), category_(category) {}

template <class T>
void CommandVar<T>::UpdateValue() {
  if (this->commandLineValue_) return this->SetValue(*this->commandLineValue_);
  return this->SetValue(defaultValue_);
}
template <class T>
void ConfigVar<T>::UpdateValue() {
  if (this->commandLineValue_) return this->SetValue(*this->commandLineValue_);
  if (this->gameConfigValue_) return this->SetValue(*this->gameConfigValue_);
  if (this->configValue_) return this->SetValue(*this->configValue_);
  return this->SetValue(this->defaultValue_);
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
  return "\"" + val + "\"";
}

template <class T>
std::string CommandVar<T>::ToString(T val) {
  return std::to_string(val);
}

template <class T>
void CommandVar<T>::SetValue(T val) {
  *currentValue_ = val;
}
template <class T>
std::string ConfigVar<T>::GetCategory() {
  return category_;
}
template <class T>
std::string ConfigVar<T>::GetConfigValue() {
  if (this->configValue_) return this->ToString(*this->configValue_);
  return this->ToString(this->defaultValue_);
}
template <class T>
void CommandVar<T>::SetCommandLineValue(const T val) {
  this->commandLineValue_ = std::make_unique<T>(val);
  this->UpdateValue();
}
template <class T>
void ConfigVar<T>::SetConfigValue(T val) {
  this->configValue_ = std::make_unique<T>(val);
  this->UpdateValue();
}
template <class T>
void ConfigVar<T>::SetGameConfigValue(T val) {
  this->gameConfigValue_ = std::make_unique<T>(val);
  this->UpdateValue();
}

extern std::map<std::string, ICommandVar*>* CmdVars;
extern std::map<std::string, IConfigVar*>* ConfigVars;

inline void AddConfigVar(IConfigVar* cv) {
  if (!ConfigVars) ConfigVars = new std::map<std::string, IConfigVar*>();
  ConfigVars->insert(std::pair<std::string, IConfigVar*>(cv->GetName(), cv));
}
inline void AddCommandVar(ICommandVar* cv) {
  if (!CmdVars) CmdVars = new std::map<std::string, ICommandVar*>();
  CmdVars->insert(std::pair<std::string, ICommandVar*>(cv->GetName(), cv));
}
void ParseLaunchArguments(int argc, char** argv);

template <typename T>
T* define_configvar(const char* name, T* defaultValue, const char* description,
                    const char* category) {
  IConfigVar* cfgVar =
      new ConfigVar<T>(name, defaultValue, description, category);
  AddConfigVar(cfgVar);
  return defaultValue;
}

template <typename T>
T* define_cmdvar(const char* name, T* defaultValue, const char* description) {
  ICommandVar* cmdVar = new CommandVar<T>(name, defaultValue, description);
  AddCommandVar(cmdVar);
  return defaultValue;
}
#define DEFINE_double(name, defaultValue, description, category) \
  DEFINE_CVar(name, defaultValue, description, category, double)

#define DEFINE_int32(name, defaultValue, description, category) \
  DEFINE_CVar(name, defaultValue, description, category, int32_t)

#define DEFINE_uint64(name, defaultValue, description, category) \
  DEFINE_CVar(name, defaultValue, description, category, uint64_t)

#define DEFINE_string(name, defaultValue, description, category) \
  DEFINE_CVar(name, defaultValue, description, category, std::string)

#define DEFINE_bool(name, defaultValue, description, category) \
  DEFINE_CVar(name, defaultValue, description, category, bool)

#define DEFINE_CVar(name, defaultValue, description, category, type)      \
  namespace cvars {                                                       \
  type name = defaultValue;                                               \
  }                                                                       \
  namespace cv {                                                          \
  static auto cv_##name =                                                 \
      cvar::define_configvar(#name, &cvars::name, description, category); \
  }

// CmdVars can only be strings for now, we don't need any others
#define CmdVar(name, defaultValue, description)              \
  namespace cvars {                                          \
  std::string name = defaultValue;                           \
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
