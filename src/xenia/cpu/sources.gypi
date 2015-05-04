# Copyright 2013 Ben Vanik. All Rights Reserved.
{
  'sources': [
    'cpu-private.h',
    'cpu.cc',
    'cpu.h',
    'debug_info.cc',
    'debug_info.h',
    'debugger.cc',
    'debugger.h',
    'entry_table.cc',
    'entry_table.h',
    'export_resolver.cc',
    'export_resolver.h',
    'function.cc',
    'function.h',
    'instrument.cc',
    'instrument.h',
    'mmio_handler.cc',
    'mmio_handler.h',
    'module.cc',
    'module.h',
    'processor.cc',
    'processor.h',
    'raw_module.cc',
    'raw_module.h',
    'symbol_info.cc',
    'symbol_info.h',
    'test_module.cc',
    'test_module.h',
    'thread_state.cc',
    'thread_state.h',
    'xex_module.cc',
    'xex_module.h',
  ],

  'conditions': [
    ['OS == "mac" or OS == "linux"', {
      'sources': [
      ],
    }],
    ['OS == "linux"', {
      'sources': [
      ],
    }],
    ['OS == "mac"', {
      'sources': [
        'mmio_handler_mac.cc',
      ],
    }],
    ['OS == "win"', {
      'sources': [
        'mmio_handler_win.cc',
      ],
    }],
  ],

  'includes': [
    'backend/sources.gypi',
    'compiler/sources.gypi',
    'frontend/sources.gypi',
    'hir/sources.gypi',
  ],
}
