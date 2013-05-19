# Copyright 2013 Ben Vanik. All Rights Reserved.
{
  'sources': [
    'backend.h',
    'code_unit_builder.h',
    'cpu-private.h',
    'cpu.cc',
    'cpu.h',
    'exec_module.cc',
    'exec_module.h',
    'function_table.cc',
    'function_table.h',
    'global_exports.cc',
    'global_exports.h',
    'jit.h',
    'library_linker.h',
    'library_loader.h',
    'ppc.h',
    'processor.cc',
    'processor.h',
    'thread_state.cc',
    'thread_state.h',
  ],

  'includes': [
    #'llvmbe/sources.gypi',
    'ppc/sources.gypi',
    'sdb/sources.gypi',
  ],
}
