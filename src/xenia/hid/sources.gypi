# Copyright 2013 Ben Vanik. All Rights Reserved.
{
  'sources': [
    'hid-private.h',
    'hid.cc',
    'hid.h',
    'input_driver.cc',
    'input_driver.h',
    'input_system.cc',
    'input_system.h',
  ],

  'includes': [
    'nop/sources.gypi',
  ],

  'conditions': [
    ['OS == "win"', {
      'includes': [
        'xinput/sources.gypi',
      ],
    }],
  ],
}
