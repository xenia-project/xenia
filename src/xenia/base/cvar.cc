#include "cvar.h"

namespace cvar {
cxxopts::Options options("xenia", "Xbox 360 Emulator");
std::map<std::string, ICommandVar*>* CmdVars;
std::map<std::string, IConfigVar*>* ConfigVars;

void PrintHelpAndExit() {
  std::cout << options.help({""}) << std::endl;
  std::cout << "For the full list of command line arguments, see xenia.cfg."
            << std::endl;
  exit(0);
}

void ParseLaunchArguments(int argc, char** argv) {
  options.add_options()("help", "Prints help and exit.");
  if (!CmdVars) CmdVars = new std::map<std::string, ICommandVar*>();
  if (!ConfigVars) ConfigVars = new std::map<std::string, IConfigVar*>();
  for (auto& it : *CmdVars) {
    auto cmdVar = it.second;
    cmdVar->AddToLaunchOptions(&options);
  }
  std::vector<IConfigVar*> vars;
  for (const auto& s : *ConfigVars) vars.push_back(s.second);

  for (auto& it : *ConfigVars) {
    auto configVar = it.second;
    configVar->AddToLaunchOptions(&options);
  }
  try {
    options.positional_help("[Path to .iso/.xex]");
    options.parse_positional({"target"});

    auto result = options.parse(argc, argv);
    if (result.count("help")) {
      PrintHelpAndExit();
    }
    for (auto& it : *CmdVars) {
      auto cmdVar = static_cast<ICommandVar*>(it.second);
      if (result.count(cmdVar->GetName())) {
        cmdVar->LoadFromLaunchOptions(&result);
      }
    }
    for (auto& it : *ConfigVars) {
      auto configVar = static_cast<IConfigVar*>(it.second);
      if (result.count(configVar->GetName())) {
        configVar->LoadFromLaunchOptions(&result);
      }
    }
  } catch (const cxxopts::OptionException& e) {
    std::cout << e.what() << std::endl;
    PrintHelpAndExit();
  }
}

}  // namespace cvar
