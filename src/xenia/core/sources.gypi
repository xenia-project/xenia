# Copyright 2013 Ben Vanik. All Rights Reserved.
{
  'sources': [
    'crc32.cc',
    'crc32.h',
    'file.cc',
    'file.h',
    'hash.cc',
    'hash.h',
    'mmap.h',
    'mutex.h',
    'pal.h',
    'path.cc',
    'path.h',
    'ref.cc',
    'ref.h',
    'run_loop.h',
    'socket.h',
    'thread.cc',
    'thread.h',
  ],

  'conditions': [
    ['OS == "mac" or OS == "linux"', {
      'sources': [
        'mmap_posix.cc',
        'mutex_posix.cc',
        'path_posix.cc',
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
        'mutex_win.cc',
        'pal_win.cc',
        'path_win.cc',
        'run_loop_win.cc',
        'socket_win.cc',
      ],
    }],
  ],
}
