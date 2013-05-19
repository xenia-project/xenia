# Copyright 2013 Ben Vanik. All Rights Reserved.
{
  'includes': [
    'tools/tools.gypi',
    'third_party/gflags.gypi',
    'third_party/sparsehash.gypi',
    'third_party/wslay.gypi',
  ],

  'targets': [
    {
      'target_name': 'xenia',
      'product_name': 'xenia',
      'type': '<(library)',

      'dependencies': [
        'gflags',
        'wslay',
        'third_party/libjit/libjit.gyp:libjit',
      ],
      'export_dependent_settings': [
        'gflags',
        'wslay',
        'third_party/libjit/libjit.gyp:libjit',
      ],

      'direct_dependent_settings': {
        'include_dirs': [
          'src/',
        ],

        'target_conditions': [
          ['_type=="shared_library"', {
            'cflags': [
            ],
          }],
          ['_type=="executable"', {
            'conditions': [
              ['OS == "win"', {
                'libraries': [
                  'wsock32',
                ],
              }],
              ['OS == "mac"', {
                'xcode_settings': {
                  'OTHER_LDFLAGS': [
                  ],
                },
              }],
              ['OS == "linux"', {
                'libraries': [
                  '-lpthread',
                  '-ldl',
                ],
              }],
            ],
          }],
        ],
      },

      'cflags': [
      ],

      'include_dirs': [
        '.',
        'src/',
      ],

      'includes': [
        'src/xenia/sources.gypi',
      ],
    },
  ],
}
