# Copyright 2013 Ben Vanik. All Rights Reserved.
{
  'sources': [
    'hash.cc',
    'hash.h',
    'ref.cc',
    'ref.h',
    'run_loop.h',
    'socket.h',
  ],

  'conditions': [
    ['OS == "mac" or OS == "linux"', {
      'sources': [
        'socket_posix.cc',
      ],
    }],
    ['OS == "linux"', {
      'sources': [
      ],
    }],
    ['OS == "mac"', {
      'sources': [
      ],
    }],
    ['OS == "win"', {
      'sources': [
        'run_loop_win.cc',
        'socket_win.cc',
      ],
    }],
  ],
}
