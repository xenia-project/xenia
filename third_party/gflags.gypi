# Copyright 2013 Ben Vanik. All Rights Reserved.
{
  'targets': [
    {
      'target_name': 'gflags',
      'type': 'static_library',

      'direct_dependent_settings': {
        'include_dirs': [
          'gflags/src/',
        ],
      },

      'include_dirs': [
        'gflags/src/',
      ],

      'sources': [
        'gflags/src/gflags.cc',
        'gflags/src/gflags_completions.cc',
        'gflags/src/gflags_nc.cc',
        'gflags/src/gflags_reporting.cc',
      ],

      'conditions': [
        ['OS == "win"', {
          'sources!': [
            'gflags/src/windows/port.cc',
          ],
        }],
      ],
    }
  ]
}
