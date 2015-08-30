/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/objects/xuser_module.h"

#include <vector>

#include "xenia/base/logging.h"
#include "xenia/cpu/elf_module.h"
#include "xenia/cpu/processor.h"
#include "xenia/cpu/xex_module.h"
#include "xenia/emulator.h"
#include "xenia/kernel/objects/xfile.h"
#include "xenia/kernel/objects/xthread.h"

namespace xe {
namespace kernel {

XUserModule::XUserModule(KernelState* kernel_state, const char* path)
    : XModule(kernel_state, ModuleType::kUserModule, path) {}

XUserModule::~XUserModule() { Unload(); }

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
  auto processor = kernel_state()->processor();

  auto magic = xe::load_and_swap<uint32_t>(addr);
  if (magic == 'XEX2') {
    module_format_ = kModuleFormatXex;
  } else if (magic == 0x7F454C46 /* 0x7F 'ELF' */) {
    module_format_ = kModuleFormatElf;
  } else {
    XELOGE("Unknown module magic: %.8X", magic);
    return X_STATUS_NOT_IMPLEMENTED;
  }

  if (module_format_ == kModuleFormatXex) {
    // Prepare the module for execution.
    // Runtime takes ownership.
    auto xex_module =
        std::make_unique<cpu::XexModule>(processor, kernel_state());
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
    this->xex_module()->GetOptHeader(XEX_HEADER_DEFAULT_STACK_SIZE,
                                     &stack_size_);
    dll_module_ = !!(header->module_flags & XEX_MODULE_DLL_MODULE);
  } else if (module_format_ == kModuleFormatElf) {
    auto elf_module =
        std::make_unique<cpu::ElfModule>(processor, kernel_state());
    if (!elf_module->Load(name_, path_, addr, length)) {
      return X_STATUS_UNSUCCESSFUL;
    }

    entry_point_ = elf_module->entry_point();
    stack_size_ = 1024 * 1024;  // 1 MB
    dll_module_ = false;        // Hardcoded not a DLL (for now)

    processor_module_ = elf_module.get();
    if (!processor->AddModule(std::move(elf_module))) {
      return X_STATUS_UNSUCCESSFUL;
    }
  }

  OnLoad();

  return X_STATUS_SUCCESS;
}

X_STATUS XUserModule::Unload() {
  if (!xex_module()->loaded()) {
    // Quick abort.
    return X_STATUS_SUCCESS;
  }

  if (xex_module()->Unload()) {
    OnUnload();
    return X_STATUS_SUCCESS;
  }

  return X_STATUS_UNSUCCESSFUL;
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
  if (!cpu::XexModule::GetOptHeader(xex_header(), XEX_HEADER_RESOURCE_INFO,
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

  if (module_format_ == kModuleFormatElf) {
    // Quick die.
    return X_STATUS_UNSUCCESSFUL;
  }

  bool ret = xex_module()->GetOptHeader(key, out_ptr);
  if (!ret) {
    return X_STATUS_NOT_FOUND;
  }

  return X_STATUS_SUCCESS;
}

X_STATUS XUserModule::GetOptHeader(xe_xex2_header_keys key,
                                   uint32_t* out_header_guest_ptr) {
  if (module_format_ == kModuleFormatElf) {
    // Quick die.
    return X_STATUS_UNSUCCESSFUL;
  }

  auto header =
      memory()->TranslateVirtual<const xex2_header*>(guest_xex_header_);
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
        field_value = static_cast<uint32_t>(
            reinterpret_cast<const uint8_t*>(&opt_header.value) - membase);
        break;
      default:
        // Data stored at offset to header.
        field_value = static_cast<uint32_t>(
                          reinterpret_cast<const uint8_t*>(header) - membase) +
                      opt_header.offset;
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

  // Create a thread to run in.
  // We start suspended so we can run the debugger prep.
  auto thread = object_ref<XThread>(new XThread(kernel_state(), stack_size_, 0,
                                                entry_point_, 0,
                                                X_CREATE_SUSPENDED, true));

  // We know this is the 'main thread'.
  char thread_name[32];
  std::snprintf(thread_name, xe::countof(thread_name), "Main XThread%08X",
                thread->handle());
  thread->set_name(thread_name);

  X_STATUS result = thread->Create();
  if (XFAILED(result)) {
    XELOGE("Could not create launch thread: %.8X", result);
    return result;
  }

  // Waits for a debugger client, if desired.
  if (emulator()->debugger()) {
    emulator()->debugger()->PreLaunch();
  }

  // Resume the thread now.
  // If the debugger has requested a suspend this will just decrement the
  // suspend count without resuming it until the debugger wants.
  thread->Resume();

  // Wait until thread completes.
  thread->Wait(0, 0, 0, nullptr);

  return X_STATUS_SUCCESS;
}

void XUserModule::Dump() {
  if (module_format_ == kModuleFormatElf) {
    // Quick die.
    return;
  }

  StringBuffer sb;

  xe::cpu::ExportResolver* export_resolver =
      kernel_state_->emulator()->export_resolver();
  auto header = xex_header();

  // XEX header.
  sb.AppendFormat("Module %s:\n", path_.c_str());
  sb.AppendFormat("    Module Flags: %.8X\n", (uint32_t)header->module_flags);

  // Security header
  auto security_info = xex_module()->xex_security_info();
  sb.AppendFormat("Security Header:\n");
  sb.AppendFormat("     Image Flags: %.8X\n",
                  (uint32_t)security_info->image_flags);
  sb.AppendFormat("    Load Address: %.8X\n",
                  (uint32_t)security_info->load_address);
  sb.AppendFormat("      Image Size: %.8X\n",
                  (uint32_t)security_info->image_size);
  sb.AppendFormat("    Export Table: %.8X\n",
                  (uint32_t)security_info->export_table);

  // Optional headers
  sb.AppendFormat("Optional Header Count: %d\n",
                  (uint32_t)header->header_count);

  for (uint32_t i = 0; i < header->header_count; i++) {
    auto& opt_header = header->headers[i];

    // Stash a pointer (although this isn't used in every case)
    auto opt_header_ptr =
        reinterpret_cast<const uint8_t*>(header) + opt_header.offset;
    switch (opt_header.key) {
      case XEX_HEADER_RESOURCE_INFO: {
        sb.AppendFormat("  XEX_HEADER_RESOURCE_INFO:\n");
        auto opt_resource_info =
            reinterpret_cast<const xex2_opt_resource_info*>(opt_header_ptr);

        uint32_t count = (opt_resource_info->size - 4) / 16;
        for (uint32_t j = 0; j < count; j++) {
          auto& res = opt_resource_info->resources[j];

          // Manually NULL-terminate the name.
          char name[9];
          std::memcpy(name, res.name, sizeof(res.name));
          name[8] = 0;

          sb.AppendFormat(
              "    %-8s %.8X-%.8X, %db\n", name, (uint32_t)res.address,
              (uint32_t)res.address + (uint32_t)res.size, (uint32_t)res.size);
        }
      } break;
      case XEX_HEADER_FILE_FORMAT_INFO: {
        sb.AppendFormat("  XEX_HEADER_FILE_FORMAT_INFO (TODO):\n");
      } break;
      case XEX_HEADER_DELTA_PATCH_DESCRIPTOR: {
        sb.AppendFormat("  XEX_HEADER_DELTA_PATCH_DESCRIPTOR (TODO):\n");
      } break;
      case XEX_HEADER_BOUNDING_PATH: {
        auto opt_bound_path =
            reinterpret_cast<const xex2_opt_bound_path*>(opt_header_ptr);
        sb.AppendFormat("  XEX_HEADER_BOUNDING_PATH: %s\n",
                        opt_bound_path->path);
      } break;
      case XEX_HEADER_ORIGINAL_BASE_ADDRESS: {
        sb.AppendFormat("  XEX_HEADER_ORIGINAL_BASE_ADDRESS: %.8X\n",
                        (uint32_t)opt_header.value);
      } break;
      case XEX_HEADER_ENTRY_POINT: {
        sb.AppendFormat("  XEX_HEADER_ENTRY_POINT: %.8X\n",
                        (uint32_t)opt_header.value);
      } break;
      case XEX_HEADER_IMAGE_BASE_ADDRESS: {
        sb.AppendFormat("  XEX_HEADER_IMAGE_BASE_ADDRESS: %.8X\n",
                        (uint32_t)opt_header.value);
      } break;
      case XEX_HEADER_IMPORT_LIBRARIES: {
        sb.AppendFormat("  XEX_HEADER_IMPORT_LIBRARIES:\n");
        auto opt_import_libraries =
            reinterpret_cast<const xex2_opt_import_libraries*>(opt_header_ptr);

        // FIXME: Don't know if 32 is the actual limit, but haven't seen more
        // than 2.
        const char* string_table[32];
        std::memset(string_table, 0, sizeof(string_table));

        // Parse the string table
        for (size_t l = 0, j = 0; l < opt_import_libraries->string_table_size;
             j++) {
          assert_true(j < xe::countof(string_table));
          const char* str = opt_import_libraries->string_table + l;

          string_table[j] = str;
          l += std::strlen(str) + 1;

          // Padding
          if ((l % 4) != 0) {
            l += 4 - (l % 4);
          }
        }

        auto libraries =
            reinterpret_cast<const uint8_t*>(opt_import_libraries) +
            opt_import_libraries->string_table_size + 12;
        uint32_t library_offset = 0;
        for (uint32_t l = 0; l < opt_import_libraries->library_count; l++) {
          auto library = reinterpret_cast<const xex2_import_library*>(
              libraries + library_offset);
          auto name = string_table[library->name_index];
          sb.AppendFormat("    %s - %d imports\n", name,
                          (uint16_t)library->count);

          // Manually byteswap these because of the bitfields.
          xex2_version version, version_min;
          version.value = xe::byte_swap<uint32_t>(library->version.value);
          version_min.value =
              xe::byte_swap<uint32_t>(library->version_min.value);
          sb.AppendFormat("      Version: %d.%d.%d.%d\n", version.major,
                          version.minor, version.build, version.qfe);
          sb.AppendFormat("      Min Version: %d.%d.%d.%d\n", version_min.major,
                          version_min.minor, version_min.build,
                          version_min.qfe);

          library_offset += library->size;
        }
      } break;
      case XEX_HEADER_CHECKSUM_TIMESTAMP: {
        sb.AppendFormat("  XEX_HEADER_CHECKSUM_TIMESTAMP (TODO):\n");
      } break;
      case XEX_HEADER_ORIGINAL_PE_NAME: {
        auto opt_pe_name =
            reinterpret_cast<const xex2_opt_original_pe_name*>(opt_header_ptr);
        sb.AppendFormat("  XEX_HEADER_ORIGINAL_PE_NAME: %s\n",
                        opt_pe_name->name);
      } break;
      case XEX_HEADER_STATIC_LIBRARIES: {
        sb.AppendFormat("  XEX_HEADER_STATIC_LIBRARIES:\n");
        auto opt_static_libraries =
            reinterpret_cast<const xex2_opt_static_libraries*>(opt_header_ptr);

        uint32_t count = (opt_static_libraries->size - 4) / 0x10;
        for (uint32_t l = 0; l < count; l++) {
          auto& library = opt_static_libraries->libraries[l];
          sb.AppendFormat("    %-8s : %d.%d.%d.%d\n", library.name,
                          static_cast<uint16_t>(library.version_major),
                          static_cast<uint16_t>(library.version_minor),
                          static_cast<uint16_t>(library.version_build),
                          static_cast<uint16_t>(library.version_qfe));
        }
      } break;
      case XEX_HEADER_TLS_INFO: {
        sb.AppendFormat("  XEX_HEADER_TLS_INFO:\n");
        auto opt_tls_info =
            reinterpret_cast<const xex2_opt_tls_info*>(opt_header_ptr);

        sb.AppendFormat("          Slot Count: %d\n",
                        static_cast<uint32_t>(opt_tls_info->slot_count));
        sb.AppendFormat("    Raw Data Address: %.8X\n",
                        static_cast<uint32_t>(opt_tls_info->raw_data_address));
        sb.AppendFormat("           Data Size: %d\n",
                        static_cast<uint32_t>(opt_tls_info->data_size));
        sb.AppendFormat("       Raw Data Size: %d\n",
                        static_cast<uint32_t>(opt_tls_info->raw_data_size));
      } break;
      case XEX_HEADER_DEFAULT_STACK_SIZE: {
        sb.AppendFormat("  XEX_HEADER_DEFAULT_STACK_SIZE: %d\n",
                        static_cast<uint32_t>(opt_header.value));
      } break;
      case XEX_HEADER_DEFAULT_FILESYSTEM_CACHE_SIZE: {
        sb.AppendFormat("  XEX_HEADER_DEFAULT_FILESYSTEM_CACHE_SIZE: %d\n",
                        static_cast<uint32_t>(opt_header.value));
      } break;
      case XEX_HEADER_DEFAULT_HEAP_SIZE: {
        sb.AppendFormat("  XEX_HEADER_DEFAULT_HEAP_SIZE: %d\n",
                        static_cast<uint32_t>(opt_header.value));
      } break;
      case XEX_HEADER_PAGE_HEAP_SIZE_AND_FLAGS: {
        sb.AppendFormat("  XEX_HEADER_PAGE_HEAP_SIZE_AND_FLAGS (TODO):\n");
      } break;
      case XEX_HEADER_SYSTEM_FLAGS: {
        sb.AppendFormat("  XEX_HEADER_SYSTEM_FLAGS: %.8X\n",
                        static_cast<uint32_t>(opt_header.value));
      } break;
      case XEX_HEADER_EXECUTION_INFO: {
        sb.AppendFormat("  XEX_HEADER_EXECUTION_INFO:\n");
        auto opt_exec_info =
            reinterpret_cast<const xex2_opt_execution_info*>(opt_header_ptr);

        sb.AppendFormat("       Media ID: %.8X\n",
                        static_cast<uint32_t>(opt_exec_info->media_id));
        sb.AppendFormat("       Title ID: %.8X\n",
                        static_cast<uint32_t>(opt_exec_info->title_id));
        sb.AppendFormat("    Savegame ID: %.8X\n",
                        static_cast<uint32_t>(opt_exec_info->title_id));
        sb.AppendFormat("    Disc Number / Total: %d / %d\n",
                        opt_exec_info->disc_number, opt_exec_info->disc_count);
      } break;
      case XEX_HEADER_TITLE_WORKSPACE_SIZE: {
        sb.AppendFormat("  XEX_HEADER_TITLE_WORKSPACE_SIZE: %d\n",
                        uint32_t(opt_header.value));
      } break;
      case XEX_HEADER_GAME_RATINGS: {
        sb.AppendFormat("  XEX_HEADER_GAME_RATINGS (TODO):\n");
      } break;
      case XEX_HEADER_LAN_KEY: {
        sb.AppendFormat("  XEX_HEADER_LAN_KEY (TODO):\n");
      } break;
      case XEX_HEADER_XBOX360_LOGO: {
        sb.AppendFormat("  XEX_HEADER_XBOX360_LOGO (TODO):\n");
      } break;
      case XEX_HEADER_MULTIDISC_MEDIA_IDS: {
        sb.AppendFormat("  XEX_HEADER_MULTIDISC_MEDIA_IDS (TODO):\n");
      } break;
      case XEX_HEADER_ALTERNATE_TITLE_IDS: {
        sb.AppendFormat("  XEX_HEADER_ALTERNATE_TITLE_IDS (TODO):\n");
      } break;
      case XEX_HEADER_ADDITIONAL_TITLE_MEMORY: {
        sb.AppendFormat("  XEX_HEADER_ADDITIONAL_TITLE_MEMORY: %d\n",
                        uint32_t(opt_header.value));
      } break;
      case XEX_HEADER_EXPORTS_BY_NAME: {
        sb.AppendFormat("  XEX_HEADER_EXPORTS_BY_NAME:\n");
        auto dir =
            reinterpret_cast<const xex2_opt_data_directory*>(opt_header_ptr);

        auto exe_address = xex_module()->xex_security_info()->load_address;
        auto e = memory()->TranslateVirtual<const X_IMAGE_EXPORT_DIRECTORY*>(
            exe_address + dir->offset);
        auto e_base = reinterpret_cast<uintptr_t>(e);

        // e->AddressOfX RVAs are relative to the IMAGE_EXPORT_DIRECTORY!
        auto function_table =
            reinterpret_cast<const uint32_t*>(e_base + e->AddressOfFunctions);
        // Names relative to directory.
        auto name_table =
            reinterpret_cast<const uint32_t*>(e_base + e->AddressOfNames);
        // Table of ordinals (by name).
        auto ordinal_table = reinterpret_cast<const uint16_t*>(
            e_base + e->AddressOfNameOrdinals);
        for (uint32_t n = 0; n < e->NumberOfNames; n++) {
          auto name = reinterpret_cast<const char*>(e_base + name_table[n]);
          uint16_t ordinal = ordinal_table[n];
          uint32_t addr = exe_address + function_table[ordinal];
          sb.AppendFormat("    %-28s - %.3X - %.8X\n", name, ordinal, addr);
        }
      } break;
      default: {
        sb.AppendFormat("  Unknown Header %.8X\n", (uint32_t)opt_header.key);
      } break;
    }
  }

  sb.AppendFormat("Sections:\n");
  for (uint32_t i = 0, page = 0; i < security_info->page_descriptor_count;
       i++) {
    // Manually byteswap the bitfield data.
    xex2_page_descriptor page_descriptor;
    page_descriptor.value =
        xe::byte_swap(security_info->page_descriptors[i].value);

    const char* type = "UNKNOWN";
    switch (page_descriptor.info) {
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

    const uint32_t page_size =
        security_info->load_address < 0x90000000 ? 64 * 1024 : 4 * 1024;
    uint32_t start_address = security_info->load_address + (page * page_size);
    uint32_t end_address = start_address + (page_descriptor.size * page_size);

    sb.AppendFormat("  %3u %s %3u pages    %.8X - %.8X (%d bytes)\n", page,
                    type, page_descriptor.size, start_address, end_address,
                    page_descriptor.size * page_size);
    page += page_descriptor.size;
  }

  // Print out imports.
  // TODO(benvanik): figure out a way to remove dependency on old xex header.
  auto old_header = xe_xex2_get_header(xex_module()->xex());

  sb.AppendFormat("Imports:\n");
  for (size_t n = 0; n < old_header->import_library_count; n++) {
    const xe_xex2_import_library_t* library = &old_header->import_libraries[n];

    xe_xex2_import_info_t* import_infos;
    size_t import_info_count;
    if (!xe_xex2_get_import_infos(xex_module()->xex(), library, &import_infos,
                                  &import_info_count)) {
      sb.AppendFormat(" %s - %lld imports\n", library->name, import_info_count);
      sb.AppendFormat("   Version: %d.%d.%d.%d\n", library->version.major,
                      library->version.minor, library->version.build,
                      library->version.qfe);
      sb.AppendFormat("   Min Version: %d.%d.%d.%d\n",
                      library->min_version.major, library->min_version.minor,
                      library->min_version.build, library->min_version.qfe);
      sb.AppendFormat("\n");

      // Counts.
      int known_count = 0;
      int unknown_count = 0;
      int impl_count = 0;
      int unimpl_count = 0;
      for (size_t m = 0; m < import_info_count; m++) {
        const xe_xex2_import_info_t* info = &import_infos[m];

        if (kernel_state_->IsKernelModule(library->name)) {
          auto kernel_export =
              export_resolver->GetExportByOrdinal(library->name, info->ordinal);
          if (kernel_export) {
            known_count++;
            if (kernel_export->is_implemented()) {
              impl_count++;
            } else {
              unimpl_count++;
            }
          } else {
            unknown_count++;
            unimpl_count++;
          }
        } else {
          auto module = kernel_state_->GetModule(library->name);
          if (module) {
            uint32_t export_addr =
                module->GetProcAddressByOrdinal(info->ordinal);
            if (export_addr) {
              impl_count++;
              known_count++;
            } else {
              unimpl_count++;
              unknown_count++;
            }
          } else {
            unimpl_count++;
            unknown_count++;
          }
        }
      }
      float total_count = static_cast<float>(import_info_count) / 100.0f;
      sb.AppendFormat("         Total: %4llu\n", import_info_count);
      sb.AppendFormat("         Known:  %3d%% (%d known, %d unknown)\n",
                      static_cast<int>(known_count / total_count), known_count,
                      unknown_count);
      sb.AppendFormat(
          "   Implemented:  %3d%% (%d implemented, %d unimplemented)\n",
          static_cast<int>(impl_count / total_count), impl_count, unimpl_count);
      sb.AppendFormat("\n");

      // Listing.
      for (size_t m = 0; m < import_info_count; m++) {
        const xe_xex2_import_info_t* info = &import_infos[m];
        const char* name = "UNKNOWN";
        bool implemented = false;

        cpu::Export* kernel_export = nullptr;
        if (kernel_state_->IsKernelModule(library->name)) {
          kernel_export =
              export_resolver->GetExportByOrdinal(library->name, info->ordinal);
          if (kernel_export) {
            name = kernel_export->name;
            implemented = kernel_export->is_implemented();
          }
        } else {
          auto module = kernel_state_->GetModule(library->name);
          if (module && module->GetProcAddressByOrdinal(info->ordinal)) {
            // TODO(benvanik): name lookup.
            implemented = true;
          }
        }
        if (kernel_export &&
            kernel_export->type == cpu::Export::Type::kVariable) {
          sb.AppendFormat("   V %.8X          %.3X (%3d) %s %s\n",
                          info->value_address, info->ordinal, info->ordinal,
                          implemented ? "  " : "!!", name);
        } else if (info->thunk_address) {
          sb.AppendFormat("   F %.8X %.8X %.3X (%3d) %s %s\n",
                          info->value_address, info->thunk_address,
                          info->ordinal, info->ordinal,
                          implemented ? "  " : "!!", name);
        }
      }
    }

    sb.AppendFormat("\n");
  }

  XELOGI(sb.GetString());
}

}  // namespace kernel
}  // namespace xe
