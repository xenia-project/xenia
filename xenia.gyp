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
          '<@(llvm_includedir)',
        ],

        'target_conditions': [
          ['_type=="shared_library"', {
            'cflags': [
              '<(llvm_cxxflags)',
            ],
          }],
          ['_type=="executable"', {
            'conditions': [
              ['OS == "win"', {
                'libraries': [
                  '<@(llvm_libs)',
                  'wsock32',
                ],
              }],
              ['OS == "mac"', {
                'xcode_settings': {
                  'OTHER_LDFLAGS': [
                    '<!@(<(llvm_config) --libs all)',
                  ],
                },
              }],
              ['OS == "linux"', {
                'libraries': [
                  '-L../../../<@(llvm_libdir)/',
                  '<!@(<(llvm_config) --libs all)',
                  '-lpthread',
                  '-ldl',
                ],
              }],
            ],
          }],
        ],
      },

      'cflags': [
        '<(llvm_cxxflags)',
      ],

      'include_dirs': [
        '.',
        'src/',
        '<(llvm_includedir)',
      ],

      'includes': [
        'src/xenia/sources.gypi',
      ],
    },
  ],
}
