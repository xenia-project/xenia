/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2019 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/xam/xam_module.h"

#include <vector>

#include "xenia/base/math.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/xam/xam_private.h"

namespace xe {
namespace kernel {
namespace xam {

std::atomic<int> xam_dialogs_shown_ = {0};

// FixMe(RodoMa92): Same hack as main_init_posix.cc:40
//  Force initialization before constructor calling, mimicking
//  Windows.
//  Ref:
//  https://reviews.llvm.org/D12689#243295
#ifdef XE_PLATFORM_LINUX
__attribute__((init_priority(101)))
#endif
static std::vector<xe::cpu::Export*>
    xam_exports(4096);

bool xeXamIsUIActive() { return xam_dialogs_shown_ > 0; }

XamModule::XamModule(Emulator* emulator, KernelState* kernel_state)
    : KernelModule(kernel_state, "xe:\\xam.xex"), loader_data_() {
  RegisterExportTable(export_resolver_);

  // Register all exported functions.
#define XE_MODULE_EXPORT_GROUP(m, n) \
  Register##n##Exports(export_resolver_, kernel_state_);
#include "xam_module_export_groups.inc"
#undef XE_MODULE_EXPORT_GROUP
}

xe::cpu::Export* RegisterExport_xam(xe::cpu::Export* export_entry) {
  assert_true(export_entry->ordinal < xam_exports.size());
  xam_exports[export_entry->ordinal] = export_entry;
  return export_entry;
}
// Build the export table used for resolution.
#include "xenia/kernel/util/export_table_pre.inc"
static constexpr xe::cpu::Export xam_export_table[] = {
#include "xenia/kernel/xam/xam_table.inc"
};
#include "xenia/kernel/util/export_table_post.inc"
void XamModule::RegisterExportTable(xe::cpu::ExportResolver* export_resolver) {
  assert_not_null(export_resolver);

  for (size_t i = 0; i < xe::countof(xam_export_table); ++i) {
    auto& export_entry = xam_export_table[i];
    assert_true(export_entry.ordinal < xam_exports.size());
    if (!xam_exports[export_entry.ordinal]) {
      xam_exports[export_entry.ordinal] =
          const_cast<xe::cpu::Export*>(&export_entry);
    }
  }
  export_resolver->RegisterTable("xam.xex", &xam_exports);
}

XamModule::~XamModule() {}

void XamModule::LoadLoaderData() {
  FILE* file = xe::filesystem::OpenFile(kXamModuleLoaderDataFileName, "rb");

  if (!file) {
    loader_data_.launch_data_present = false;
    return;
  }

  loader_data_.launch_data_present = true;

  auto string_read = [file]() {
    uint16_t string_size = 0;
    fread(&string_size, sizeof(string_size), 1, file);

    std::string result_string;
    result_string.resize(string_size);
    fread(result_string.data(), string_size, 1, file);
    return result_string;
  };

  loader_data_.host_path = string_read();
  loader_data_.launch_path = string_read();

  fread(&loader_data_.launch_flags, sizeof(loader_data_.launch_flags), 1, file);

  uint16_t launch_data_size = 0;
  fread(&launch_data_size, sizeof(launch_data_size), 1, file);

  if (launch_data_size > 0) {
    loader_data_.launch_data.resize(launch_data_size);
    fread(loader_data_.launch_data.data(), launch_data_size, 1, file);
  }

  fclose(file);
  // We read launch data. Let's remove it till next request.
  std::filesystem::remove(kXamModuleLoaderDataFileName);
}

void XamModule::SaveLoaderData() {
  FILE* file = xe::filesystem::OpenFile(kXamModuleLoaderDataFileName, "wb");

  if (!file) {
    return;
  }

  std::filesystem::path host_path = loader_data_.host_path;
  std::string launch_path = loader_data_.launch_path;

  auto remove_prefix = [&launch_path](std::string_view prefix) {
    if (launch_path.compare(0, prefix.length(), prefix) == 0) {
      launch_path = launch_path.substr(prefix.length());
    }
  };

  remove_prefix("game:\\");
  remove_prefix("d:\\");

  if (host_path.extension() == ".xex") {
    host_path.remove_filename();
    host_path = host_path / launch_path;
    launch_path = "";
  }

  const std::string host_path_as_string = xe::path_to_utf8(host_path);
  const uint16_t host_path_length =
      static_cast<uint16_t>(host_path_as_string.size());

  fwrite(&host_path_length, sizeof(host_path_length), 1, file);
  fwrite(host_path_as_string.c_str(), host_path_length, 1, file);

  const uint16_t launch_path_length = static_cast<uint16_t>(launch_path.size());
  fwrite(&launch_path_length, sizeof(launch_path_length), 1, file);
  fwrite(launch_path.c_str(), launch_path_length, 1, file);

  fwrite(&loader_data_.launch_flags, sizeof(loader_data_.launch_flags), 1,
         file);

  const uint16_t launch_data_size =
      static_cast<uint16_t>(loader_data_.launch_data.size());
  fwrite(&launch_data_size, sizeof(launch_data_size), 1, file);

  fwrite(loader_data_.launch_data.data(), launch_data_size, 1, file);

  fclose(file);
}

}  // namespace xam
}  // namespace kernel
}  // namespace xe
