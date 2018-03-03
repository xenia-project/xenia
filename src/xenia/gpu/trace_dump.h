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

  std::unique_ptr<Emulator> emulator_;
  GraphicsSystem* graphics_system_ = nullptr;
  std::unique_ptr<TracePlayer> player_;

 private:
  bool Setup();
  bool Load(std::wstring trace_file_path);
  int Run();

  std::wstring trace_file_path_;
  std::wstring base_output_path_;
};

}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_TRACE_DUMP_H_
