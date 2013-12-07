# Copyright 2013 Ben Vanik. All Rights Reserved.
{
  'includes': [
    'tools/tools.gypi',
    'third_party/asmjit.gypi',
    'third_party/beaengine.gypi',
    'third_party/gflags.gypi',
    'third_party/sparsehash.gypi',
  ],

  'default_configuration': 'release',

  'variables': {
    'configurations': {
      'Debug': {
      },
      'Release': {
      },
    },

    'library%': 'static_library',
    'target_arch%': 'x64',
  },

  'target_defaults': {
    'include_dirs': [
      'include/',
    ],

    'defines': [
      '__STDC_LIMIT_MACROS=1',
      '__STDC_CONSTANT_MACROS=1',
      '_ISOC99_SOURCE=1',
    ],

    'conditions': [
      ['OS == "win"', {
        'defines': [
          '_WIN64=1',
          '_AMD64_=1',

          # HACK: it'd be nice to use the proper functions, when available.
          '_CRT_SECURE_NO_WARNINGS=1',
        ],
      }],
    ],

    'cflags': [
      #'-std=c99',
    ],

    'configurations': {
      'common_base': {
        'abstract': 1,

        'msvs_configuration_platform': 'x64',
        'msvs_configuration_attributes': {
          'OutputDirectory': '$(SolutionDir)$(ConfigurationName)',
          'IntermediateDirectory': '$(OutDir)\\obj\\$(ProjectName)',
          'CharacterSet': '1',
        },
        'msvs_disabled_warnings': [],
        'msvs_configuration_platform': 'x64',
        'msvs_cygwin_shell': '0',
        'msvs_settings': {
          'VCCLCompilerTool': {
            #'MinimalRebuild': 'true',
            'BufferSecurityCheck': 'true',
            'EnableFunctionLevelLinking': 'true',
            'RuntimeTypeInfo': 'false',
            'WarningLevel': '3',
            #'WarnAsError': 'true',
            'DebugInformationFormat': '3',
            'ExceptionHandling': '1', # /EHsc
            'AdditionalOptions': [
              #'/TP',    # Compile as C++
              '/EHsc',  # C++ exception handling,
            ],
          },
          'VCLinkerTool': {
            'GenerateDebugInformation': 'true',
            #'LinkIncremental': '1', # 1 = NO, 2 = YES
            'TargetMachine': '17', # x86 - 64
            'AdditionalLibraryDirectories': [
            ],
          },
        },

        'scons_settings': {
          'sconsbuild_dir': '<(DEPTH)/build/xenia/',
        },

        'xcode_settings': {
          'SYMROOT': '<(DEPTH)/build/xenia/',
          'ALWAYS_SEARCH_USER_PATHS': 'NO',
          'ARCHS': ['x86_64'],
          #'CLANG_CXX_LANGUAGE_STANDARD': 'c++0x',
          'COMBINE_HIDPI_IMAGES': 'YES',
          'GCC_C_LANGUAGE_STANDARD': 'gnu99',
          'GCC_SYMBOLS_PRIVATE_EXTERN': 'YES',
          #'GCC_TREAT_WARNINGS_AS_ERRORS': 'YES',
          'GCC_WARN_ABOUT_MISSING_NEWLINE': 'YES',
          'GCC_VERSION': 'com.apple.compilers.llvm.clang.1_0',
          'WARNING_CFLAGS': ['-Wall', '-Wendif-labels'],
          'LIBRARY_SEARCH_PATHS': [
          ],
        },

        'defines': [
        ],
      },

      'Debug': {
        'inherit_from': ['common_base',],
        'defines': [
          'DEBUG',
          'ASMJIT_DEBUG=',
        ],
        'msvs_configuration_attributes': {
          'OutputDirectory': '<(DEPTH)\\build\\xenia\\Debug',
        },
        'msvs_settings': {
          'VCCLCompilerTool': {
            'Optimization': '0',
            'BasicRuntimeChecks': '0',  # disable /RTC1 when compiling /O2
            'DebugInformationFormat': '3',
            'ExceptionHandling': '0',
            'RuntimeTypeInfo': 'false',
            'OmitFramePointers': 'false',
          },
          'VCLinkerTool': {
            'LinkIncremental': '2',
            'GenerateDebugInformation': 'true',
            'StackReserveSize': '2097152',
          },
        },
        'xcode_settings': {
          'GCC_OPTIMIZATION_LEVEL': '0',
        },
      },
      'Debug_x64': {
        'inherit_from': ['Debug',],
      },

      'Release': {
        'inherit_from': ['common_base',],
        'defines': [
          'RELEASE',
          'NDEBUG',
        ],
        'msvs_configuration_attributes': {
          'OutputDirectory': '<(DEPTH)\\build\\xenia\\release',
        },
        'msvs_settings': {
          'VCCLCompilerTool': {
            'Optimization': '2',
            'InlineFunctionExpansion': '2',
            'EnableIntrinsicFunctions': 'true',
            'FavorSizeOrSpeed': '0',
            'ExceptionHandling': '0',
            'RuntimeTypeInfo': 'false',
            'OmitFramePointers': 'false',
            'StringPooling': 'true',
          },
          'VCLinkerTool': {
            'LinkIncremental': '1',
            'GenerateDebugInformation': 'true',
            'OptimizeReferences': '2',
            'EnableCOMDATFolding': '2',
            'StackReserveSize': '2097152',
          },
        },
      },
      'Release_x64': {
        'inherit_from': ['Release',],
      },
    },
  },

  'targets': [
    {
      'target_name': 'alloy',
      'product_name': 'alloy',
      'type': 'static_library',

      'dependencies': [
        'gflags',
      ],
      'export_dependent_settings': [
        'gflags',
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
                  'kernel32',
                  'user32',
                  'ole32',
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
        'src/alloy/sources.gypi',
      ],
    },

    {
      'target_name': 'xenia',
      'product_name': 'xenia',
      'type': 'static_library',

      'dependencies': [
        'asmjit',
        'beaengine',
        'gflags',
        'alloy',
      ],
      'export_dependent_settings': [
        'asmjit',
        'beaengine',
        'gflags',
        'alloy',
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
                  'kernel32',
                  'user32',
                  'ole32',
                  'wsock32',
                  'dxgi',
                  'd3d11',
                  'd3dcompiler',
                  'xinput',
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
      'conditions': [
        ['OS == "win"', {
          'copies': [
            {
              'files': [
                # Depending on which SDK you have...
                '<(windows_sdk_dir)/redist/d3d/x64/d3dcompiler_47.dll',
              ],
              'destination': '<(PRODUCT_DIR)',
            },
          ],
        }],
      ],

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
