/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_METAL_METAL_GEOMETRY_SHADER_H_
#define XENIA_GPU_METAL_METAL_GEOMETRY_SHADER_H_

#include <cstdint>
#include <functional>
#include <vector>

#include "xenia/base/assert.h"
#include "xenia/gpu/dxbc_shader_translator.h"

namespace xe {
namespace gpu {
namespace metal {

enum class PipelineGeometryShader : uint32_t {
  kNone,
  kPointList,
  kRectangleList,
  kQuadList,
};

union GeometryShaderKey {
  uint32_t key;
  struct {
    PipelineGeometryShader type : 2;
    uint32_t interpolator_count : 5;
    uint32_t user_clip_plane_count : 3;
    uint32_t user_clip_plane_cull : 1;
    uint32_t has_vertex_kill_and : 1;
    uint32_t has_point_size : 1;
    uint32_t has_point_coordinates : 1;
  };

  GeometryShaderKey() : key(0) { static_assert_size(*this, sizeof(key)); }

  struct Hasher {
    size_t operator()(const GeometryShaderKey& key) const {
      return std::hash<uint32_t>{}(key.key);
    }
  };
  bool operator==(const GeometryShaderKey& other_key) const {
    return key == other_key.key;
  }
  bool operator!=(const GeometryShaderKey& other_key) const {
    return !(*this == other_key);
  }
};

bool GetGeometryShaderKey(
    PipelineGeometryShader geometry_shader_type,
    DxbcShaderTranslator::Modification vertex_shader_modification,
    DxbcShaderTranslator::Modification pixel_shader_modification,
    GeometryShaderKey& key_out);

const std::vector<uint32_t>& GetGeometryShader(GeometryShaderKey key);

}  // namespace metal
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_METAL_METAL_GEOMETRY_SHADER_H_
