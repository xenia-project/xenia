# Copyright 2013 Ben Vanik. All Rights Reserved.
{
  'targets': [
    {
      'target_name': 'xenia-info',
      'type': 'executable',

      'dependencies': [
        'xeniacore',
      ],

      'include_dirs': [
        '.',
      ],

      'sources': [
        'xenia-info.cc',
      ],
    },
  ],
}
