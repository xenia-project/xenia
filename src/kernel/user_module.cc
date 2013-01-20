/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/kernel/user_module.h>

#include <third_party/pe/pe_image.h>


using namespace xe;
using namespace kernel;


UserModule::UserModule(xe_memory_ref memory) {
  memory_ = xe_memory_retain(memory);
  xex_ = NULL;
}

UserModule::~UserModule() {
  for (std::vector<PESection*>::iterator it = sections_.begin();
       it != sections_.end(); ++it) {
    delete *it;
  }

  xe_xex2_release(xex_);
  xe_memory_release(memory_);
}

int UserModule::Load(const void* addr, const size_t length,
                     const xechar_t* path) {
  XEIGNORE(xestrcpy(path_, XECOUNT(path_), path));
  const xechar_t *slash = xestrrchr(path, '/');
  if (slash) {
    XEIGNORE(xestrcpy(name_, XECOUNT(name_), slash + 1));
  }

  xe_xex2_options_t xex_options;
  xex_ = xe_xex2_load(memory_, addr, length, xex_options);
  XEEXPECTNOTNULL(xex_);

  XEEXPECTZERO(LoadPE());

  return 0;

XECLEANUP:
  return 1;
}

const xechar_t* UserModule::path() {
  return path_;
}

const xechar_t* UserModule::name() {
  return name_;
}

uint32_t UserModule::handle() {
  return handle_;
}

xe_xex2_ref UserModule::xex() {
  return xe_xex2_retain(xex_);
}

const xe_xex2_header_t* UserModule::xex_header() {
  return xe_xex2_get_header(xex_);
}

void* UserModule::GetProcAddress(const uint32_t ordinal) {
  XEASSERTALWAYS();
  return NULL;
}

// IMAGE_CE_RUNTIME_FUNCTION_ENTRY
// http://msdn.microsoft.com/en-us/library/ms879748.aspx
typedef struct IMAGE_XBOX_RUNTIME_FUNCTION_ENTRY_t {
  uint32_t      FuncStart;              // Virtual address
  union {
    struct {
      uint32_t  PrologLen       : 8;    // # of prolog instructions (size = x4)
      uint32_t  FuncLen         : 22;   // # of instructions total (size = x4)
      uint32_t  ThirtyTwoBit    : 1;    // Always 1
      uint32_t  ExceptionFlag   : 1;    // 1 if PDATA_EH in .text -- unknown if used
    } Flags;
    uint32_t    FlagsValue;             // To make byte swapping easier
  };
} IMAGE_XBOX_RUNTIME_FUNCTION_ENTRY;

int UserModule::LoadPE() {
  const xe_xex2_header_t* xex_header = xe_xex2_get_header(xex_);
  uint8_t* mem = xe_memory_addr(memory_, 0);
  const uint8_t* p = mem + xex_header->exe_address;

  // Verify DOS signature (MZ).
  const IMAGE_DOS_HEADER* doshdr = (const IMAGE_DOS_HEADER*)p;
  if (doshdr->e_magic != IMAGE_DOS_SIGNATURE) {
    return 1;
  }

  // Move to the NT header offset from the DOS header.
  p += doshdr->e_lfanew;

  // Verify NT signature (PE\0\0).
  const IMAGE_NT_HEADERS32* nthdr = (const IMAGE_NT_HEADERS32*)(p);
  if (nthdr->Signature != IMAGE_NT_SIGNATURE) {
    return 1;
  }

  // Verify matches an Xbox PE.
  const IMAGE_FILE_HEADER* filehdr = &nthdr->FileHeader;
  if ((filehdr->Machine != IMAGE_FILE_MACHINE_POWERPCBE) ||
      !(filehdr->Characteristics & IMAGE_FILE_32BIT_MACHINE)) {
      return 1;
  }
  // Verify the expected size.
  if (filehdr->SizeOfOptionalHeader != IMAGE_SIZEOF_NT_OPTIONAL_HEADER) {
    return 1;
  }

  // Verify optional header is 32bit.
  const IMAGE_OPTIONAL_HEADER32* opthdr = &nthdr->OptionalHeader;
  if (opthdr->Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC) {
    return 1;
  }
  // Verify subsystem.
  if (opthdr->Subsystem != IMAGE_SUBSYSTEM_XBOX) {
    return 1;
  }

  // Linker version - likely 8+
  // Could be useful for recognizing certain patterns
  //opthdr->MajorLinkerVersion; opthdr->MinorLinkerVersion;

  // Data directories of interest:
  // EXPORT           IMAGE_EXPORT_DIRECTORY
  // IMPORT           IMAGE_IMPORT_DESCRIPTOR[]
  // EXCEPTION        IMAGE_CE_RUNTIME_FUNCTION_ENTRY[]
  // BASERELOC
  // DEBUG            IMAGE_DEBUG_DIRECTORY[]
  // ARCHITECTURE     /IMAGE_ARCHITECTURE_HEADER/ ----- import thunks!
  // TLS              IMAGE_TLS_DIRECTORY
  // IAT              Import Address Table ptr
  //opthdr->DataDirectory[IMAGE_DIRECTORY_ENTRY_X].VirtualAddress / .Size

  // Quick scan to determine bounds of sections.
  size_t upper_address = 0;
  const IMAGE_SECTION_HEADER* sechdr = IMAGE_FIRST_SECTION(nthdr);
  for (size_t n = 0; n < filehdr->NumberOfSections; n++, sechdr++) {
    const size_t physical_address = opthdr->ImageBase + sechdr->VirtualAddress;
    upper_address =
        MAX(upper_address, physical_address + sechdr->Misc.VirtualSize);
  }

  // Setup/load sections.
  sechdr = IMAGE_FIRST_SECTION(nthdr);
  for (size_t n = 0; n < filehdr->NumberOfSections; n++, sechdr++) {
    PESection* section = (PESection*)xe_calloc(sizeof(PESection));
    xe_copy_memory(section->name, sizeof(section->name),
                   sechdr->Name, sizeof(sechdr->Name));
    section->name[8]      = 0;
    section->raw_address  = sechdr->PointerToRawData;
    section->raw_size     = sechdr->SizeOfRawData;
    section->address      = xex_header->exe_address + sechdr->VirtualAddress;
    section->size         = sechdr->Misc.VirtualSize;
    section->flags        = sechdr->Characteristics;
    sections_.push_back(section);
  }

  //DumpTLSDirectory(pImageBase, pNTHeader, (PIMAGE_TLS_DIRECTORY32)0);
  //DumpExportsSection(pImageBase, pNTHeader);
  return 0;
}

PESection* UserModule::GetSection(const char *name) {
  for (std::vector<PESection*>::iterator it = sections_.begin();
       it != sections_.end(); ++it) {
    if (!xestrcmpa((*it)->name, name)) {
      return *it;
    }
  }
  return NULL;
}

int UserModule::GetMethodHints(PEMethodInfo** out_method_infos,
                               size_t* out_method_info_count) {
  uint8_t* mem = xe_memory_addr(memory_, 0);

  *out_method_infos = NULL;
  *out_method_info_count = 0;

  const IMAGE_XBOX_RUNTIME_FUNCTION_ENTRY* entry = NULL;

  // Find pdata, which contains the exception handling entries.
  PESection* pdata = GetSection(".pdata");
  if (!pdata) {
    // No exception data to go on.
    return 0;
  }

  // Resolve.
  const uint8_t* p = mem + pdata->address;

  // Entry count = pdata size / sizeof(entry).
  size_t entry_count = pdata->size / sizeof(IMAGE_XBOX_RUNTIME_FUNCTION_ENTRY);
  if (!entry_count) {
    // Empty?
    return 0;
  }

  // Allocate output.
  PEMethodInfo* method_infos = (PEMethodInfo*)xe_calloc(
      entry_count * sizeof(PEMethodInfo));
  XEEXPECTNOTNULL(method_infos);

  // Parse entries.
  // NOTE: entries are in memory as big endian, so pull them out and swap the
  // values before using them.
  entry = (const IMAGE_XBOX_RUNTIME_FUNCTION_ENTRY*)p;
  IMAGE_XBOX_RUNTIME_FUNCTION_ENTRY temp_entry;
  for (size_t n = 0; n < entry_count; n++, entry++) {
    PEMethodInfo* method_info   = &method_infos[n];
    method_info->address        = XESWAP32BE(entry->FuncStart);

    // The bitfield needs to be swapped by hand.
    temp_entry.FlagsValue       = XESWAP32BE(entry->FlagsValue);
    method_info->total_length   = temp_entry.Flags.FuncLen * 4;
    method_info->prolog_length  = temp_entry.Flags.PrologLen * 4;
  }

  *out_method_infos = method_infos;
  *out_method_info_count = entry_count;
  return 0;

XECLEANUP:
  if (method_infos) {
    xe_free(method_infos);
  }
  return 1;
}

void UserModule::Dump(ExportResolver* export_resolver) {
  //const uint8_t *mem = (const uint8_t*)xe_memory_addr(memory_, 0);
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
  printf("  %.8X, %db\n", header->resource_info.address,
                          header->resource_info.size);
  printf("  TODO\n");
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
          if (kernel_export->IsImplemented()) {
            impl_count++;
          }
        } else {
          unknown_count++;
          unimpl_count++;
        }
      }
      printf("         Total: %4zu\n", import_info_count);
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
          implemented = kernel_export->IsImplemented();
        }
        if (info->thunk_address) {
          printf("   F %.8X %.8X %.3X (%3d) %s %s\n",
                 info->value_address, info->thunk_address, info->ordinal,
                 info->ordinal, implemented ? "  " : "!!", name);
        } else {
          printf("   V %.8X          %.3X (%3d) %s %s\n",
                 info->value_address, info->ordinal, info->ordinal,
                 implemented ? "  " : "!!", name);
        }
      }

      xe_free(import_infos);
    }

    printf("\n");
  }

  // Exports.
  printf("Exports:\n");
  printf("  TODO\n");
  printf("\n");
}
