# Copyright 2014 Ben Vanik. All Rights Reserved.
{
  'sources': [
    'cursor.cc',
    'cursor.h',
    'debug_target.cc',
    'debug_target.h',
    'module.cc',
    'module.h',
    'postmortem_cursor.cc',
    'postmortem_cursor.h',
    'postmortem_debug_target.cc',
    'postmortem_debug_target.h',
    'protocol.h',
    'thread.cc',
    'thread.h',
    'xdb.cc',
    'xdb.h',
  ],

  'includes': [
    'sym/sources.gypi',
    'ui/sources.gypi',
  ],
}
