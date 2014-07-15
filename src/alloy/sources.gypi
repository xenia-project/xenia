# Copyright 2013 Ben Vanik. All Rights Reserved.
{
  'sources': [
    'alloy-private.h',
    'alloy.cc',
    'alloy.h',
    'arena.cc',
    'arena.h',
    'core.h',
    'delegate.h',
    'memory.cc',
    'memory.h',
    'reset_scope.h',
    'string_buffer.cc',
    'string_buffer.h',
    'type_pool.h',
    'vec128.h',
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
      ],
    }],
    ['OS == "win"', {
      'sources': [
      ],
    }],
  ],

  'includes': [
    'backend/sources.gypi',
    'compiler/sources.gypi',
    'frontend/sources.gypi',
    'hir/sources.gypi',
    'runtime/sources.gypi',
  ],
}
