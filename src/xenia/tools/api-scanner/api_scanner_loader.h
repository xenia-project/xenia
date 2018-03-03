/**
 ******************************************************************************
 * api-scanner - Scan for API imports from a packaged 360 game                *
 ******************************************************************************
 * Copyright 2015 x1nixmzeng. All rights reserved.                            *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <vector>

#include "xenia/base/main.h"
#include "xenia/base/math.h"
#include "xenia/base/string.h"
#include "xenia/cpu/export_resolver.h"
#include "xenia/kernel/fs/filesystem.h"
#include "xenia/kernel/objects/xfile.h"
#include "xenia/kernel/util/xex2.h"
#include "xenia/memory.h"
#include "xenia/xbox.h"

namespace xe {
namespace tools {

class apiscanner_logger {
 public:
  enum LogType { LT_WARNING, LT_ERROR };

  void operator()(const LogType type, const char* szMessage);
};

class apiscanner_loader {
 private:
  kernel::fs::FileSystem file_system;
  apiscanner_logger log;
  std::unique_ptr<Memory> memory_;
  std::unique_ptr<xe::cpu::ExportResolver> export_resolver;

 public:
  apiscanner_loader();
  ~apiscanner_loader();

  bool LoadTitleImports(const std::wstring& target);

  struct title {
    uint32_t title_id;
    std::vector<std::string> imports;
  };

  const std::vector<title>& GetAllTitles() const { return loaded_titles; }

 private:
  std::vector<title> loaded_titles;

  bool ReadTarget();
  bool ExtractImports(const void* addr, const size_t length, title& info);
};

}  // namespace tools
}  // namespace xe
