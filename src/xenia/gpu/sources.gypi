# Copyright 2013 Ben Vanik. All Rights Reserved.
{
  'sources': [
    'gpu-private.h',
    'gpu.cc',
    'gpu.h',
    'graphics_system.cc',
    'graphics_system.h',
    'register_file.cc',
    'register_file.h',
    'register_table.inc',
    'sampler_info.cc',
    'sampler_info.h',
    'shader.cc',
    'shader.h',
    'texture_info.cc',
    'texture_info.h',
    'ucode.h',
    'ucode_disassembler.cc',
    'ucode_disassembler.h',
    'xenos.h',
  ],

  'includes': [
    'gl4/sources.gypi',
  ],
}
