# Copyright 2013 Ben Vanik. All Rights Reserved.
{
  'targets': [
    {
      'target_name': 'sparsehash',
      'type': 'static_library',

      'direct_dependent_settings': {
        'include_dirs': [
          'sparsehash/src/',
        ],
      },

      'include_dirs': [
        'sparsehash/src/',
      ],

      'sources': [
      ],

      'conditions': [
        ['OS == "win"', {
          'sources!': [
            'sparsehash/src/windows/port.cc',
          ],
        }],
      ],
    }
  ]
}
