/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_TRACE_DUMP_H_
#define XENIA_GPU_TRACE_DUMP_H_

#include <string>

#include "xenia/emulator.h"
#include "xenia/gpu/shader.h"
#include "xenia/gpu/trace_player.h"
#include "xenia/gpu/trace_protocol.h"
#include "xenia/gpu/xenos.h"
#include "xenia/memory.h"

namespace xe {
namespace ui {
class Loop;
class Window;
}  // namespace ui
}  // namespace xe

namespace xe {
namespace gpu {

struct SamplerInfo;
struct TextureInfo;

class TraceDump {
 public:
  virtual ~TraceDump();

  int Main(const std::vector<std::wstring>& args);

 protected:
  TraceDump();

  virtual std::unique_ptr<gpu::GraphicsSystem> CreateGraphicsSystem() = 0;

  virtual uintptr_t GetColorRenderTarget(uint32_t pitch, MsaaSamples samples,
                                         uint32_t base,
                                         ColorRenderTargetFormat format) = 0;
  virtual uintptr_t GetDepthRenderTarget(uint32_t pitch, MsaaSamples samples,
                                         uint32_t base,
                                         DepthRenderTargetFormat format) = 0;
  virtual uintptr_t GetTextureEntry(const TextureInfo& texture_info,
                                    const SamplerInfo& sampler_info) = 0;

  std::unique_ptr<xe::ui::Loop> loop_;
  std::unique_ptr<xe::ui::Window> window_;
  std::unique_ptr<Emulator> emulator_;
  Memory* memory_ = nullptr;
  GraphicsSystem* graphics_system_ = nullptr;
  std::unique_ptr<TracePlayer> player_;

 private:
  bool Setup();
  bool Load(std::wstring trace_file_path);
  void Run();

  std::wstring trace_file_path_;
  std::wstring base_output_path_;
};

}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_TRACE_DUMP_H_
