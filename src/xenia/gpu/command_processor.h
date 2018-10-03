/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_COMMAND_PROCESSOR_H_
#define XENIA_GPU_COMMAND_PROCESSOR_H_

#include <atomic>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <vector>

#include "xenia/base/ring_buffer.h"
#include "xenia/base/threading.h"
#include "xenia/gpu/register_file.h"
#include "xenia/gpu/trace_writer.h"
#include "xenia/gpu/xenos.h"
#include "xenia/kernel/xthread.h"
#include "xenia/memory.h"
#include "xenia/ui/graphics_context.h"

namespace xe {

class ByteStream;

namespace gpu {

class GraphicsSystem;
class Shader;

struct SwapState {
  // Lock must be held when changing data in this structure.
  std::mutex mutex;
  // Dimensions of the framebuffer textures. Should match window size.
  uint32_t width = 0;
  uint32_t height = 0;
  // Current front buffer, being drawn to the screen.
  uintptr_t front_buffer_texture = 0;
  // Current back buffer, being updated by the CP.
  uintptr_t back_buffer_texture = 0;
  // Backend data
  void* backend_data = nullptr;
  // Whether the back buffer is dirty and a swap is pending.
  bool pending = false;
};

enum class SwapMode {
  kNormal,
  kIgnored,
};

enum class GammaRampType {
  kUnknown = 0,
  kNormal,
  kPWL,
};

struct GammaRamp {
  struct NormalEntry {
    union {
      struct {
        uint32_t r : 10;
        uint32_t g : 10;
        uint32_t b : 10;
        uint32_t : 2;
      };
      uint32_t value;
    };
  };

  struct PWLValue {
    union {
      struct {
        uint16_t base;
        uint16_t delta;
      };
      uint32_t value;
    };
  };

  struct PWLEntry {
    union {
      struct {
        PWLValue r;
        PWLValue g;
        PWLValue b;
      };
      PWLValue values[3];
    };
  };

  NormalEntry normal[256];
  PWLEntry pwl[128];
};

class CommandProcessor {
 public:
  CommandProcessor(GraphicsSystem* graphics_system,
                   kernel::KernelState* kernel_state);
  virtual ~CommandProcessor();

  uint32_t counter() const { return counter_; }
  void increment_counter() { counter_++; }

  Shader* active_vertex_shader() const { return active_vertex_shader_; }
  Shader* active_pixel_shader() const { return active_pixel_shader_; }

  virtual bool Initialize(std::unique_ptr<xe::ui::GraphicsContext> context);
  virtual void Shutdown();

  void CallInThread(std::function<void()> fn);

  virtual void ClearCaches();

  SwapState& swap_state() { return swap_state_; }
  void set_swap_mode(SwapMode swap_mode) { swap_mode_ = swap_mode; }
  void IssueSwap(uint32_t frontbuffer_ptr, uint32_t frontbuffer_width,
                 uint32_t frontbuffer_height);

  void set_swap_request_handler(std::function<void()> fn) {
    swap_request_handler_ = fn;
  }

  virtual void RequestFrameTrace(const std::wstring& root_path);
  virtual void BeginTracing(const std::wstring& root_path);
  virtual void EndTracing();

  void InitializeRingBuffer(uint32_t ptr, uint32_t page_count);
  void EnableReadPointerWriteBack(uint32_t ptr, uint32_t block_size);

  void UpdateWritePointer(uint32_t value);

  void ExecutePacket(uint32_t ptr, uint32_t count);

  bool is_paused() const { return paused_; }
  void Pause();
  void Resume();

  bool Save(ByteStream* stream);
  bool Restore(ByteStream* stream);

 protected:
  struct IndexBufferInfo {
    IndexFormat format = IndexFormat::kInt16;
    Endian endianness = Endian::kUnspecified;
    uint32_t count = 0;
    uint32_t guest_base = 0;
    size_t length = 0;
  };

  void WorkerThreadMain();
  virtual bool SetupContext() = 0;
  virtual void ShutdownContext() = 0;

  virtual void WriteRegister(uint32_t index, uint32_t value);

  void UpdateGammaRampValue(GammaRampType type, uint32_t value);

  virtual void MakeCoherent();
  virtual void PrepareForWait();
  virtual void ReturnFromWait();

  virtual void PerformSwap(uint32_t frontbuffer_ptr, uint32_t frontbuffer_width,
                           uint32_t frontbuffer_height) = 0;

  uint32_t ExecutePrimaryBuffer(uint32_t start_index, uint32_t end_index);
  void ExecuteIndirectBuffer(uint32_t ptr, uint32_t length);
  bool ExecutePacket(RingBuffer* reader);
  bool ExecutePacketType0(RingBuffer* reader, uint32_t packet);
  bool ExecutePacketType1(RingBuffer* reader, uint32_t packet);
  bool ExecutePacketType2(RingBuffer* reader, uint32_t packet);
  bool ExecutePacketType3(RingBuffer* reader, uint32_t packet);
  bool ExecutePacketType3_ME_INIT(RingBuffer* reader, uint32_t packet,
                                  uint32_t count);
  bool ExecutePacketType3_NOP(RingBuffer* reader, uint32_t packet,
                              uint32_t count);
  bool ExecutePacketType3_INTERRUPT(RingBuffer* reader, uint32_t packet,
                                    uint32_t count);
  bool ExecutePacketType3_XE_SWAP(RingBuffer* reader, uint32_t packet,
                                  uint32_t count);
  bool ExecutePacketType3_INDIRECT_BUFFER(RingBuffer* reader, uint32_t packet,
                                          uint32_t count);
  bool ExecutePacketType3_WAIT_REG_MEM(RingBuffer* reader, uint32_t packet,
                                       uint32_t count);
  bool ExecutePacketType3_REG_RMW(RingBuffer* reader, uint32_t packet,
                                  uint32_t count);
  bool ExecutePacketType3_REG_TO_MEM(RingBuffer* reader, uint32_t packet,
                                     uint32_t count);
  bool ExecutePacketType3_MEM_WRITE(RingBuffer* reader, uint32_t packet,
                                    uint32_t count);
  bool ExecutePacketType3_COND_WRITE(RingBuffer* reader, uint32_t packet,
                                     uint32_t count);
  bool ExecutePacketType3_EVENT_WRITE(RingBuffer* reader, uint32_t packet,
                                      uint32_t count);
  bool ExecutePacketType3_EVENT_WRITE_SHD(RingBuffer* reader, uint32_t packet,
                                          uint32_t count);
  bool ExecutePacketType3_EVENT_WRITE_EXT(RingBuffer* reader, uint32_t packet,
                                          uint32_t count);
  bool ExecutePacketType3_EVENT_WRITE_ZPD(RingBuffer* reader, uint32_t packet,
                                          uint32_t count);
  bool ExecutePacketType3_DRAW_INDX(RingBuffer* reader, uint32_t packet,
                                    uint32_t count);
  bool ExecutePacketType3_DRAW_INDX_2(RingBuffer* reader, uint32_t packet,
                                      uint32_t count);
  bool ExecutePacketType3_SET_CONSTANT(RingBuffer* reader, uint32_t packet,
                                       uint32_t count);
  bool ExecutePacketType3_SET_CONSTANT2(RingBuffer* reader, uint32_t packet,
                                        uint32_t count);
  bool ExecutePacketType3_LOAD_ALU_CONSTANT(RingBuffer* reader, uint32_t packet,
                                            uint32_t count);
  bool ExecutePacketType3_SET_SHADER_CONSTANTS(RingBuffer* reader,
                                               uint32_t packet, uint32_t count);
  bool ExecutePacketType3_IM_LOAD(RingBuffer* reader, uint32_t packet,
                                  uint32_t count);
  bool ExecutePacketType3_IM_LOAD_IMMEDIATE(RingBuffer* reader,

                                            uint32_t packet, uint32_t count);
  bool ExecutePacketType3_INVALIDATE_STATE(RingBuffer* reader, uint32_t packet,
                                           uint32_t count);
  bool ExecutePacketType3_VIZ_QUERY(RingBuffer* reader, uint32_t packet,
                                    uint32_t count);

  virtual Shader* LoadShader(ShaderType shader_type, uint32_t guest_address,
                             const uint32_t* host_address,
                             uint32_t dword_count) = 0;

  virtual bool IssueDraw(PrimitiveType prim_type, uint32_t index_count,
                         IndexBufferInfo* index_buffer_info) = 0;
  virtual bool IssueCopy() = 0;

  Memory* memory_ = nullptr;
  kernel::KernelState* kernel_state_ = nullptr;
  GraphicsSystem* graphics_system_ = nullptr;
  RegisterFile* register_file_ = nullptr;

  TraceWriter trace_writer_;
  enum class TraceState {
    kDisabled,
    kStreaming,
    kSingleFrame,
  };
  TraceState trace_state_ = TraceState::kDisabled;
  std::wstring trace_stream_path_;
  std::wstring trace_frame_path_;

  std::atomic<bool> worker_running_;
  kernel::object_ref<kernel::XHostThread> worker_thread_;

  std::unique_ptr<xe::ui::GraphicsContext> context_;
  SwapMode swap_mode_ = SwapMode::kNormal;
  SwapState swap_state_;
  std::function<void()> swap_request_handler_;
  std::queue<std::function<void()>> pending_fns_;

  // MicroEngine binary from PM4_ME_INIT
  std::vector<uint32_t> me_bin_;

  uint32_t counter_ = 0;

  uint32_t primary_buffer_ptr_ = 0;
  uint32_t primary_buffer_size_ = 0;

  uint32_t read_ptr_index_ = 0;
  uint32_t read_ptr_update_freq_ = 0;
  uint32_t read_ptr_writeback_ptr_ = 0;

  std::unique_ptr<xe::threading::Event> write_ptr_index_event_;
  std::atomic<uint32_t> write_ptr_index_;

  uint64_t bin_select_ = 0xFFFFFFFFull;
  uint64_t bin_mask_ = 0xFFFFFFFFull;

  Shader* active_vertex_shader_ = nullptr;
  Shader* active_pixel_shader_ = nullptr;

  bool paused_ = false;

  GammaRamp gamma_ramp_ = {};
  int gamma_ramp_rw_subindex_ = 0;
  bool dirty_gamma_ramp_normal_ = true;
  bool dirty_gamma_ramp_pwl_ = true;
};

}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_COMMAND_PROCESSOR_H_
