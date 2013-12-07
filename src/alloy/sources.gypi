# Copyright 2013 Ben Vanik. All Rights Reserved.
{
  'sources': [
    'alloy-private.h',
    'alloy.h',
    'arena.cc',
    'arena.h',
    'core.h',
    'memory.cc',
    'memory.h',
    'mutex.h',
    'string_buffer.cc',
    'string_buffer.h',
    'type_pool.h',
  ],

  'conditions': [
    ['OS == "mac" or OS == "linux"', {
      'sources': [
        'mutex_posix.cc',
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
        'mutex_win.cc',
      ],
    }],
  ],

  'includes': [
    'backend/sources.gypi',
    'compiler/sources.gypi',
    'frontend/sources.gypi',
    'hir/sources.gypi',
    'runtime/sources.gypi',
    'tracing/sources.gypi',
  ],
}
