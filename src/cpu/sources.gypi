# Copyright 2013 Ben Vanik. All Rights Reserved.
{
  'sources': [
    'cpu.cc',
    'exec_module.cc',
    'llvm_exports.cc',
    'processor.cc',
    'sdb.cc',
    'thread_state.cc',
  ],

  'includes': [
    'codegen/sources.gypi',
    'ppc/sources.gypi',
  ],
}
