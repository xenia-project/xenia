# Copyright 2014 Ben Vanik. All Rights Reserved.
{
  'sources': [
    'assert.h',
    'atomic.h',
    'byte_order.h',
    'debugging.h',
    'config.h',
    'cxx_compat.h',
    'math.cc',
    'math.h',
    'memory.cc',
    'memory.h',
    'platform.h',
    'poly-private.h',
    'poly.cc',
    'poly.h',
    'threading.h',
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
        'debugging_mac.cc',
        'threading_mac.cc',
      ],
    }],
    ['OS == "win"', {
      'sources': [
        'debugging_win.cc',
        'threading_win.cc',
      ],
    }],
  ],

  'includes': [
  ],
}
