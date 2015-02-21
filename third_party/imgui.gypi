# Copyright 2015 Ben Vanik. All Rights Reserved.
{
  'targets': [
    {
      'target_name': 'imgui',
      'type': '<(library)',

      'direct_dependent_settings': {
        'include_dirs': [
          'imgui/',
        ],
      },

      'include_dirs': [
        'imgui/',
      ],

      'sources': [
        'imgui/imconfig.h',
        'imgui/imgui.cpp',
        'imgui/imgui.h',
        'imgui/stb_rect_pack.h',
        'imgui/stb_textedit.h',
        'imgui/stb_truetype.h',
      ],
    }
  ]
}
