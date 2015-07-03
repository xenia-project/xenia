/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/objects/xuser_module.h"

#include "xenia/base/logging.h"
#include "xenia/cpu/processor.h"
#include "xenia/cpu/xex_module.h"
#include "xenia/emulator.h"
#include "xenia/kernel/objects/xfile.h"
#include "xenia/kernel/objects/xthread.h"

namespace xe {
namespace kernel {

using namespace xe::cpu;

XUserModule::XUserModule(KernelState* kernel_state, const char* path)
    : XModule(kernel_state, ModuleType::kUserModule, path) {}

XUserModule::~XUserModule() {}

X_STATUS XUserModule::LoadFromFile(std::string path) {
  X_STATUS result = X_STATUS_UNSUCCESSFUL;

  // Resolve the file to open.
  // TODO(benvanik): make this code shared?
  auto fs_entry = kernel_state()->file_system()->ResolvePath(path);
  if (!fs_entry) {
    XELOGE("File not found: %s", path.c_str());
    return X_STATUS_NO_SUCH_FILE;
  }

  // If the FS supports mapping, map the file in and load from that.
  if (fs_entry->can_map()) {
    // Map.
    auto mmap = fs_entry->OpenMapped(MappedMemory::Mode::kRead);
    if (!mmap) {
      return result;
    }

    // Load the module.
    result = LoadFromMemory(mmap->data(), mmap->size());
  } else {
    std::vector<uint8_t> buffer(fs_entry->size());

    // Open file for reading.
    object_ref<XFile> file;
    result =
        fs_entry->Open(kernel_state(), vfs::FileAccess::kGenericRead, &file);
    if (result) {
      return result;
    }

    // Read entire file into memory.
    // Ugh.
    size_t bytes_read = 0;
    result = file->Read(buffer.data(), buffer.size(), 0, &bytes_read);
    if (result) {
      return result;
    }

    // Load the module.
    result = LoadFromMemory(buffer.data(), bytes_read);
  }

  return result;
}

X_STATUS XUserModule::LoadFromMemory(const void* addr, const size_t length) {
  Processor* processor = kernel_state()->processor();

  // Prepare the module for execution.
  // Runtime takes ownership.
  auto xex_module = std::make_unique<XexModule>(processor, kernel_state());
  if (!xex_module->Load(name_, path_, addr, length)) {
    return X_STATUS_UNSUCCESSFUL;
  }
  processor_module_ = xex_module.get();
  if (!processor->AddModule(std::move(xex_module))) {
    return X_STATUS_UNSUCCESSFUL;
  }

  // Copy the xex2 header into guest memory.
  const xex2_header* header = this->xex_module()->xex_header();
  guest_xex_header_ = memory()->SystemHeapAlloc(header->header_size);

  uint8_t* xex_header_ptr = memory()->TranslateVirtual(guest_xex_header_);
  std::memcpy(xex_header_ptr, header, header->header_size);

  // Setup the loader data entry
  auto ldr_data =
      memory()->TranslateVirtual<X_LDR_DATA_TABLE_ENTRY*>(hmodule_ptr_);

  ldr_data->dll_base = 0;  // GetProcAddress will read this.
  ldr_data->xex_header_base = guest_xex_header_;

  // Cache some commonly used headers...
  this->xex_module()->GetOptHeader(XEX_HEADER_ENTRY_POINT, &entry_point_);
  this->xex_module()->GetOptHeader(XEX_HEADER_DEFAULT_STACK_SIZE, &stack_size_);

  OnLoad();

  return X_STATUS_SUCCESS;
}

uint32_t XUserModule::GetProcAddressByOrdinal(uint16_t ordinal) {
  return xex_module()->GetProcAddress(ordinal);
}

uint32_t XUserModule::GetProcAddressByName(const char* name) {
  return xex_module()->GetProcAddress(name);
}

X_STATUS XUserModule::GetSection(const char* name, uint32_t* out_section_data,
                                 uint32_t* out_section_size) {
  xex2_opt_resource_info* resource_header = nullptr;
  if (!XexModule::GetOptHeader(xex_header(), XEX_HEADER_RESOURCE_INFO,
                               &resource_header)) {
    // No resources.
    return X_STATUS_UNSUCCESSFUL;
  }

  uint32_t count = (resource_header->size - 4) / 16;
  for (uint32_t i = 0; i < count; i++) {
    auto& res = resource_header->resources[i];
    if (strcmp(name, res.name) == 0) {
      // Found!
      *out_section_data = res.address;
      *out_section_size = res.size;

      return X_STATUS_SUCCESS;
    }
  }

  return X_STATUS_UNSUCCESSFUL;
}

X_STATUS XUserModule::GetOptHeader(xe_xex2_header_keys key, void** out_ptr) {
  assert_not_null(out_ptr);

  bool ret = xex_module()->GetOptHeader(key, out_ptr);
  if (!ret) {
    return X_STATUS_NOT_FOUND;
  }

  return X_STATUS_SUCCESS;
}

X_STATUS XUserModule::GetOptHeader(xe_xex2_header_keys key,
                                   uint32_t* out_header_guest_ptr) {
  auto header = xex_module()->xex_header();
  if (!header) {
    return X_STATUS_UNSUCCESSFUL;
  }
  return GetOptHeader(memory()->virtual_membase(), header, key,
                      out_header_guest_ptr);
}

X_STATUS XUserModule::GetOptHeader(uint8_t* membase, const xex2_header* header,
                                   xe_xex2_header_keys key,
                                   uint32_t* out_header_guest_ptr) {
  assert_not_null(out_header_guest_ptr);
  uint32_t field_value = 0;
  bool field_found = false;
  for (uint32_t i = 0; i < header->header_count; i++) {
    auto& opt_header = header->headers[i];
    if (opt_header.key != key) {
      continue;
    }
    field_found = true;
    switch (opt_header.key & 0xFF) {
      case 0x00:
        // Return data stored in header value.
        field_value = opt_header.value;
        break;
      case 0x01:
        // Return pointer to data stored in header value.
        field_value = uint32_t((uint8_t*)&opt_header.value - membase);
        break;
      default:
        // Data stored at offset to header.
        field_value = uint32_t((uint8_t*)header - membase) + opt_header.offset;
        break;
    }
    break;
  }
  *out_header_guest_ptr = field_value;
  if (!field_found) {
    return X_STATUS_NOT_FOUND;
  }
  return X_STATUS_SUCCESS;
}

X_STATUS XUserModule::Launch(uint32_t flags) {
  XELOGI("Launching module...");
  Dump();

  // Create a thread to run in.
  auto thread = object_ref<XThread>(
      new XThread(kernel_state(), stack_size_, 0, entry_point_, 0, 0));

  X_STATUS result = thread->Create();
  if (XFAILED(result)) {
    XELOGE("Could not create launch thread: %.8X", result);
    return result;
  }

  // Wait until thread completes.
  thread->Wait(0, 0, 0, nullptr);

  return X_STATUS_SUCCESS;
}

void XUserModule::Dump() {
  xe::cpu::ExportResolver* export_resolver =
      kernel_state_->emulator()->export_resolver();
  auto header = xex_header();

  // TODO: Need to loop through the optional headers one-by-one.

  // XEX header.
  printf("Module %s:\n", path_.c_str());
  printf("    Module Flags: %.8X\n", (uint32_t)header->module_flags);

  // Security header
  auto security_info = xex_module()->xex_security_info();
  printf("Security Header:\n");
  printf("     Image Flags: %.8X\n", (uint32_t)security_info->image_flags);
  printf("    Load Address: %.8X\n", (uint32_t)security_info->load_address);
  printf("      Image Size: %.8X\n", (uint32_t)security_info->image_size);
  printf("    Export Table: %.8X\n", (uint32_t)security_info->export_table);

  // Optional headers
  printf("Optional Header Count: %d\n", (uint32_t)header->header_count);

  for (uint32_t i = 0; i < header->header_count; i++) {
    auto& opt_header = header->headers[i];

    // Stash a pointer (although this isn't used in every case)
    void* opt_header_ptr = (uint8_t*)header + opt_header.offset;
    switch (opt_header.key) {
      case XEX_HEADER_RESOURCE_INFO: {
        printf("  XEX_HEADER_RESOURCE_INFO (TODO):\n");
        auto opt_resource_info =
            reinterpret_cast<xex2_opt_resource_info*>(opt_header_ptr);

      } break;
      case XEX_HEADER_FILE_FORMAT_INFO: {
        printf("  XEX_HEADER_FILE_FORMAT_INFO (TODO):\n");
      } break;
      case XEX_HEADER_DELTA_PATCH_DESCRIPTOR: {
        printf("  XEX_HEADER_DELTA_PATCH_DESCRIPTOR (TODO):\n");
      } break;
      case XEX_HEADER_BOUNDING_PATH: {
        auto opt_bound_path =
            reinterpret_cast<xex2_opt_bound_path*>(opt_header_ptr);
        printf("  XEX_HEADER_BOUNDING_PATH: %s\n", opt_bound_path->path);
      } break;
      case XEX_HEADER_ORIGINAL_BASE_ADDRESS: {
        printf("  XEX_HEADER_ORIGINAL_BASE_ADDRESS: %.8X\n",
               (uint32_t)opt_header.value);
      } break;
      case XEX_HEADER_ENTRY_POINT: {
        printf("  XEX_HEADER_ENTRY_POINT: %.8X\n", (uint32_t)opt_header.value);
      } break;
      case XEX_HEADER_IMAGE_BASE_ADDRESS: {
        printf("  XEX_HEADER_IMAGE_BASE_ADDRESS: %.8X\n",
               (uint32_t)opt_header.value);
      } break;
      case XEX_HEADER_ORIGINAL_PE_NAME: {
        auto opt_pe_name =
            reinterpret_cast<xex2_opt_original_pe_name*>(opt_header_ptr);
        printf("  XEX_HEADER_ORIGINAL_PE_NAME: %s\n", opt_pe_name->name);
      } break;
      case XEX_HEADER_STATIC_LIBRARIES: {
        printf("  XEX_HEADER_STATIC_LIBRARIES:\n");
        auto opt_static_libraries =
            reinterpret_cast<const xex2_opt_static_libraries*>(opt_header_ptr);

        uint32_t count = (opt_static_libraries->size - 4) / 0x10;
        for (uint32_t i = 0; i < count; i++) {
          auto& library = opt_static_libraries->libraries[i];
          printf(
              "    %-8s : %d.%d.%d.%d\n", library.name,
              (uint16_t)library.version_major, (uint16_t)library.version_minor,
              (uint16_t)library.version_build, (uint16_t)library.version_qfe);
        }
      } break;
      case XEX_HEADER_TLS_INFO: {
        printf("  XEX_HEADER_TLS_INFO:\n");
        auto opt_tls_info =
            reinterpret_cast<const xex2_opt_tls_info*>(opt_header_ptr);

        printf("          Slot Count: %d\n",
               (uint32_t)opt_tls_info->slot_count);
        printf("    Raw Data Address: %.8X\n",
               (uint32_t)opt_tls_info->raw_data_address);
        printf("           Data Size: %d\n", (uint32_t)opt_tls_info->data_size);
        printf("       Raw Data Size: %d\n",
               (uint32_t)opt_tls_info->raw_data_size);
      } break;
      case XEX_HEADER_DEFAULT_STACK_SIZE: {
        printf("  XEX_HEADER_DEFAULT_STACK_SIZE: %d\n",
               (uint32_t)opt_header.value);
      } break;
      case XEX_HEADER_DEFAULT_FILESYSTEM_CACHE_SIZE: {
        printf("  XEX_HEADER_DEFAULT_FILESYSTEM_CACHE_SIZE: %d\n",
               (uint32_t)opt_header.value);
      } break;
      case XEX_HEADER_DEFAULT_HEAP_SIZE: {
        printf("  XEX_HEADER_DEFAULT_HEAP_SIZE: %d\n",
               (uint32_t)opt_header.value);
      } break;
      case XEX_HEADER_PAGE_HEAP_SIZE_AND_FLAGS: {
        printf("  XEX_HEADER_PAGE_HEAP_SIZE_AND_FLAGS (TODO):\n");
      } break;
      case XEX_HEADER_SYSTEM_FLAGS: {
        printf("  XEX_HEADER_SYSTEM_FLAGS (TODO):\n");
      } break;
      case XEX_HEADER_EXECUTION_INFO: {
        printf("  XEX_HEADER_EXECUTION_INFO (TODO):\n");
      } break;
      case XEX_HEADER_TITLE_WORKSPACE_SIZE: {
        printf("  XEX_HEADER_TITLE_WORKSPACE_SIZE (TODO):\n");
      } break;
      case XEX_HEADER_GAME_RATINGS: {
        printf("  XEX_HEADER_GAME_RATINGS (TODO):\n");
      } break;
      case XEX_HEADER_LAN_KEY: {
        printf("  XEX_HEADER_LAN_KEY (TODO):\n");
      } break;
      case XEX_HEADER_XBOX360_LOGO: {
        printf("  XEX_HEADER_XBOX360_LOGO (TODO):\n");
      } break;
      case XEX_HEADER_MULTIDISC_MEDIA_IDS: {
        printf("  XEX_HEADER_MULTIDISC_MEDIA_IDS (TODO):\n");
      } break;
      case XEX_HEADER_ALTERNATE_TITLE_IDS: {
        printf("  XEX_HEADER_ALTERNATE_TITLE_IDS (TODO):\n");
      } break;
      case XEX_HEADER_ADDITIONAL_TITLE_MEMORY: {
        printf("  XEX_HEADER_ADDITIONAL_TITLE_MEMORY (TODO):\n");
      } break;
      case XEX_HEADER_EXPORTS_BY_NAME: {
        printf("  XEX_HEADER_EXPORTS_BY_NAME (TODO):\n");
      } break;
    }
  }
}

}  // namespace kernel
}  // namespace xe
