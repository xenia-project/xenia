# Copyright 2013 Ben Vanik. All Rights Reserved.
{
  'sources': [
    'debug_agent.cc',
    'debug_agent.h',
    'emulator.cc',
    'emulator.h',
    'export_resolver.cc',
    'export_resolver.h',
    'logging.cc',
    'logging.h',
    'memory.cc',
    'memory.h',
    'profiling.cc',
    'profiling.h',
    'xbox.h',
    # xenia_main.cc is purposefully omitted as it's used in another target.
  ],

  'includes': [
    'apu/sources.gypi',
    'cpu/sources.gypi',
    'gpu/sources.gypi',
    'hid/sources.gypi',
    'kernel/sources.gypi',
    'ui/sources.gypi',
  ],
}
