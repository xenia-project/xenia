# Copyright 2014 Ben Vanik. All Rights Reserved.
{
  'sources': [
    'gl4_gpu-private.h',
    'gl4_gpu.cc',
    'gl4_gpu.h',
    'gl4_graphics_system.cc',
    'gl4_graphics_system.h',
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
