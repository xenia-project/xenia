/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/kernel/module.h>

#include <third_party/pe/pe_image.h>


#define kXEModuleMaxSectionCount  32

typedef struct xe_module {
  xe_ref_t ref;

  xe_module_options_t options;

  xe_memory_ref   memory;
  xe_kernel_export_resolver_ref export_resolver;

  uint32_t        handle;
  xe_xex2_ref     xex;

  size_t          section_count;
  xe_module_pe_section_t sections[kXEModuleMaxSectionCount];
} xe_module_t;


int xe_module_load_pe(xe_module_ref module);


xe_module_ref xe_module_load(xe_memory_ref memory,
                             xe_kernel_export_resolver_ref export_resolver,
                             const void *addr, const size_t length,
                             xe_module_options_t options) {
  xe_module_ref module = (xe_module_ref)xe_calloc(sizeof(xe_module));
  xe_ref_init((xe_ref)module);

  xe_copy_struct(&module->options, &options, sizeof(xe_module_options_t));

  module->memory = xe_memory_retain(memory);
  module->export_resolver = xe_kernel_export_resolver_retain(export_resolver);

  xe_xex2_options_t xex_options;
  module->xex = xe_xex2_load(memory, addr, length, xex_options);
  XEEXPECTNOTNULL(module->xex);

  XEEXPECTZERO(xe_module_load_pe(module));

  return module;

XECLEANUP:
  xe_module_release(module);
  return NULL;
}

void xe_module_dealloc(xe_module_ref module) {
  xe_kernel_export_resolver_release(module->export_resolver);
  xe_memory_release(module->memory);
}

xe_module_ref xe_module_retain(xe_module_ref module) {
  xe_ref_retain((xe_ref)module);
  return module;
}

void xe_module_release(xe_module_ref module) {
  xe_ref_release((xe_ref)module, (xe_ref_dealloc_t)xe_module_dealloc);
}

uint32_t xe_module_get_handle(xe_module_ref module) {
  return module->handle;
}

xe_xex2_ref xe_module_get_xex(xe_module_ref module) {
  return xe_xex2_retain(module->xex);
}

const xe_xex2_header_t *xe_module_get_xex_header(xe_module_ref module) {
  return xe_xex2_get_header(module->xex);
}

void *xe_module_get_proc_address(xe_module_ref module, const uint32_t ordinal) {
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

int xe_module_load_pe(xe_module_ref module) {
  const xe_xex2_header_t *xex_header = xe_xex2_get_header(module->xex);
  uint8_t *mem = (uint8_t*)xe_memory_addr(module->memory, 0);
  const uint8_t *p = mem + xex_header->exe_address;

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

  // Verify section count not overrun.
  // NOTE: if this ever asserts, change to a dynamic section list.
  XEASSERT(filehdr->NumberOfSections <= kXEModuleMaxSectionCount);
  if (filehdr->NumberOfSections > kXEModuleMaxSectionCount) {
    return 1;
  }
  module->section_count = filehdr->NumberOfSections;

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
    xe_module_pe_section_t *section = &module->sections[n];
    xe_copy_memory(section->name, sizeof(section->name),
                   sechdr->Name, sizeof(sechdr->Name));
    section->name[8]      = 0;
    section->raw_address  = sechdr->PointerToRawData;
    section->raw_size     = sechdr->SizeOfRawData;
    section->address      = xex_header->exe_address + sechdr->VirtualAddress;
    section->size         = sechdr->Misc.VirtualSize;
    section->flags        = sechdr->Characteristics;
  }

  //DumpTLSDirectory(pImageBase, pNTHeader, (PIMAGE_TLS_DIRECTORY32)0);
  //DumpExportsSection(pImageBase, pNTHeader);
  return 0;
}

xe_module_pe_section_t *xe_module_get_section(xe_module_ref module,
                                              const char *name) {
  for (size_t n = 0; n < module->section_count; n++) {
    if (xestrcmpa(module->sections[n].name, name) == 0) {
      return &module->sections[n];
    }
  }
  return NULL;
}

int xe_module_get_method_hints(xe_module_ref module,
                               xe_module_pe_method_info_t **out_method_infos,
                               size_t *out_method_info_count) {
  uint8_t *mem = (uint8_t*)xe_memory_addr(module->memory, 0);

  *out_method_infos = NULL;
  *out_method_info_count = 0;

  const IMAGE_XBOX_RUNTIME_FUNCTION_ENTRY *entry = NULL;

  // Find pdata, which contains the exception handling entries.
  xe_module_pe_section_t *pdata = xe_module_get_section(module, ".pdata");
  if (!pdata) {
    // No exception data to go on.
    return 0;
  }

  // Resolve.
  const uint8_t *p = mem + pdata->address;

  // Entry count = pdata size / sizeof(entry).
  const size_t entry_count =
      pdata->size / sizeof(IMAGE_XBOX_RUNTIME_FUNCTION_ENTRY);
  if (entry_count == 0) {
    // Empty?
    return 0;
  }

  // Allocate output.
  xe_module_pe_method_info_t *method_infos =
      (xe_module_pe_method_info_t*)xe_calloc(
          entry_count * sizeof(xe_module_pe_method_info_t));
  XEEXPECTNOTNULL(method_infos);

  // Parse entries.
  // NOTE: entries are in memory as big endian, so pull them out and swap the
  // values before using them.
  entry = (const IMAGE_XBOX_RUNTIME_FUNCTION_ENTRY*)p;
  IMAGE_XBOX_RUNTIME_FUNCTION_ENTRY temp_entry;
  for (size_t n = 0; n < entry_count; n++, entry++) {
    xe_module_pe_method_info_t *method_info = &method_infos[n];
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

void xe_module_dump(xe_module_ref module) {
  //const uint8_t *mem = (const uint8_t*)xe_memory_addr(module->memory, 0);
  const xe_xex2_header_t *header = xe_xex2_get_header(module->xex);

  // XEX info.
  printf("Module %s:\n\n", module->options.path);
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
    const xe_xex2_opt_header_t *opt_header = &header->headers[n];
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
    const xe_xex2_section_t *section = &header->sections[n];
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
    const size_t start_address = header->exe_address +
                                 (i * xe_xex2_section_length);
    const size_t end_address = start_address + (section->info.page_count *
                                                xe_xex2_section_length);
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
    printf("  %-8s : %d.%d.%d.%d\n", library->name, library->major,
           library->minor, library->build, library->qfe);
  }
  printf("\n");

  // Imports.
  printf("Imports:\n");
  for (size_t n = 0; n < header->import_library_count; n++) {
    const xe_xex2_import_library_t *library = &header->import_libraries[n];

    xe_xex2_import_info_t* import_infos;
    size_t import_info_count;
    if (!xe_xex2_get_import_infos(module->xex, library,
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
        const xe_xex2_import_info_t *info = &import_infos[m];
        const xe_kernel_export_t *kernel_export =
            xe_kernel_export_resolver_get_by_ordinal(
                module->export_resolver, library->name, info->ordinal);
        if (kernel_export) {
          known_count++;
          if (xe_kernel_export_is_implemented(kernel_export)) {
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
        const xe_xex2_import_info_t *info = &import_infos[m];
        const xe_kernel_export_t *kernel_export =
            xe_kernel_export_resolver_get_by_ordinal(
                module->export_resolver, library->name, info->ordinal);
        const char *name = "UNKNOWN";
        bool implemented = false;
        if (kernel_export) {
          name = kernel_export->name;
          implemented = xe_kernel_export_is_implemented(kernel_export);
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
