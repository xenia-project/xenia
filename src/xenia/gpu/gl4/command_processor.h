/**
******************************************************************************
* Xenia : Xbox 360 Emulator Research Project                                 *
******************************************************************************
* Copyright 2014 Ben Vanik. All rights reserved.                             *
* Released under the BSD license - see LICENSE in the root for more details. *
******************************************************************************
*/

#ifndef XENIA_GPU_GL4_COMMAND_PROCESSOR_H_
#define XENIA_GPU_GL4_COMMAND_PROCESSOR_H_

#include <atomic>
#include <functional>
#include <memory>
#include <thread>
#include <unordered_map>
#include <vector>

#include <xenia/gpu/gl4/gl_context.h>
#include <xenia/gpu/gl4/gl4_shader.h>
#include <xenia/gpu/register_file.h>
#include <xenia/gpu/xenos.h>
#include <xenia/memory.h>

namespace xe {
namespace gpu {
namespace gl4 {

class GL4GraphicsSystem;

// TODO(benvanik): move more of the enums in here?
struct DrawCommand {
  PrimitiveType prim_type;
  uint32_t start_index;
  uint32_t index_count;
  uint32_t base_vertex;

  GL4Shader* vertex_shader;
  GL4Shader* pixel_shader;

  // Index buffer, if present.
  // If index_count > 0 but buffer is nullptr then auto draw.
  //IndexBufferResource* index_buffer;
  void* index_buffer;

  // Vertex buffers.
  struct {
    uint32_t input_index;
    //VertexBufferResource* buffer;
    uint32_t stride;
    uint32_t offset;
  } vertex_buffers[96];
  size_t vertex_buffer_count;

  // Texture samplers.
  struct SamplerInput {
    uint32_t input_index;
    //TextureResource* texture;
    //SamplerStateResource* sampler_state;
  };
  SamplerInput vertex_shader_samplers[32];
  size_t vertex_shader_sampler_count;
  SamplerInput pixel_shader_samplers[32];
  size_t pixel_shader_sampler_count;
};

class CommandProcessor {
 public:
  CommandProcessor(GL4GraphicsSystem* graphics_system);
  ~CommandProcessor();

  void set_swap_handler(std::function<void()> fn) { swap_handler_ = fn; }

  uint64_t QueryTime();
  uint32_t counter() const { return counter_; }
  void increment_counter() { counter_++; }

  bool Initialize(std::unique_ptr<GLContext> context);
  void Shutdown();

  void InitializeRingBuffer(uint32_t ptr, uint32_t page_count);
  void EnableReadPointerWriteBack(uint32_t ptr, uint32_t block_size);

  void UpdateWritePointer(uint32_t value);

 private:
  class RingbufferReader;

  void WorkerMain();
  bool SetupGL();
  void ShutdownGL();

  void WriteRegister(uint32_t packet_ptr, uint32_t index, uint32_t value);
  void MakeCoherent();
  void PrepareForWait();

  void ExecutePrimaryBuffer(uint32_t start_index, uint32_t end_index);
  void ExecuteIndirectBuffer(uint32_t ptr, uint32_t length);
  bool ExecutePacket(RingbufferReader* reader);
  bool ExecutePacketType0(RingbufferReader* reader, uint32_t packet_ptr,
                          uint32_t packet);
  bool ExecutePacketType1(RingbufferReader* reader, uint32_t packet_ptr,
                          uint32_t packet);
  bool ExecutePacketType2(RingbufferReader* reader, uint32_t packet_ptr,
                          uint32_t packet);
  bool ExecutePacketType3(RingbufferReader* reader, uint32_t packet_ptr,
                          uint32_t packet);
  bool ExecutePacketType3_ME_INIT(RingbufferReader* reader, uint32_t packet_ptr,
                                  uint32_t packet, uint32_t count);
  bool ExecutePacketType3_NOP(RingbufferReader* reader, uint32_t packet_ptr,
                              uint32_t packet, uint32_t count);
  bool ExecutePacketType3_INTERRUPT(RingbufferReader* reader,
                                    uint32_t packet_ptr, uint32_t packet,
                                    uint32_t count);
  bool ExecutePacketType3_XE_SWAP(RingbufferReader* reader, uint32_t packet_ptr,
                                  uint32_t packet, uint32_t count);
  bool ExecutePacketType3_INDIRECT_BUFFER(RingbufferReader* reader,
                                          uint32_t packet_ptr, uint32_t packet,
                                          uint32_t count);
  bool ExecutePacketType3_WAIT_REG_MEM(RingbufferReader* reader,
                                       uint32_t packet_ptr, uint32_t packet,
                                       uint32_t count);
  bool ExecutePacketType3_REG_RMW(RingbufferReader* reader, uint32_t packet_ptr,
                                  uint32_t packet, uint32_t count);
  bool ExecutePacketType3_COND_WRITE(RingbufferReader* reader,
                                     uint32_t packet_ptr, uint32_t packet,
                                     uint32_t count);
  bool ExecutePacketType3_EVENT_WRITE(RingbufferReader* reader,
                                      uint32_t packet_ptr, uint32_t packet,
                                      uint32_t count);
  bool ExecutePacketType3_EVENT_WRITE_SHD(RingbufferReader* reader,
                                          uint32_t packet_ptr, uint32_t packet,
                                          uint32_t count);
  bool ExecutePacketType3_DRAW_INDX(RingbufferReader* reader,
                                    uint32_t packet_ptr, uint32_t packet,
                                    uint32_t count);
  bool ExecutePacketType3_DRAW_INDX_2(RingbufferReader* reader,
                                      uint32_t packet_ptr, uint32_t packet,
                                      uint32_t count);
  bool ExecutePacketType3_SET_CONSTANT(RingbufferReader* reader,
                                       uint32_t packet_ptr, uint32_t packet,
                                       uint32_t count);
  bool ExecutePacketType3_LOAD_ALU_CONSTANT(RingbufferReader* reader,
                                            uint32_t packet_ptr,
                                            uint32_t packet, uint32_t count);
  bool ExecutePacketType3_IM_LOAD(RingbufferReader* reader, uint32_t packet_ptr,
                                  uint32_t packet, uint32_t count);
  bool ExecutePacketType3_IM_LOAD_IMMEDIATE(RingbufferReader* reader,
                                            uint32_t packet_ptr,
                                            uint32_t packet, uint32_t count);
  bool ExecutePacketType3_INVALIDATE_STATE(RingbufferReader* reader,
                                           uint32_t packet_ptr, uint32_t packet,
                                           uint32_t count);

  bool LoadShader(ShaderType shader_type, const uint32_t* address,
                  uint32_t dword_count);

  bool PrepareDraw(DrawCommand* draw_command);
  bool UpdateState(DrawCommand* draw_command);
  bool UpdateRenderTargets();
  bool IssueDraw(DrawCommand* draw_command);

  Memory* memory_;
  uint8_t* membase_;
  GL4GraphicsSystem* graphics_system_;
  RegisterFile* register_file_;

  std::thread worker_thread_;
  std::atomic<bool> worker_running_;
  std::unique_ptr<GLContext> context_;
  std::function<void()> swap_handler_;

  uint64_t time_base_;
  uint32_t counter_;

  uint32_t primary_buffer_ptr_;
  uint32_t primary_buffer_size_;

  uint32_t read_ptr_index_;
  uint32_t read_ptr_update_freq_;
  uint32_t read_ptr_writeback_ptr_;

  HANDLE write_ptr_index_event_;
  std::atomic<uint32_t> write_ptr_index_;

  uint64_t bin_select_;
  uint64_t bin_mask_;

  std::vector<std::unique_ptr<GL4Shader>> all_shaders_;
  std::unordered_map<uint64_t, GL4Shader*> shader_cache_;
  GL4Shader* active_vertex_shader_;
  GL4Shader* active_pixel_shader_;

  GLuint uniform_data_buffer_;

  DrawCommand draw_command_;
};

}  // namespace gl4
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_GL4_COMMAND_PROCESSOR_H_
