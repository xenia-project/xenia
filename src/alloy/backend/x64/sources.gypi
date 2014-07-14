# Copyright 2013 Ben Vanik. All Rights Reserved.
{
  'sources': [
    'x64_assembler.cc',
    'x64_assembler.h',
    'x64_backend.cc',
    'x64_backend.h',
    'x64_code_cache.h',
    'x64_emitter.cc',
    'x64_emitter.h',
    'x64_function.cc',
    'x64_function.h',
    'x64_sequence.inl',
    'x64_sequences.cc',
    'x64_sequences.h',
    'x64_thunk_emitter.cc',
    'x64_thunk_emitter.h',
    'x64_tracers.cc',
    'x64_tracers.h',
  ],

  'conditions': [
    ['OS == "mac" or OS == "linux"', {
      'sources': [
        'x64_code_cache_posix.cc',
      ],
    }],
    ['OS == "win"', {
      'sources': [
        'x64_code_cache_win.cc',
      ],
    }],
  ],
}
