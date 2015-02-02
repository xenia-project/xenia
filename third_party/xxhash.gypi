# Copyright 2014 Ben Vanik. All Rights Reserved.
{
  'targets': [
    {
      'target_name': 'xxhash',
      'type': '<(library)',

      'include_dirs': [
        'xxhash/',
      ],

      'sources': [
        'xxhash/xxhash.c',
        'xxhash/xxhash.h',
      ],
    }
  ]
}
