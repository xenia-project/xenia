#ifndef XENIA_CONFIG_H_
#define XENIA_CONFIG_H_
#include <cpptoml/include/cpptoml.h>
#include <string>

class CommandVar {
 public:
  CommandVar(const char* name, const char* defaultValue,
             const char* description);
  std::string GetValue();
  std::string GetName() { return name_; };
  std::string GetDescription() { return description_; };
  bool GetBool();
  float GetFloat();
  int GetInt();
  void SetCommandLineValue(std::string val);

 protected:
  std::string name_;
  std::string defaultValue_;
  std::string commandLineValue_;
  std::string description_;
  bool _conversionDone = false;
  bool _valAsBool;
  float _valAsFloat;
  int _valAsInt;
};

class ConfigVar : public CommandVar {
 public:
  ConfigVar(const char* name, const char* defaultValue, const char* description,
            const char* category);
  std::string GetValue();
  std::string GetConfigValue();
  std::string GetCategory() { return category_; };
  void SetConfigValue(std::string val);
  void SetGameConfigValue(std::string val);

 private:
  std::string category_;
  std::string configValue_;
  std::string gameConfigValue_;
};

namespace config {
ConfigVar GetConfigVar(const char* name);
void ParseLaunchArguments(int argc, char** argv);
void SetupConfig(const std::wstring& content_path);
void LoadGameConfig(const std::wstring& config_folder,
                    const std::wstring& title_id);
}  // namespace config
#define LoadCVar(name) ConfigVar var_##name = config::GetConfigVar(#name);
#define CVar(name, defaultValue, description, category) \
  ConfigVar var_##name = ConfigVar(#name, #defaultValue, description, category);
#define CmdVar(name, defaultValue, description) \
  CommandVar var_##name = CommandVar(#name, #defaultValue, description);
#define CVar2(name, defaultValue, description, category) \
  namespace cv {                                               \
    static const ConfigVar var_##name =                                        ConfigVar(#name, #defaultValue, description, category);                           \
  }                                                                       \
  using cv::var_##name
#define LoadCVar2(name)        \
  namespace cv {               \
  extern ConfigVar var_##name; \
  }                            \
  using cv::var_##name
#endif  // XENIA_CONFIG_H_