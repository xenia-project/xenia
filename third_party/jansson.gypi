# Copyright 2013 Ben Vanik. All Rights Reserved.
{
  'targets': [
    {
      'target_name': 'jansson',
      'type': '<(library)',

      'direct_dependent_settings': {
        'include_dirs': [
          'jansson/win32/',
          'jansson/src/',
        ],

        'defines': [
        ],
      },

      'msvs_disabled_warnings': [4267],

      'defines': [
      ],

      'conditions': [
        ['OS != "win"', {
          'defines': [
          ],
        }],
        ['OS == "win"', {
          'defines': [
          ],
        }],
      ],

      'include_dirs': [
        'jansson/win32/',
        'jansson/src/',
      ],

      'sources': [
        'jansson/win32/jansson_config.h',
        'jansson/src/dump.c',
        'jansson/src/error.c',
        'jansson/src/hashtable.c',
        'jansson/src/hashtable.h',
        'jansson/src/jansson.h',
        'jansson/src/jansson_private.h',
        'jansson/src/load.c',
        'jansson/src/memory.c',
        'jansson/src/pack_unpack.c',
        'jansson/src/strbuffer.c',
        'jansson/src/strbuffer.h',
        'jansson/src/strconv.c',
        'jansson/src/utf.c',
        'jansson/src/utf.h',
        'jansson/src/value.c',
      ],
    }
  ]
}
