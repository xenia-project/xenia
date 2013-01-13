# Copyright 2013 Ben Vanik. All Rights Reserved.
{
  'sources': [
    'file.cc',
    'memory.cc',
    'pal.cc',
    'ref.cc',
  ],

  'conditions': [
    ['OS == "mac" or OS == "linux"', {
      'sources': [
        'mmap_posix.cc',
      ],
    }],
    ['OS == "win"', {
      'sources': [
      ],
    }],
  ],
}
