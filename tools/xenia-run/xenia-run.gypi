# Copyright 2013 Ben Vanik. All Rights Reserved.
{
  'targets': [
    {
      'target_name': 'xenia-run',
      'type': 'executable',

      'msvs_settings': {
        'VCLinkerTool': {
          #'SubSystem': '2'
        }
      },

      'dependencies': [
        'xenia',
      ],

      'include_dirs': [
        '.',
      ],

      'sources': [
        'xenia-run.cc',
      ],
    },
  ],
}
