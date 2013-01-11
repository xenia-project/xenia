# Copyright 2013 Ben Vanik. All Rights Reserved.
{
  'includes': [
    'common.gypi',
    'tools/tools.gypi',
  ],

  'targets': [
    {
      'target_name': 'xeniacore',
      'product_name': 'xeniacore',
      'type': 'static_library',

      'direct_dependent_settings': {
        'include_dirs': [
          'include/',
        ],
        'link_settings': {
          'libraries': [
            '<!@(<(llvm_config) --ldflags)',
            '<!@(<(llvm_config) --libs core)',
          ],
          'library_dirs': [
            # NOTE: this doesn't actually do anything...
            # http://code.google.com/p/gyp/issues/detail?id=130
            '<!@(<(llvm_config) --libdir)',
          ],
        },
        'linkflags': [
        ],
        'libraries': [
          #'!@(pkg-config --libs-only-l apr-1)',
        ],
      },

      'cflags': [
        '<!@(<(llvm_config) --cxxflags)'
      ],

      'defines': [
        '__STDC_LIMIT_MACROS=1',
        '__STDC_CONSTANT_MACROS=1',
      ],

      'include_dirs': [
        '.',
        '<!@(<(llvm_config) --includedir)',
      ],

      'includes': [
        'src/core/sources.gypi',
      ],
    },
  ],
}
