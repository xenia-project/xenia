# Copyright 2014 Ben Vanik. All Rights Reserved.
{
  'targets': [
    {
      'target_name': 'glew',
      'type': '<(library)',

      'direct_dependent_settings': {
        'include_dirs': [
          'GL/',
        ],
        'defines': [
          'GLEW_STATIC=1',
          'GLEW_MX=1',
        ],
      },

      'include_dirs': [
        'GL/',
      ],

      'defines': [
        'GLEW_STATIC=1',
        'GLEW_MX=1',
      ],

      'sources': [
        # Khronos sources:
        'GL/glcorearb.h',
        'GL/glext.h',
        'GL/glxext.h',
        'GL/wglext.h',
        # GLEW sources:
        'GL/glew.c',
        'GL/glew.h',
        'GL/glxew.h',
        'GL/wglew.h',
      ],
    }
  ]
}
