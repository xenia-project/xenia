# Copyright 2013 Ben Vanik. All Rights Reserved.
{
  'sources': [
    'hash.cc',
    'hash.h',
    'mmap.h',
    'pal.h',
    'ref.cc',
    'ref.h',
    'run_loop.h',
    'socket.h',
  ],

  'conditions': [
    ['OS == "mac" or OS == "linux"', {
      'sources': [
        'mmap_posix.cc',
        'socket_posix.cc',
      ],
    }],
    ['OS == "linux"', {
      'sources': [
        'pal_posix.cc',
      ],
    }],
    ['OS == "mac"', {
      'sources': [
        'pal_mac.cc',
      ],
    }],
    ['OS == "win"', {
      'sources': [
        'mmap_win.cc',
        'pal_win.cc',
        'run_loop_win.cc',
        'socket_win.cc',
      ],
    }],
  ],
}
