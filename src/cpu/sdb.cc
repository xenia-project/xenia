/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/cpu/sdb.h>


struct xe_sdb {
  xe_ref_t ref;

  xe_memory_ref   memory;
};


xe_sdb_ref xe_sdb_create(xe_memory_ref memory) {
  xe_sdb_ref sdb = (xe_sdb_ref)xe_calloc(sizeof(xe_sdb));
  xe_ref_init((xe_ref)sdb);

  sdb->memory = xe_memory_retain(memory);

  return sdb;
}

void xe_sdb_dealloc(xe_sdb_ref sdb) {
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
  return NULL;
}

xe_sdb_variable_t* xe_sdb_insert_variable(xe_sdb_ref sdb, uint32_t address) {
  return NULL;
}

xe_sdb_function_t* xe_sdb_get_function(xe_sdb_ref sdb, uint32_t address) {
  return NULL;
}

xe_sdb_variable_t* xe_sdb_get_variable(xe_sdb_ref sdb, uint32_t address) {
  return NULL;
}

void xe_sdb_dump(xe_sdb_ref sdb) {
  // TODO(benvanik): dump all functions and symbols
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

int xe_sdb_analyze_module(xe_sdb_ref sdb, xe_module_ref module) {
  // TODO(benvanik): analysis.
  // Iteratively run passes over the db:
  // - for each import:
  //   - insert fn and setup as a thunk
  // - for each export
  //   - insert fn or variable
  //   - queue fn
  // - insert entry point
  // - queue entry point
  // - while (process_queue.length()):
  //   - fn = shift()
  //   - analyze_function(fn)
  return 0;
}
