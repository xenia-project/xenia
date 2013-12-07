# Copyright 2013 Ben Vanik. All Rights Reserved.
{
  'targets': [
    {
      'target_name': 'alloy-sandbox',
      'type': 'executable',

      'dependencies': [
        'alloy',
        'xenia',
      ],

      'include_dirs': [
        '.',
      ],

      'sources': [
        'alloy-sandbox.cc',
      ],
    },
  ],
}
