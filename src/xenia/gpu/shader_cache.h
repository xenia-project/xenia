/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_SHADER_CACHE_H_
#define XENIA_GPU_SHADER_CACHE_H_

#include <xenia/core.h>


namespace xe {
namespace gpu {


class ShaderCache {
public:
  ShaderCache();
  virtual ~ShaderCache();
};


}  // namespace gpu
}  // namespace xe


#endif  // XENIA_GPU_SHADER_CACHE_H_
