/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/cpu/sdb.h>

#include <list>
#include <map>


typedef std::map<uint32_t, xe_sdb_symbol_t> xe_sdb_symbol_map;
typedef std::list<xe_sdb_function_t*> xe_sdb_function_queue;

struct xe_sdb {
  xe_ref_t ref;

  xe_memory_ref   memory;

  size_t                  function_count;
  size_t                  variable_count;
  xe_sdb_symbol_map       *symbols;
  xe_sdb_function_queue   *scan_queue;
};


int xe_sdb_analyze_module(xe_sdb_ref sdb, xe_module_ref module);


xe_sdb_ref xe_sdb_create(xe_memory_ref memory, xe_module_ref module) {
  xe_sdb_ref sdb = (xe_sdb_ref)xe_calloc(sizeof(xe_sdb));
  xe_ref_init((xe_ref)sdb);

  sdb->memory = xe_memory_retain(memory);

  sdb->symbols = new xe_sdb_symbol_map();
  sdb->scan_queue = new xe_sdb_function_queue();

  XEEXPECTZERO(xe_sdb_analyze_module(sdb, module));

  return sdb;

XECLEANUP:
  xe_sdb_release(sdb);
  return NULL;
}

void xe_sdb_dealloc(xe_sdb_ref sdb) {
  // TODO(benvanik): release strdup results

  for (xe_sdb_symbol_map::iterator it = sdb->symbols->begin(); it !=
      sdb->symbols->end(); ++it) {
    switch (it->second.type) {
      case 0:
        delete it->second.function;
        break;
      case 1:
        delete it->second.variable;
        break;
    }
  }

  delete sdb->scan_queue;
  delete sdb->symbols;

  xe_memory_release(sdb->memory);
}

xe_sdb_ref xe_sdb_retain(xe_sdb_ref sdb) {
  xe_ref_retain((xe_ref)sdb);
  return sdb;
}

void xe_sdb_release(xe_sdb_ref sdb) {
  xe_ref_release((xe_ref)sdb, (xe_ref_dealloc_t)xe_sdb_dealloc);
}

xe_sdb_function_t* xe_sdb_insert_function(xe_sdb_ref sdb, uint32_t address) {
  xe_sdb_function_t *fn = xe_sdb_get_function(sdb, address);
  if (fn) {
    return fn;
  }

  printf("add fn %.8X\n", address);
  fn = (xe_sdb_function_t*)xe_calloc(sizeof(xe_sdb_function_t));
  fn->start_address = address;
  xe_sdb_symbol_t symbol;
  symbol.type = 0;
  symbol.function = fn;
  sdb->function_count++;
  sdb->symbols->insert(xe_sdb_symbol_map::value_type(address, symbol));
  sdb->scan_queue->push_back(fn);
  return fn;
}

xe_sdb_variable_t* xe_sdb_insert_variable(xe_sdb_ref sdb, uint32_t address) {
  xe_sdb_variable_t *var = xe_sdb_get_variable(sdb, address);
  if (var) {
    return var;
  }

  printf("add var %.8X\n", address);
  var = (xe_sdb_variable_t*)xe_calloc(sizeof(xe_sdb_variable_t));
  var->address = address;
  xe_sdb_symbol_t symbol;
  symbol.type = 1;
  symbol.variable = var;
  sdb->variable_count++;
  sdb->symbols->insert(xe_sdb_symbol_map::value_type(address, symbol));
  return var;
}

xe_sdb_function_t* xe_sdb_get_function(xe_sdb_ref sdb, uint32_t address) {
  xe_sdb_symbol_map::iterator i = sdb->symbols->find(address);
  if (i != sdb->symbols->end() &&
      i->second.type == 0) {
    return i->second.function;
  }
  return NULL;
}

xe_sdb_variable_t* xe_sdb_get_variable(xe_sdb_ref sdb, uint32_t address) {
  xe_sdb_symbol_map::iterator i = sdb->symbols->find(address);
  if (i != sdb->symbols->end() &&
      i->second.type == 1) {
    return i->second.variable;
  }
  return NULL;
}

int xe_sdb_get_functions(xe_sdb_ref sdb, xe_sdb_function_t ***out_functions,
                         size_t *out_function_count) {
  xe_sdb_function_t **functions = (xe_sdb_function_t**)xe_malloc(
      sizeof(xe_sdb_function_t*) * sdb->function_count);
  int n = 0;
  for (xe_sdb_symbol_map::iterator it = sdb->symbols->begin();
      it != sdb->symbols->end(); ++it) {
    switch (it->second.type) {
      case 0:
        functions[n++] = it->second.function;
        break;
    }
  }
  *out_functions = functions;
  *out_function_count = sdb->function_count;
  return 0;
}

void xe_sdb_dump(xe_sdb_ref sdb) {
  uint32_t previous = 0;
  for (xe_sdb_symbol_map::iterator it = sdb->symbols->begin();
      it != sdb->symbols->end(); ++it) {
    switch (it->second.type) {
      case 0:
        {
          xe_sdb_function_t *fn = it->second.function;
          if (previous && (int)(fn->start_address - previous) > 0) {
            printf("%.8X-%.8X (%5d) h\n", previous, fn->start_address,
                   fn->start_address - previous);
          }
          printf("%.8X-%.8X (%5d) f %s\n", fn->start_address,
                 fn->end_address + 4,
                 fn->end_address - fn->start_address + 4,
                 fn->name ? fn->name : "<unknown>");
          previous = fn->end_address + 4;
        }
        break;
      case 1:
        {
          xe_sdb_variable_t *var = it->second.variable;
          printf("%.8X v %s\n", var->address,
                 var->name ? var->name : "<unknown>");
        }
        break;
    }
  }
}

int xe_sdb_find_gplr(xe_sdb_ref sdb, xe_module_ref module) {
  // Special stack save/restore functions.
  // __savegprlr_14 to __savegprlr_31
  // __restgprlr_14 to __restgprlr_31
  // http://research.microsoft.com/en-us/um/redmond/projects/invisible/src/crt/md/ppc/xxx.s.htm
  // It'd be nice to stash these away and mark them as such to allow for
  // special codegen.
  static const uint32_t code_values[] = {
    0x68FFC1F9, // __savegprlr_14
    0x70FFE1F9, // __savegprlr_15
    0x78FF01FA, // __savegprlr_16
    0x80FF21FA, // __savegprlr_17
    0x88FF41FA, // __savegprlr_18
    0x90FF61FA, // __savegprlr_19
    0x98FF81FA, // __savegprlr_20
    0xA0FFA1FA, // __savegprlr_21
    0xA8FFC1FA, // __savegprlr_22
    0xB0FFE1FA, // __savegprlr_23
    0xB8FF01FB, // __savegprlr_24
    0xC0FF21FB, // __savegprlr_25
    0xC8FF41FB, // __savegprlr_26
    0xD0FF61FB, // __savegprlr_27
    0xD8FF81FB, // __savegprlr_28
    0xE0FFA1FB, // __savegprlr_29
    0xE8FFC1FB, // __savegprlr_30
    0xF0FFE1FB, // __savegprlr_31
    0xF8FF8191,
    0x2000804E,
    0x68FFC1E9, // __restgprlr_14
    0x70FFE1E9, // __restgprlr_15
    0x78FF01EA, // __restgprlr_16
    0x80FF21EA, // __restgprlr_17
    0x88FF41EA, // __restgprlr_18
    0x90FF61EA, // __restgprlr_19
    0x98FF81EA, // __restgprlr_20
    0xA0FFA1EA, // __restgprlr_21
    0xA8FFC1EA, // __restgprlr_22
    0xB0FFE1EA, // __restgprlr_23
    0xB8FF01EB, // __restgprlr_24
    0xC0FF21EB, // __restgprlr_25
    0xC8FF41EB, // __restgprlr_26
    0xD0FF61EB, // __restgprlr_27
    0xD8FF81EB, // __restgprlr_28
    0xE0FFA1EB, // __restgprlr_29
    0xE8FFC1EB, // __restgprlr_30
    0xF0FFE1EB, // __restgprlr_31
    0xF8FF8181,
    0xA603887D,
    0x2000804E,
  };

  uint32_t gplr_start = 0;
  const xe_xex2_header_t *header = xe_module_get_xex_header(module);
  for (size_t n = 0, i = 0; n < header->section_count; n++) {
    const xe_xex2_section_t *section = &header->sections[n];
    const size_t start_address = header->exe_address +
                                 (i * xe_xex2_section_length);
    const size_t end_address = start_address + (section->info.page_count *
                                                xe_xex2_section_length);
    if (section->info.type == XEX_SECTION_CODE) {
      gplr_start = xe_memory_search_aligned(
          sdb->memory, start_address, end_address,
          code_values, XECOUNT(code_values));
      if (gplr_start) {
        break;
      }
    }
    i += section->info.page_count;
  }
  if (!gplr_start) {
    return 0;
  }

  // Add function stubs.
  char name[32];
  uint32_t addr = gplr_start;
  for (int n = 14; n <= 31; n++) {
    xesnprintf(name, XECOUNT(name), "__savegprlr_%d", n);
    xe_sdb_function_t *fn = xe_sdb_insert_function(sdb, addr);
    fn->end_address = fn->start_address + (31 - n) * 4 + 2 * 4;
    fn->name = xestrdup(name);
    fn->type = kXESDBFunctionUser;
    addr += 4;
  }
  addr = gplr_start + 20 * 4;
  for (int n = 14; n <= 31; n++) {
    xesnprintf(name, XECOUNT(name), "__restgprlr_%d", n);
    xe_sdb_function_t *fn = xe_sdb_insert_function(sdb, addr);
    fn->end_address = fn->start_address + (31 - n) * 4 + 3 * 4;
    fn->name = xestrdup(name);
    fn->type = kXESDBFunctionUser;
    addr += 4;
  }

  return 0;
}

int xe_sdb_add_imports(xe_sdb_ref sdb, xe_module_ref module,
                       const xe_xex2_import_library_t *library) {
  xe_xex2_ref xex = xe_module_get_xex(module);
  xe_xex2_import_info_t *import_infos;
  size_t import_info_count;
  if (xe_xex2_get_import_infos(xex, library, &import_infos,
                               &import_info_count)) {
    return 1;
  }

  char name[64];
  for (size_t n = 0; n < import_info_count; n++) {
    const xe_xex2_import_info_t *info = &import_infos[n];
    xe_sdb_variable_t *var = xe_sdb_insert_variable(sdb, info->value_address);
    // TODO(benvanik): use kernel name
    xesnprintf(name, XECOUNT(name), "__var_%s_%.3X", library->name,
               info->ordinal);
    var->name = strdup(name);
    if (info->thunk_address) {
      xe_sdb_function_t *fn = xe_sdb_insert_function(sdb, info->thunk_address);
      // TODO(benvanik): use kernel name
      xesnprintf(name, XECOUNT(name), "__thunk_%s_%.3X", library->name,
                 info->ordinal);
      fn->end_address = fn->start_address + 16 - 4;
      fn->name = strdup(name);
      fn->type = kXESDBFunctionKernel;
    }
  }

  return 0;
}

int xe_sdb_add_method_hints(xe_sdb_ref sdb, xe_module_ref module) {
  xe_module_pe_method_info_t *method_infos;
  size_t method_info_count;
  if (xe_module_get_method_hints(module, &method_infos, &method_info_count)) {
    return 1;
  }

  for (size_t n = 0; n < method_info_count; n++) {
    xe_module_pe_method_info_t *method_info = &method_infos[n];
    xe_sdb_function_t *fn = xe_sdb_insert_function(sdb, method_info->address);
    fn->end_address = method_info->address + method_info->total_length - 4;
    fn->type = kXESDBFunctionUser;
    // TODO(benvanik): something with prolog_length?
  }

  return 0;
}

int xe_sdb_analyze_function(xe_sdb_ref sdb, xe_sdb_function_t *fn) {
  // Ignore functions already analyzed.
  if (fn->type != kXESDBFunctionUnknown) {
    return 0;
  }

  // TODO(benvanik): analysis.
  // Search forward from start address to find the end address.
  // Use branch tracking to figure that out.
  return 0;
}

int xe_sdb_flush_queue(xe_sdb_ref sdb) {
  while (sdb->scan_queue->size()) {
    xe_sdb_function_t *fn = sdb->scan_queue->front();
    sdb->scan_queue->pop_front();
    if (!xe_sdb_analyze_function(sdb, fn)) {
      return 1;
    }
  }
  return 0;
}

int xe_sdb_analyze_module(xe_sdb_ref sdb, xe_module_ref module) {
  // Iteratively run passes over the db.
  // This uses a queue to do a breadth-first search of all accessible
  // functions. Callbacks and such likely won't be hit.

  const xe_xex2_header_t *header = xe_module_get_xex_header(module);

  // Find __savegprlr_* and __restgprlr_*.
  xe_sdb_find_gplr(sdb, module);

  // Add each import thunk.
  for (size_t n = 0; n < header->import_library_count; n++) {
    const xe_xex2_import_library_t *library = &header->import_libraries[n];
    xe_sdb_add_imports(sdb, module, library);
  }

  // Add each export root.
  // TODO(benvanik): exports.
  //   - insert fn or variable
  //   - queue fn

  // Add method hints, if available.
  // Not all XEXs have these.
  xe_sdb_add_method_hints(sdb, module);

  // Queue entry point of the application.
  xe_sdb_function_t *fn = xe_sdb_insert_function(sdb, header->exe_entry_point);
  fn->name = strdup("<entry>");

  // Keep pumping the queue until there's nothing left to do.
  xe_sdb_flush_queue(sdb);

  // Do a pass over the functions to fill holes.
  // TODO(benvanik): hole filling.
  xe_sdb_flush_queue(sdb);

  return 0;
}
