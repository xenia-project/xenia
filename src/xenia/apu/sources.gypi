# Copyright 2013 Ben Vanik. All Rights Reserved.
{
  'sources': [
    'apu-private.h',
    'apu.cc',
    'apu.h',
    'audio_driver.cc',
    'audio_driver.h',
    'audio_system.cc',
    'audio_system.h',
  ],

  'includes': [
    'nop/sources.gypi',
  ],

  'conditions': [
    ['OS == "win"', {
      'includes': [
        'xaudio2/sources.gypi',
      ],
    }],
  ],
}
