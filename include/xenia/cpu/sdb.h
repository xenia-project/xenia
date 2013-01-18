/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_SDB_H_
#define XENIA_CPU_SDB_H_

#include <xenia/core.h>

#include <vector>

#include <xenia/kernel/module.h>


struct xe_sdb_function;
typedef struct xe_sdb_function xe_sdb_function_t;
struct xe_sdb_variable;
typedef struct xe_sdb_variable xe_sdb_variable_t;


typedef struct {
  uint32_t          address;
  xe_sdb_function_t *source;
  xe_sdb_function_t *target;
} xe_sdb_function_call_t;

typedef struct {
  uint32_t          address;
  xe_sdb_function_t *source;
  xe_sdb_variable_t *target;
} xe_sdb_variable_access_t;

typedef enum {
  kXESDBFunctionUnknown = 0,
  kXESDBFunctionKernel  = 1,
  kXESDBFunctionUser    = 2,
} xe_sdb_function_type;

struct xe_sdb_function {
  uint32_t  start_address;
  uint32_t  end_address;
  char      *name;
  xe_sdb_function_type type;
  uint32_t  flags;

  std::vector<xe_sdb_function_call_t*> incoming_calls;
  std::vector<xe_sdb_function_call_t*> outgoing_calls;
  std::vector<xe_sdb_variable_access_t*> variable_accesses;
};

struct xe_sdb_variable {
  uint32_t  address;
  char      *name;
};

typedef struct {
  int       type;
  union {
    xe_sdb_function_t*  function;
    xe_sdb_variable_t*  variable;
  };
} xe_sdb_symbol_t;


struct xe_sdb;
typedef struct xe_sdb* xe_sdb_ref;


xe_sdb_ref xe_sdb_create(xe_memory_ref memory, xe_module_ref module);
xe_sdb_ref xe_sdb_retain(xe_sdb_ref sdb);
void xe_sdb_release(xe_sdb_ref sdb);

xe_sdb_function_t* xe_sdb_insert_function(xe_sdb_ref sdb, uint32_t address);
xe_sdb_variable_t* xe_sdb_insert_variable(xe_sdb_ref sdb, uint32_t address);

xe_sdb_function_t* xe_sdb_get_function(xe_sdb_ref sdb, uint32_t address);
xe_sdb_variable_t* xe_sdb_get_variable(xe_sdb_ref sdb, uint32_t address);

int xe_sdb_get_functions(xe_sdb_ref sdb, xe_sdb_function_t ***out_functions,
                         size_t *out_function_count);

void xe_sdb_dump(xe_sdb_ref sdb);


#endif  // XENIA_CPU_SDB_H_
