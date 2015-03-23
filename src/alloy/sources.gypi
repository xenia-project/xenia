# Copyright 2013 Ben Vanik. All Rights Reserved.
{
  'sources': [
    'alloy-private.h',
    'alloy.cc',
    'alloy.h',
    'memory.cc',
    'memory.h',
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
