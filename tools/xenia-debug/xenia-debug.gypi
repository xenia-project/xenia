# Copyright 2013 Ben Vanik. All Rights Reserved.
{
  'targets': [
    {
      'target_name': 'xenia-debug',
      'type': 'executable',

      'msvs_settings': {
        'VCLinkerTool': {
          'SubSystem': '2'
        }
      },

      'dependencies': [
        'xdb',
        'xenia',
      ],

      'include_dirs': [
        '.',
      ],

      'sources': [
        'xenia-debug.cc',
      ],
    },
  ],
}
