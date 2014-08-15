# Copyright 2014 Ben Vanik. All Rights Reserved.
{
  'targets': [
    {
      'target_name': 'xenia-compare',
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
        'xenia-compare.cc',
      ],
    },
  ],
}
