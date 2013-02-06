# Copyright 2013 Ben Vanik. All Rights Reserved.
{
  'sources': [
    'cpu-private.h',
    'cpu.cc',
    'cpu.h',
    'exec_module.cc',
    'exec_module.h',
    'llvm_exports.cc',
    'llvm_exports.h',
    'ppc.h',
    'processor.cc',
    'processor.h',
    'thread_state.cc',
    'thread_state.h',
  ],

  'includes': [
    'codegen/sources.gypi',
    'ppc/sources.gypi',
    'sdb/sources.gypi',
  ],
}
