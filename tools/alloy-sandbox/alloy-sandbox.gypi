# Copyright 2013 Ben Vanik. All Rights Reserved.
{
  'targets': [
    {
      'target_name': 'alloy-sandbox',
      'type': 'executable',

      'msvs_settings': {
        'VCLinkerTool': {
          'SubSystem': '1'
        },
      },

      'dependencies': [
        'alloy',
      ],

      'include_dirs': [
        '.',
      ],

      'sources': [
        'alloy-sandbox.cc',
      ],
    },
  ],
}
