# Copyright 2014 Ben Vanik. All Rights Reserved.
{
  'targets': [
    {
      'target_name': 'llvm',
      'type': '<(library)',

      'direct_dependent_settings': {
        'include_dirs': [
          'llvm/include/',
        ],

        'defines': [
        ],
      },

      'msvs_disabled_warnings': [4267],

      'defines': [
      ],

      'include_dirs': [
        'llvm/include/',
      ],

      'sources': [
        'llvm/dummy.cc',
        'llvm/include/llvm/ADT/BitVector.h',
        'llvm/include/llvm/Support/Compiler.h',
        'llvm/include/llvm/Support/MathExtras.h',
        'llvm/include/llvm/Support/type_traits.h',
      ],
    }
  ]
}
