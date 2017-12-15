/**
 ******************************************************************************
 * api-scanner - Scan for API imports from a packaged 360 game                *
 ******************************************************************************
 * Copyright 2015 x1nixmzeng. All rights reserved.                            *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "api_scanner_loader.h"
#include "xenia/kernel/xam_module.h"
#include "xenia/kernel/xboxkrnl_module.h"

namespace xe {
namespace tools {

void apiscanner_logger::operator()(const LogType type, const char* szMessage) {
  switch (type) {
    case LT_WARNING:
      fprintf(stderr, "[W] %s\n", szMessage);
      break;
    case LT_ERROR:
      fprintf(stderr, "[!] %s\n", szMessage);
      break;
    default:
      break;
  }
}

apiscanner_loader::apiscanner_loader()
    : export_resolver(nullptr), memory_(nullptr) {
  export_resolver = std::make_unique<xe::cpu::ExportResolver>();

  kernel::XamModule::RegisterExportTable(export_resolver.get());
  kernel::XboxkrnlModule::RegisterExportTable(export_resolver.get());

  memory_ = std::make_unique<Memory>();
  memory_->Initialize();
}

apiscanner_loader::~apiscanner_loader() {
  if (export_resolver != nullptr) {
    export_resolver.reset();
  }

  if (memory_ != nullptr) {
    memory_.reset();
  }
}

bool apiscanner_loader::LoadTitleImports(const std::wstring& target) {
  auto type(file_system.InferType(target));
  int result = file_system.InitializeFromPath(type, target);
  if (result) {
    log(log.LT_ERROR, "Could not load target");
    return false;
  }

  return ReadTarget();
}

bool apiscanner_loader::ReadTarget() {
  // XXX Do a wildcard search for all xex files?
  const char path[] = "game:\\default.xex";

  kernel::XFile* file(nullptr);
  bool read_result(false);
  auto fs_entry = file_system.ResolvePath(path);
  if (!fs_entry) {
    log(log.LT_WARNING, "Could not resolve xex path");
    return false;
  }

  // If the FS supports mapping, map the file in and load from that.
  if (fs_entry->can_map()) {
    auto mmap = fs_entry->CreateMemoryMapping(kernel::fs::Mode::READ, 0, 0);
    if (!mmap) {
      if (file) {
        file->Release();
      }
      log(log.LT_WARNING, "Could not map filesystem");
      return false;
    }

    title res;
    read_result = ExtractImports(mmap->address(), mmap->length(), res);
    if (read_result) {
      loaded_titles.push_back(res);
    }
  } else {
    kernel::XFileInfo file_info;
    if (fs_entry->QueryInfo(&file_info)) {
      if (file) {
        file->Release();
      }
      log(log.LT_WARNING, "Could not read xex attributes");
      return false;
    }

    // Load into memory
    std::vector<uint8_t> buffer(file_info.file_length);

    // XXX No kernel state again
    int result = file_system.Open(std::move(fs_entry), nullptr,
                                  kernel::fs::Mode::READ, false, &file);
    if (result) {
      if (file) {
        file->Release();
      }
      log(log.LT_WARNING, "Could not open xex file");
      return false;
    }

    size_t bytes_read = 0;
    result = file->Read(buffer.data(), buffer.size(), 0, &bytes_read);
    if (result) {
      if (file) {
        file->Release();
      }
      log(log.LT_ERROR, "Could not read xex data");
      return false;
    }

    title res;
    read_result = ExtractImports(buffer.data(), bytes_read, res);
    if (read_result) {
      loaded_titles.push_back(res);
    }
  }

  if (file) {
    file->Release();
  }

  return read_result;
}

bool apiscanner_loader::ExtractImports(const void* addr, const size_t length,
                                       title& info) {
  // Load the XEX into memory and decrypt.
  xe_xex2_options_t xex_options = {0};
  xe_xex2_ref xex_(xe_xex2_load(memory_.get(), addr, length, xex_options));
  if (!xex_) {
    log(log.LT_ERROR, "Failed to parse xex file");
    return false;
  }

  const xe_xex2_header_t* header = xe_xex2_get_header(xex_);

  info.title_id = header->execution_info.title_id;

  // XXX Copy out library versions?
  for (size_t n = 0; n < header->import_library_count; n++) {
    const xe_xex2_import_library_t* library = &header->import_libraries[n];

    xe_xex2_import_info_t* import_infos;
    size_t import_info_count;
    if (!xe_xex2_get_import_infos(xex_, library, &import_infos,
                                  &import_info_count)) {
      for (size_t m = 0; m < import_info_count; m++) {
        const xe_xex2_import_info_t* import_info = &import_infos[m];

        auto kernel_export = export_resolver->GetExportByOrdinal(
            library->name, import_info->ordinal);

        if ((kernel_export &&
             kernel_export->type == xe::cpu::KernelExport::Variable) ||
            import_info->thunk_address) {
          const std::string named(kernel_export ? kernel_export->name : "");
          info.imports.push_back(named);
        }
      }
    }
  }

  xe_xex2_dealloc(xex_);
  std::sort(info.imports.begin(), info.imports.end());

  return true;
}

}  // namespace tools
}  // namespace xe
