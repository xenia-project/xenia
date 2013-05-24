# Copyright 2013 Ben Vanik. All Rights Reserved.
{
  'targets': [
    {
      'target_name': 'beaengine',
      'type': '<(library)',

      'direct_dependent_settings': {
        'include_dirs': [
          'beaengine/include/',
        ],
        'defines': [
          'BEA_ENGINE_STATIC=1',
        ],
      },

      'sources': [
        'beaengine/beaengineSources/BeaEngine.c',
      ],

      'include_dirs': [
        'beaengine/include/',
        'beaengine/beaengineSources/',
      ],

      'defines': [
        'BEA_ENGINE_STATIC=1',
        #'BEA_LIGHT_DISASSEMBLY=1',
      ],
    }
  ]
}
