# Copyright 2014 Ben Vanik. All Rights Reserved.
{
  'targets': [
    {
      'target_name': 'xe-cpu-ppc-test',
      'type': 'executable',

      'msvs_settings': {
        'VCLinkerTool': {
          'SubSystem': '1',
        },
      },

      'dependencies': [
        'libxenia',
      ],

      'include_dirs': [
        '.',
      ],

      'sources': [
        'xe-cpu-ppc-test.cc',
      ],
    },
  ],
}
