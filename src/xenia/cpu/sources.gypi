# Copyright 2013 Ben Vanik. All Rights Reserved.
{
  'sources': [
    'backend.h',
    'cpu-private.h',
    'cpu.cc',
    'cpu.h',
    'exec_module.cc',
    'exec_module.h',
    'global_exports.cc',
    'global_exports.h',
    'jit.h',
    'ppc.h',
    'processor.cc',
    'processor.h',
    'thread_state.cc',
    'thread_state.h',
  ],

  'includes': [
    'ppc/sources.gypi',
    'sdb/sources.gypi',
    'x64/sources.gypi',
  ],
}
