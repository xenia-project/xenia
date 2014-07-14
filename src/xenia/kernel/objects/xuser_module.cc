/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/kernel/objects/xuser_module.h>

#include <xenia/emulator.h>
#include <xenia/cpu/cpu.h>
#include <xenia/kernel/objects/xfile.h>
#include <xenia/kernel/objects/xthread.h>


using namespace xe;
using namespace xe::cpu;
using namespace xe::kernel;


XUserModule::XUserModule(KernelState* kernel_state, const char* path) :
    XModule(kernel_state, path),
    xex_(NULL) {
}

XUserModule::~XUserModule() {
  xe_xex2_release(xex_);
}

xe_xex2_ref XUserModule::xex() {
  return xe_xex2_retain(xex_);
}

const xe_xex2_header_t* XUserModule::xex_header() {
  return xe_xex2_get_header(xex_);
}

X_STATUS XUserModule::LoadFromFile(const char* path) {
  X_STATUS result = X_STATUS_UNSUCCESSFUL;
  XFile* file = NULL;
  uint8_t* buffer = 0;

  // Resolve the file to open.
  // TODO(benvanik): make this code shared?
  fs::Entry* fs_entry = kernel_state()->file_system()->ResolvePath(path);
  if (!fs_entry) {
    XELOGE("File not found: %s", path);
    result = X_STATUS_NO_SUCH_FILE;
    XEFAIL();
  }
  if (fs_entry->type() != fs::Entry::kTypeFile) {
    XELOGE("Invalid file type: %s", path);
    result = X_STATUS_NO_SUCH_FILE;
    XEFAIL();
  }

  // If the FS supports mapping, map the file in and load from that.
  if (fs_entry->can_map()) {
    // Map.
    fs::MemoryMapping* mmap = fs_entry->CreateMemoryMapping(kXEFileModeRead, 0, 0);
    XEEXPECTNOTNULL(mmap);

    // Load the module.
    result = LoadFromMemory(mmap->address(), mmap->length());

    // Unmap memory and cleanup.
    delete mmap;
  } else {
    XFileInfo file_info;
    result = fs_entry->QueryInfo(&file_info);
    XEEXPECTZERO(result);

    size_t buffer_length = file_info.file_length;
    buffer = (uint8_t*)xe_malloc(buffer_length);

    // Open file for reading.
    result = fs_entry->Open(kernel_state(), kXEFileModeRead, false, &file);
    XEEXPECTZERO(result);

    // Read entire file into memory.
    // Ugh.
    size_t bytes_read = 0;
    result = file->Read(buffer, buffer_length, 0, &bytes_read);
    XEEXPECTZERO(result);

    // Load the module.
    result = LoadFromMemory(buffer, bytes_read);
  }

XECLEANUP:
  if (buffer) {
    xe_free(buffer);
  }
  if (file) {
    file->Release();
  }
  delete fs_entry;
  return result;
}

X_STATUS XUserModule::LoadFromMemory(const void* addr, const size_t length) {
  Processor* processor = kernel_state()->processor();
  XenonRuntime* runtime = processor->runtime();

  // Load the XEX into memory and decrypt.
  xe_xex2_options_t xex_options;
  xe_zero_struct(&xex_options, sizeof(xex_options));
  xex_ = xe_xex2_load(kernel_state()->memory(), addr, length, xex_options);
  if (!xex_) {
    return X_STATUS_UNSUCCESSFUL;
  }

  // Prepare the module for execution.
  // Runtime takes ownership.
  auto xex_module = std::make_unique<XexModule>(runtime);
  if (xex_module->Load(name_, path_, xex_)) {
    return X_STATUS_UNSUCCESSFUL;
  }
  if (runtime->AddModule(std::move(xex_module))) {
    return X_STATUS_UNSUCCESSFUL;
  }

  return X_STATUS_SUCCESS;
}

void* XUserModule::GetProcAddressByOrdinal(uint16_t ordinal) {
  // TODO(benvanik): check export tables.
  XELOGE("GetProcAddressByOrdinal not implemented");
  return NULL;
}

X_STATUS XUserModule::GetSection(
    const char* name,
    uint32_t* out_section_data, uint32_t* out_section_size) {
  auto header = xe_xex2_get_header(xex_);
  for (size_t n = 0; n < header->resource_info_count; n++) {
    auto& res = header->resource_infos[n];
    if (xestrcmpa(name, res.name) == 0) {
      // Found!
      *out_section_data = res.address;
      *out_section_size = res.size;
      return X_STATUS_SUCCESS;
    }
  }
  return X_STATUS_UNSUCCESSFUL;
}

X_STATUS XUserModule::Launch(uint32_t flags) {
  const xe_xex2_header_t* header = xex_header();

  XELOGI("Launching module...");

  Dump();

  // Create a thread to run in.
  XThread* thread = new XThread(
      kernel_state(),
      header->exe_stack_size,
      0,
      header->exe_entry_point, NULL,
      0);

  X_STATUS result = thread->Create();
  if (XFAILED(result)) {
    XELOGE("Could not create launch thread: %.8X", result);
    return result;
  }

  // Wait until thread completes.
  thread->Wait(0, 0, 0, NULL);

  thread->Release();

  return X_STATUS_SUCCESS;
}

void XUserModule::Dump() {
  ExportResolver* export_resolver =
      kernel_state_->emulator()->export_resolver();
  const xe_xex2_header_t* header = xe_xex2_get_header(xex_);

  // XEX info.
  printf("Module %s:\n\n", path_);
  printf("    Module Flags: %.8X\n", header->module_flags);
  printf("    System Flags: %.8X\n", header->system_flags);
  printf("\n");
  printf("         Address: %.8X\n", header->exe_address);
  printf("     Entry Point: %.8X\n", header->exe_entry_point);
  printf("      Stack Size: %.8X\n", header->exe_stack_size);
  printf("       Heap Size: %.8X\n", header->exe_heap_size);
  printf("\n");
  printf("  Execution Info:\n");
  printf("        Media ID: %.8X\n", header->execution_info.media_id);
  printf("         Version: %d.%d.%d.%d\n",
         header->execution_info.version.major,
         header->execution_info.version.minor,
         header->execution_info.version.build,
         header->execution_info.version.qfe);
  printf("    Base Version: %d.%d.%d.%d\n",
         header->execution_info.base_version.major,
         header->execution_info.base_version.minor,
         header->execution_info.base_version.build,
         header->execution_info.base_version.qfe);
  printf("        Title ID: %.8X\n", header->execution_info.title_id);
  printf("        Platform: %.8X\n", header->execution_info.platform);
  printf("      Exec Table: %.8X\n", header->execution_info.executable_table);
  printf("     Disc Number: %d\n", header->execution_info.disc_number);
  printf("      Disc Count: %d\n", header->execution_info.disc_count);
  printf("     Savegame ID: %.8X\n", header->execution_info.savegame_id);
  printf("\n");
  printf("  Loader Info:\n");
  printf("     Image Flags: %.8X\n", header->loader_info.image_flags);
  printf("    Game Regions: %.8X\n", header->loader_info.game_regions);
  printf("     Media Flags: %.8X\n", header->loader_info.media_flags);
  printf("\n");
  printf("  TLS Info:\n");
  printf("      Slot Count: %d\n", header->tls_info.slot_count);
  printf("       Data Size: %db\n", header->tls_info.data_size);
  printf("         Address: %.8X, %db\n", header->tls_info.raw_data_address,
                                          header->tls_info.raw_data_size);
  printf("\n");
  printf("  Headers:\n");
  for (size_t n = 0; n < header->header_count; n++) {
    const xe_xex2_opt_header_t* opt_header = &header->headers[n];
    printf("    %.8X (%.8X, %4db) %.8X = %11d\n",
           opt_header->key, opt_header->offset, opt_header->length,
           opt_header->value, opt_header->value);
  }
  printf("\n");

  // Resources.
  printf("Resources:\n");
  for (size_t n = 0; n < header->resource_info_count; n++) {
    auto& res = header->resource_infos[n];
    printf("  %-8s %.8X-%.8X, %db\n",
           res.name, res.address, res.address + res.size, res.size);
  }
  printf("\n");

  // Section info.
  printf("Sections:\n");
  for (size_t n = 0, i = 0; n < header->section_count; n++) {
    const xe_xex2_section_t* section = &header->sections[n];
    const char* type = "UNKNOWN";
    switch (section->info.type) {
    case XEX_SECTION_CODE:
      type = "CODE   ";
      break;
    case XEX_SECTION_DATA:
      type = "RWDATA ";
      break;
    case XEX_SECTION_READONLY_DATA:
      type = "RODATA ";
      break;
    }
    const size_t start_address =
        header->exe_address + (i * xe_xex2_section_length);
    const size_t end_address =
        start_address + (section->info.page_count * xe_xex2_section_length);
    printf("  %3d %s %3d pages    %.8X - %.8X (%d bytes)\n",
           (int)n, type, section->info.page_count, (int)start_address,
           (int)end_address, section->info.page_count * xe_xex2_section_length);
    i += section->info.page_count;
  }
  printf("\n");

  // Static libraries.
  printf("Static Libraries:\n");
  for (size_t n = 0; n < header->static_library_count; n++) {
    const xe_xex2_static_library_t *library = &header->static_libraries[n];
    printf("  %-8s : %d.%d.%d.%d\n",
           library->name, library->major,
           library->minor, library->build, library->qfe);
  }
  printf("\n");

  // Imports.
  printf("Imports:\n");
  for (size_t n = 0; n < header->import_library_count; n++) {
    const xe_xex2_import_library_t* library = &header->import_libraries[n];

    xe_xex2_import_info_t* import_infos;
    size_t import_info_count;
    if (!xe_xex2_get_import_infos(xex_, library,
                                  &import_infos, &import_info_count)) {
      printf(" %s - %d imports\n", library->name, (int)import_info_count);
      printf("   Version: %d.%d.%d.%d\n",
             library->version.major, library->version.minor,
             library->version.build, library->version.qfe);
      printf("   Min Version: %d.%d.%d.%d\n",
             library->min_version.major, library->min_version.minor,
             library->min_version.build, library->min_version.qfe);
      printf("\n");

      // Counts.
      int known_count = 0;
      int unknown_count = 0;
      int impl_count = 0;
      int unimpl_count = 0;
      for (size_t m = 0; m < import_info_count; m++) {
        const xe_xex2_import_info_t* info = &import_infos[m];
        KernelExport* kernel_export =
            export_resolver->GetExportByOrdinal(library->name, info->ordinal);
        if (kernel_export) {
          known_count++;
          if (kernel_export->is_implemented) {
            impl_count++;
          } else {
            unimpl_count++;
          }
        } else {
          unknown_count++;
          unimpl_count++;
        }
      }
      printf("         Total: %4u\n", import_info_count);
      printf("         Known:  %3d%% (%d known, %d unknown)\n",
             (int)(known_count / (float)import_info_count * 100.0f),
             known_count, unknown_count);
      printf("   Implemented:  %3d%% (%d implemented, %d unimplemented)\n",
             (int)(impl_count / (float)import_info_count * 100.0f),
             impl_count, unimpl_count);
      printf("\n");

      // Listing.
      for (size_t m = 0; m < import_info_count; m++) {
        const xe_xex2_import_info_t* info = &import_infos[m];
        KernelExport* kernel_export = export_resolver->GetExportByOrdinal(
            library->name, info->ordinal);
        const char *name = "UNKNOWN";
        bool implemented = false;
        if (kernel_export) {
          name = kernel_export->name;
          implemented = kernel_export->is_implemented;
        }
        if (kernel_export && kernel_export->type == KernelExport::Variable) {
          printf("   V %.8X          %.3X (%3d) %s %s\n",
                 info->value_address, info->ordinal, info->ordinal,
                 implemented ? "  " : "!!", name);
        } else if (info->thunk_address) {
          printf("   F %.8X %.8X %.3X (%3d) %s %s\n",
                 info->value_address, info->thunk_address, info->ordinal,
                 info->ordinal, implemented ? "  " : "!!", name);

        }
      }
    }

    printf("\n");
  }

  // Exports.
  printf("Exports:\n");
  printf("  TODO\n");
  printf("\n");
}
