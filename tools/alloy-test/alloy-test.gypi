# Copyright 2014 Ben Vanik. All Rights Reserved.
{
  'targets': [
    {
      'target_name': 'alloy-test',
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
        'alloy-test.cc',
        'test_add.cc',
        'test_vector_add.cc',
        'test_vector_max.cc',
        'test_vector_min.cc',
        'util.h',
      ],
    },
  ],
}
