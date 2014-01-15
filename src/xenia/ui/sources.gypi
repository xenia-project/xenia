# Copyright 2014 Ben Vanik. All Rights Reserved.
{
  'sources': [
    'menu_item.cc',
    'menu_item.h',
    'ui_event.h',
    'window.cc',
    'window.h',
  ],

  'conditions': [
    ['OS == "win"', {
      'includes': [
        'win32/sources.gypi',
      ],
    }],
  ],
}
