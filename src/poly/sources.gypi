# Copyright 2014 Ben Vanik. All Rights Reserved.
{
  'sources': [
    'assert.h',
    'atomic.h',
    'byte_order.h',
    'debugging.h',
    'delegate.h',
    'config.h',
    'cxx_compat.h',
    'logging.cc',
    'logging.h',
    'main.h',
    'mapped_memory.h',
    'math.cc',
    'math.h',
    'memory.cc',
    'memory.h',
    'platform.h',
    'poly.h',
    'string.cc',
    'string.h',
    'threading.cc',
    'threading.h',
  ],

  'conditions': [
    ['OS == "mac" or OS == "linux"', {
      'sources': [
        'main_posix.cc',
        'mapped_memory_posix.cc',
      ],
    }],
    ['OS == "linux"', {
      'sources': [
        'threading_posix.cc',
      ],
    }],
    ['OS == "mac"', {
      'sources': [
        'debugging_mac.cc',
        'threading_mac.cc',
      ],
    }],
    ['OS == "win"', {
      'sources': [
        'debugging_win.cc',
        'main_win.cc',
        'mapped_memory_win.cc',
        'threading_win.cc',
      ],
    }],
  ],

  'includes': [
    'ui/sources.gypi',
  ],
}
