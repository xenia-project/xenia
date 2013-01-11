# Copyright 2013 Ben Vanik. All Rights Reserved.
{
  'default_configuration': 'release',

  'variables': {
    'configurations': {
      'debug': {
      },
      'release': {
      },
    },

    # LLVM paths.
    'llvm_path': 'build/llvm/release/',
    'llvm_config': '<(llvm_path)bin/llvm-config',
  },


  'target_defaults': {
    'include_dirs': [
      'include/',
    ],

    'configurations': {
      'debug': {
        'defines': [
          'DEBUG',
        ],
        'msvs_configuration_attributes': {
          'OutputDirectory': '<(DEPTH)\\build\\xenia\\debug',
        },
        'msvs_settings': {
          'VCCLCompilerTool': {
            'Optimization': '2',
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
      },
      'release': {
        'defines': [
          'RELEASE',
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
    },

    'msvs_configuration_platform': 'x64',
    'msvs_cygwin_shell': '0',
    'scons_settings': {
      'sconsbuild_dir': '<(DEPTH)/build/xenia/',
    },
    'xcode_settings': {
      'SYMROOT': '<(DEPTH)/build/xenia/',
      'ALWAYS_SEARCH_USER_PATHS': 'NO',
      'ARCHS': ['x86_64'],
      'GCC_C_LANGUAGE_STANDARD': 'c99',
      'GCC_TREAT_WARNINGS_AS_ERRORS': 'YES',
      'GCC_WARN_ABOUT_MISSING_NEWLINE': 'YES',
      'WARNING_CFLAGS': ['-Wall', '-Wendif-labels'],
    },
  },
}
