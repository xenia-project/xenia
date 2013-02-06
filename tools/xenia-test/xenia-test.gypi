# Copyright 2013 Ben Vanik. All Rights Reserved.
{
  'targets': [
    {
      'target_name': 'xenia-test',
      'type': 'executable',

      'dependencies': [
        'xenia',
      ],

      'include_dirs': [
        '.',
      ],

      'sources': [
        'xenia-test.cc',
      ],
    },
  ],
}
