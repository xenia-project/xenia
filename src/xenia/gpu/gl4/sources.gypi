# Copyright 2014 Ben Vanik. All Rights Reserved.
{
  'sources': [
    'circular_buffer.cc',
    'circular_buffer.h',
    'command_processor.cc',
    'command_processor.h',
    'gl4_gpu-private.h',
    'gl4_gpu.cc',
    'gl4_gpu.h',
    'gl4_graphics_system.cc',
    'gl4_graphics_system.h',
    'gl4_shader.cc',
    'gl4_shader.h',
    'gl4_shader_translator.cc',
    'gl4_shader_translator.h',
    'gl_context.cc',
    'gl_context.h',
  ],

  'conditions': [
    ['OS == "win"', {
      'sources': [
        'wgl_control.cc',
        'wgl_control.h',
      ],
    }],
  ],
}
