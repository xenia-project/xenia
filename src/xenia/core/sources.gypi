# Copyright 2013 Ben Vanik. All Rights Reserved.
{
  'sources': [
    'file.cc',
    'file.h',
    'memory.cc',
    'memory.h',
    'mmap.h',
    'mutex.h',
    'pal.cc',
    'pal.h',
    'ref.cc',
    'ref.h',
    'socket.h',
    'thread.cc',
    'thread.h',
  ],

  'conditions': [
    ['OS == "mac" or OS == "linux"', {
      'sources': [
        'mmap_posix.cc',
        'mutex_posix.cc',
        'socket_posix.cc',
      ],
    }],
    ['OS == "win"', {
      'sources': [
        'mmap_win.cc',
        'mutex_win.cc',
        'socket_win.cc',
      ],
    }],
  ],
}
