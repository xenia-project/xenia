# Copyright 2014 Ben Vanik. All Rights Reserved.
{
  'sources': [
    'control.cc',
    'control.h',
    'loop.h',
    'menu_item.cc',
    'menu_item.h',
    'ui_event.h',
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
