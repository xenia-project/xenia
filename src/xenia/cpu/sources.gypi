# Copyright 2013 Ben Vanik. All Rights Reserved.
{
  'sources': [
    'cpu-private.h',
    'cpu.cc',
    'cpu.h',
    'mmio_handler.cc',
    'mmio_handler.h',
    'processor.cc',
    'processor.h',
    'xenon_memory.cc',
    'xenon_memory.h',
    'xenon_runtime.cc',
    'xenon_runtime.h',
    'xenon_thread_state.cc',
    'xenon_thread_state.h',
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
}
