#ifndef XENIA_CONFIG_H_
#define XENIA_CONFIG_H_
#include <cpptoml/include/cpptoml.h>
#include <string>

class CmdVar {
 public:
  CmdVar(const char* name, const char* defaultValue, const char* description);
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

class CVar : public CmdVar {
 public:
  CVar(const char* name, const char* defaultValue, const char* description,
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

void ParseLaunchArguments(int argc, char** argv);
void SetupConfig(const std::wstring& content_path);
void LoadGameConfig(const std::wstring& config_folder,
                    const std::wstring& title_id);
}  // namespace config

#endif  // XENIA_CONFIG_H_
