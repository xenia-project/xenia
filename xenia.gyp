# Copyright 2013 Ben Vanik. All Rights Reserved.
{
  'includes': [
    'common.gypi',
    'tools/tools.gypi',
    'third_party/gflags.gypi',
    'third_party/sparsehash.gypi',
  ],

  'targets': [
    {
      'target_name': 'xeniacore',
      'product_name': 'xeniacore',
      'type': 'static_library',

      'dependencies': [
        'gflags',
      ],
      'export_dependent_settings': [
        'gflags',
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
      ],
    },

    {
      'target_name': 'xeniakernel',
      'product_name': 'xeniakernel',
      'type': 'static_library',

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
      'type': 'static_library',

      'dependencies': [
        'xeniacore',
      ],

      'export_dependent_settings': [
        'xeniacore',
      ],

      'direct_dependent_settings': {
        'include_dirs': [
          '<!@(<(llvm_config) --includedir)',
        ],

        'target_conditions': [
          ['_type=="shared_library"', {
            'cflags': [
              '<!@(<(llvm_config) --cxxflags)'
            ],
          }],
          ['_type=="executable"', {
            'libraries': [
              '<!@(<(llvm_config) --ldflags)',
              '<!@(<(llvm_config) --libs all)',
            ],
            'library_dirs': [
              # NOTE: this doesn't actually do anything...
              # http://code.google.com/p/gyp/issues/detail?id=130
              '<!@(<(llvm_config) --libdir)',
            ],
            'xcode_settings': {
              'OTHER_LDFLAGS': [
                '<!@(<(llvm_config) --ldflags)',
                '<!@(<(llvm_config) --libs all)',
              ],
            },
          }],
        ],
      },

      'cflags': [
        '<!@(<(llvm_config) --cxxflags)'
      ],

      'include_dirs': [
        '.',
        'src/',
        '<!@(<(llvm_config) --includedir)',
      ],

      'includes': [
        'src/cpu/sources.gypi',
      ],
    },

    {
      'target_name': 'xeniagpu',
      'product_name': 'xeniagpu',
      'type': 'static_library',

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
