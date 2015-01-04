# Copyright 2014 Ben Vanik. All Rights Reserved.
{
  'sources': [
    'circular_buffer.cc',
    'circular_buffer.h',
    'command_processor.cc',
    'command_processor.h',
    'draw_batcher.cc',
    'draw_batcher.h',
    'gl4_gpu-private.h',
    'gl4_gpu.cc',
    'gl4_gpu.h',
    'gl4_graphics_system.cc',
    'gl4_graphics_system.h',
    'gl4_profiler_display.cc',
    'gl4_profiler_display.h',
    'gl4_shader.cc',
    'gl4_shader.h',
    'gl4_shader_translator.cc',
    'gl4_shader_translator.h',
    'gl_context.cc',
    'gl_context.h',
    'texture_cache.cc',
    'texture_cache.h',
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
