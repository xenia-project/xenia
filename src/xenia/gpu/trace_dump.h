/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
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
namespace gpu {

struct SamplerInfo;
struct TextureInfo;

class TraceDump {
 public:
  virtual ~TraceDump();

  int Main(const std::vector<std::string>& args);

 protected:
  TraceDump();

  virtual std::unique_ptr<gpu::GraphicsSystem> CreateGraphicsSystem() = 0;

  virtual void BeginHostCapture() = 0;
  virtual void EndHostCapture() = 0;

  std::unique_ptr<Emulator> emulator_;
  GraphicsSystem* graphics_system_ = nullptr;
  std::unique_ptr<TracePlayer> player_;

 private:
  bool Setup();
  bool Load(const std::filesystem::path& trace_file_path);
  int Run();

  std::filesystem::path trace_file_path_;
  std::filesystem::path base_output_path_;
};

}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_TRACE_DUMP_H_
