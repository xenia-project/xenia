# Copyright 2013 Ben Vanik. All Rights Reserved.
{
  'sources': [
    'buffer_resource.cc',
    'buffer_resource.h',
    'command_processor.cc',
    'command_processor.h',
    'draw_command.cc',
    'draw_command.h',
    'gpu-private.h',
    'gpu.cc',
    'gpu.h',
    'graphics_driver.cc',
    'graphics_driver.h',
    'graphics_system.cc',
    'graphics_system.h',
    'register_file.cc',
    'register_file.h',
    'resource.cc',
    'resource.h',
    'resource_cache.cc',
    'resource_cache.h',
    'sampler_state_resource.cc',
    'sampler_state_resource.h',
    'shader_resource.cc',
    'shader_resource.h',
    'texture_resource.cc',
    'texture_resource.h',
  ],

  'includes': [
    'nop/sources.gypi',
    'xenos/sources.gypi',
  ],

  'conditions': [
    ['OS == "win"', {
      'includes': [
        'd3d11/sources.gypi',
      ],
    }],
  ],
}
