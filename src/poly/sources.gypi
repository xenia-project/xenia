# Copyright 2014 Ben Vanik. All Rights Reserved.
{
  'sources': [
    'cxx_compat.h',
    'math.h',
    'poly-private.h',
    'poly.cc',
    'poly.h',
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
  ],
}
