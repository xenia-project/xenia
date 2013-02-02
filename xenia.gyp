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
      'target_name': 'xeniacore',
      'product_name': 'xeniacore',
      'type': '<(library)',

      'dependencies': [
        'gflags',
        'wslay',
        './third_party/openssl/openssl.gyp:openssl',
      ],
      'export_dependent_settings': [
        'gflags',
        'wslay',
      ],

      'direct_dependent_settings': {
        'include_dirs': [
          'include/',
        ],
      },

      'include_dirs': [
        '.',
        'src/',
      ],

      'includes': [
        'src/xenia/sources.gypi',
        'src/core/sources.gypi',
        'src/dbg/sources.gypi',
      ],
    },

    {
      'target_name': 'xeniakernel',
      'product_name': 'xeniakernel',
      'type': '<(library)',

      'dependencies': [
        'xeniacore',
      ],
      'export_dependent_settings': [
        'xeniacore',
      ],

      'include_dirs': [
        '.',
        'src/',
      ],

      'includes': [
        'src/kernel/sources.gypi',
      ],
    },

    {
      'target_name': 'xeniacpu',
      'product_name': 'xeniacpu',
      'type': '<(library)',

      'dependencies': [
        'xeniacore',
      ],

      'export_dependent_settings': [
        'xeniacore',
      ],

      'direct_dependent_settings': {
        'include_dirs': [
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
              ['OS != "mac"', {
                'libraries': [
                  '<@(llvm_libs)',
                ],
              }],
              ['OS == "mac"', {
                'xcode_settings': {
                  'OTHER_LDFLAGS': [
                    '<!@(<(llvm_config) --libs all)',
                  ],
                },
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
        'src/cpu/sources.gypi',
      ],
    },

    {
      'target_name': 'xeniagpu',
      'product_name': 'xeniagpu',
      'type': '<(library)',

      'dependencies': [
        'xeniacore',
      ],

      'export_dependent_settings': [
        'xeniacore',
      ],

      'include_dirs': [
        '.',
        'src/',
      ],

      'includes': [
        'src/gpu/sources.gypi',
      ],
    },
  ],
}
