# Copyright 2013 Ben Vanik. All Rights Reserved.
{
  'sources': [
    'assert.h',
    'atomic.h',
    'byte_order.h',
    'common.h',
    'config.h',
    'core.h',
    'logging.cc',
    'logging.h',
    'malloc.cc',
    'malloc.h',
    'platform.cc',
    'platform.h',
    'platform_includes.h',
    'string.cc',
    'string.h',
    'types.h',
    'xenia.h',
  ],

  'includes': [
    'core/sources.gypi',
    'cpu/sources.gypi',
    'dbg/sources.gypi',
    'gpu/sources.gypi',
    'kernel/sources.gypi',
  ],
}
