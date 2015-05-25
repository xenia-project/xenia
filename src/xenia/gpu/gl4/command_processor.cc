/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/gl4/command_processor.h"

#include <algorithm>

#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/gpu/gl4/gl4_gpu-private.h"
#include "xenia/gpu/gl4/gl4_graphics_system.h"
#include "xenia/gpu/gpu-private.h"
#include "xenia/gpu/sampler_info.h"
#include "xenia/gpu/texture_info.h"
#include "xenia/gpu/xenos.h"
#include "xenia/emulator.h"
#include "xenia/kernel/objects/xthread.h"
#include "xenia/profiling.h"

#include "third_party/xxhash/xxhash.h"

namespace xe {
namespace gpu {
namespace gl4 {

using namespace xe::gpu::xenos;

extern "C" GLEWContext* glewGetContext();

const GLuint kAnyTarget = UINT_MAX;

// All uncached vertex/index data goes here. If it fills up we need to sync
// with the GPU, so this should be large enough to prevent that in a normal
// frame.
const size_t kScratchBufferCapacity = 256 * 1024 * 1024;
const size_t kScratchBufferAlignment = 256;

CommandProcessor::CachedPipeline::CachedPipeline()
    : vertex_program(0), fragment_program(0), handles({0}) {}

CommandProcessor::CachedPipeline::~CachedPipeline() {
  glDeleteProgramPipelines(1, &handles.default_pipeline);
  glDeleteProgramPipelines(1, &handles.point_list_pipeline);
  glDeleteProgramPipelines(1, &handles.rect_list_pipeline);
  glDeleteProgramPipelines(1, &handles.quad_list_pipeline);
  glDeleteProgramPipelines(1, &handles.line_quad_list_pipeline);
}

CommandProcessor::CommandProcessor(GL4GraphicsSystem* graphics_system)
    : memory_(graphics_system->memory()),
      graphics_system_(graphics_system),
      register_file_(graphics_system_->register_file()),
      trace_writer_(graphics_system->memory()->physical_membase()),
      trace_state_(TraceState::kDisabled),
      worker_running_(true),
      swap_mode_(SwapMode::kNormal),
      time_base_(0),
      counter_(0),
      primary_buffer_ptr_(0),
      primary_buffer_size_(0),
      read_ptr_index_(0),
      read_ptr_update_freq_(0),
      read_ptr_writeback_ptr_(0),
      write_ptr_index_event_(CreateEvent(NULL, FALSE, FALSE, NULL)),
      write_ptr_index_(0),
      bin_select_(0xFFFFFFFFull),
      bin_mask_(0xFFFFFFFFull),
      has_bindless_vbos_(false),
      active_vertex_shader_(nullptr),
      active_pixel_shader_(nullptr),
      active_framebuffer_(nullptr),
      last_framebuffer_texture_(0),
      last_swap_width_(0),
      last_swap_height_(0),
      point_list_geometry_program_(0),
      rect_list_geometry_program_(0),
      quad_list_geometry_program_(0),
      draw_index_count_(0),
      draw_batcher_(graphics_system_->register_file()),
      scratch_buffer_(kScratchBufferCapacity, kScratchBufferAlignment) {
  LARGE_INTEGER perf_counter;
  QueryPerformanceCounter(&perf_counter);
  time_base_ = perf_counter.QuadPart;
}

CommandProcessor::~CommandProcessor() { CloseHandle(write_ptr_index_event_); }

uint64_t CommandProcessor::QueryTime() {
  LARGE_INTEGER perf_counter;
  QueryPerformanceCounter(&perf_counter);
  return perf_counter.QuadPart - time_base_;
}

bool CommandProcessor::Initialize(std::unique_ptr<GLContext> context) {
  context_ = std::move(context);

  worker_running_ = true;
  worker_thread_ = kernel::object_ref<kernel::XHostThread>(
      new kernel::XHostThread(graphics_system_->emulator()->kernel_state(),
                              128 * 1024, 0, [this]() {
        WorkerThreadMain();
        return 0;
      }));
  worker_thread_->Create();

  return true;
}

void CommandProcessor::Shutdown() {
  EndTracing();

  worker_running_ = false;
  SetEvent(write_ptr_index_event_);
  worker_thread_->Wait(0, 0, 0, nullptr);
  worker_thread_.reset();

  all_pipelines_.clear();
  all_shaders_.clear();
  shader_cache_.clear();

  context_.reset();
}

void CommandProcessor::RequestFrameTrace(const std::wstring& root_path) {
  if (trace_state_ == TraceState::kStreaming) {
    XELOGE("Streaming trace; cannot also trace frame.");
    return;
  }
  if (trace_state_ == TraceState::kSingleFrame) {
    XELOGE("Frame trace already pending; ignoring.");
    return;
  }
  trace_state_ = TraceState::kSingleFrame;
  trace_frame_path_ = root_path;
}

void CommandProcessor::BeginTracing(const std::wstring& root_path) {
  if (trace_state_ == TraceState::kStreaming) {
    XELOGE("Streaming already active; ignoring request.");
    return;
  }
  if (trace_state_ == TraceState::kSingleFrame) {
    XELOGE("Frame trace pending; ignoring streaming request.");
    return;
  }
  std::wstring path = root_path + L"stream";
  trace_state_ = TraceState::kStreaming;
  trace_writer_.Open(path);
}

void CommandProcessor::EndTracing() {
  if (!trace_writer_.is_open()) {
    return;
  }
  assert_true(trace_state_ == TraceState::kStreaming);
  trace_writer_.Close();
}

void CommandProcessor::CallInThread(std::function<void()> fn) {
  if (pending_fns_.empty() &&
      worker_thread_.get() == kernel::XThread::GetCurrentThread()) {
    fn();
  } else {
    pending_fns_.push(std::move(fn));
  }
}

void CommandProcessor::WorkerThreadMain() {
  xe::threading::set_name("GL4 Worker");

  context_->MakeCurrent();
  if (!SetupGL()) {
    XEFATAL("Unable to setup command processor GL state");
    return;
  }

  while (worker_running_) {
    while (!pending_fns_.empty()) {
      auto fn = std::move(pending_fns_.front());
      pending_fns_.pop();
      fn();
    }

    uint32_t write_ptr_index = write_ptr_index_.load();
    if (write_ptr_index == 0xBAADF00D || read_ptr_index_ == write_ptr_index) {
      SCOPE_profile_cpu_i("gpu", "xe::gpu::gl4::CommandProcessor::Stall");
      // We've run out of commands to execute.
      // We spin here waiting for new ones, as the overhead of waiting on our
      // event is too high.
      // PrepareForWait();
      do {
        // TODO(benvanik): if we go longer than Nms, switch to waiting?
        // It'll keep us from burning power.
        // const int wait_time_ms = 5;
        // WaitForSingleObject(write_ptr_index_event_, wait_time_ms);
        SwitchToThread();
        MemoryBarrier();
        write_ptr_index = write_ptr_index_.load();
      } while (pending_fns_.empty() && (write_ptr_index == 0xBAADF00D ||
                                        read_ptr_index_ == write_ptr_index));
      // ReturnFromWait();
      if (!pending_fns_.empty()) {
        continue;
      }
    }
    assert_true(read_ptr_index_ != write_ptr_index);

    // Execute. Note that we handle wraparound transparently.
    ExecutePrimaryBuffer(read_ptr_index_, write_ptr_index);
    read_ptr_index_ = write_ptr_index;

    // TODO(benvanik): use reader->Read_update_freq_ and only issue after moving
    //     that many indices.
    if (read_ptr_writeback_ptr_) {
      xe::store_and_swap<uint32_t>(
          memory_->TranslatePhysical(read_ptr_writeback_ptr_), read_ptr_index_);
    }
  }

  ShutdownGL();
  context_->ClearCurrent();
}

bool CommandProcessor::SetupGL() {
  if (FLAGS_vendor_gl_extensions && GLEW_NV_vertex_buffer_unified_memory) {
    has_bindless_vbos_ = true;
  }

  // Circular buffer holding scratch vertex/index data.
  if (!scratch_buffer_.Initialize()) {
    XELOGE("Unable to initialize scratch buffer");
    return false;
  }

  // Command buffer.
  if (!draw_batcher_.Initialize(&scratch_buffer_)) {
    XELOGE("Unable to initialize command buffer");
    return false;
  }

  // Texture cache that keeps track of any textures/samplers used.
  if (!texture_cache_.Initialize(memory_, &scratch_buffer_)) {
    XELOGE("Unable to initialize texture cache");
    return false;
  }

  const std::string geometry_header =
      "#version 450\n"
      "#extension all : warn\n"
      "#extension GL_ARB_explicit_uniform_location : require\n"
      "#extension GL_ARB_shading_language_420pack : require\n"
      "in gl_PerVertex {\n"
      "  vec4 gl_Position;\n"
      "  float gl_PointSize;\n"
      "  float gl_ClipDistance[];\n"
      "} gl_in[];\n"
      "out gl_PerVertex {\n"
      "  vec4 gl_Position;\n"
      "  float gl_PointSize;\n"
      "  float gl_ClipDistance[];\n"
      "};\n"
      "struct VertexData {\n"
      "  vec4 o[16];\n"
      "};\n"
      "\n"
      "layout(location = 0) in VertexData in_vtx[];\n"
      "layout(location = 0) out VertexData out_vtx;\n";
  // TODO(benvanik): fetch default point size from register and use that if
  //     the VS doesn't write oPointSize.
  // TODO(benvanik): clamp to min/max.
  // TODO(benvanik): figure out how to see which interpolator gets adjusted.
  std::string point_list_shader =
      geometry_header +
      "layout(points) in;\n"
      "layout(triangle_strip, max_vertices = 4) out;\n"
      "void main() {\n"
      "  const vec2 offsets[4] = {\n"
      "    vec2(-1.0,  1.0),\n"
      "    vec2( 1.0,  1.0),\n"
      "    vec2(-1.0, -1.0),\n"
      "    vec2( 1.0, -1.0),\n"
      "  };\n"
      "  vec4 pos = gl_in[0].gl_Position;\n"
      "  float psize = gl_in[0].gl_PointSize;\n"
      "  for (int i = 0; i < 4; ++i) {\n"
      "    gl_Position = vec4(pos.xy + offsets[i] * psize, pos.zw);\n"
      "    out_vtx = in_vtx[0];\n"
      "    EmitVertex();\n"
      "  }\n"
      "  EndPrimitive();\n"
      "}\n";
  std::string rect_list_shader =
      geometry_header +
      "layout(triangles) in;\n"
      "layout(triangle_strip, max_vertices = 4) out;\n"
      "void main() {\n"
      // Most games use the left-aligned form.
      "  bool left_aligned = gl_in[0].gl_Position.x == \n"
      "     gl_in[2].gl_Position.x;\n"
      "  if (left_aligned) {\n"
      //  0 ------ 1
      //  |      - |
      //  |   //   |
      //  | -      |
      //  2 ----- [3]
      "    gl_Position = gl_in[0].gl_Position;\n"
      "    gl_PointSize = gl_in[0].gl_PointSize;\n"
      "    out_vtx = in_vtx[0];\n"
      "    EmitVertex();\n"
      "    gl_Position = gl_in[1].gl_Position;\n"
      "    gl_PointSize = gl_in[1].gl_PointSize;\n"
      "    out_vtx = in_vtx[1];\n"
      "    EmitVertex();\n"
      "    gl_Position = gl_in[2].gl_Position;\n"
      "    gl_PointSize = gl_in[2].gl_PointSize;\n"
      "    out_vtx = in_vtx[2];\n"
      "    EmitVertex();\n"
      "    gl_Position = \n"
      "       (gl_in[1].gl_Position + gl_in[2].gl_Position) - \n"
      "       gl_in[0].gl_Position;\n"
      "    gl_PointSize = gl_in[2].gl_PointSize;\n"
      "    for (int i = 0; i < 16; ++i) {\n"
      "      out_vtx.o[i] = -in_vtx[0].o[i] + in_vtx[1].o[i] + \n"
      "          in_vtx[2].o[i];\n"
      "    }\n"
      "    EmitVertex();\n"
      "  } else {\n"
      //  0 ------ 1
      //  | -      |
      //  |   \\   |
      //  |      - |
      // [3] ----- 2
      "    gl_Position = gl_in[0].gl_Position;\n"
      "    gl_PointSize = gl_in[0].gl_PointSize;\n"
      "    out_vtx = in_vtx[0];\n"
      "    EmitVertex();\n"
      "    gl_Position = gl_in[1].gl_Position;\n"
      "    gl_PointSize = gl_in[1].gl_PointSize;\n"
      "    out_vtx = in_vtx[1];\n"
      "    EmitVertex();\n"
      "    gl_Position = gl_in[0].gl_Position + (gl_in[2].gl_Position - \n"
      "        gl_in[1].gl_Position);\n"
      "    gl_PointSize = gl_in[2].gl_PointSize;\n"
      "    for (int i = 0; i < 16; ++i) {\n"
      "      out_vtx.o[i] = -in_vtx[0].o[i] + in_vtx[1].o[i] + \n"
      "          in_vtx[2].o[i];\n"
      "    }\n"
      "    EmitVertex();\n"
      "    gl_Position = gl_in[2].gl_Position;\n"
      "    gl_PointSize = gl_in[2].gl_PointSize;\n"
      "    out_vtx = in_vtx[2];\n"
      "    EmitVertex();\n"
      "  }\n"
      "  EndPrimitive();\n"
      "}\n";
  std::string quad_list_shader =
      geometry_header +
      "layout(lines_adjacency) in;\n"
      "layout(triangle_strip, max_vertices = 4) out;\n"
      "void main() {\n"
      "  const int order[4] = { 0, 1, 3, 2 };\n"
      "  for (int i = 0; i < 4; ++i) {\n"
      "    int input_index = order[i];\n"
      "    gl_Position = gl_in[input_index].gl_Position;\n"
      "    gl_PointSize = gl_in[input_index].gl_PointSize;\n"
      "    out_vtx = in_vtx[input_index];\n"
      "    EmitVertex();\n"
      "  }\n"
      "  EndPrimitive();\n"
      "}\n";
  std::string line_quad_list_shader =
      geometry_header +
      "layout(lines_adjacency) in;\n"
      "layout(line_strip, max_vertices = 5) out;\n"
      "void main() {\n"
      "  gl_Position = gl_in[0].gl_Position;\n"
      "  gl_PointSize = gl_in[0].gl_PointSize;\n"
      "  out_vtx = in_vtx[0];\n"
      "  EmitVertex();\n"
      "  gl_Position = gl_in[1].gl_Position;\n"
      "  gl_PointSize = gl_in[1].gl_PointSize;\n"
      "  out_vtx = in_vtx[1];\n"
      "  EmitVertex();\n"
      "  gl_Position = gl_in[2].gl_Position;\n"
      "  gl_PointSize = gl_in[2].gl_PointSize;\n"
      "  out_vtx = in_vtx[2];\n"
      "  EmitVertex();\n"
      "  gl_Position = gl_in[3].gl_Position;\n"
      "  gl_PointSize = gl_in[3].gl_PointSize;\n"
      "  out_vtx = in_vtx[3];\n"
      "  EmitVertex();\n"
      "  gl_Position = gl_in[0].gl_Position;\n"
      "  gl_PointSize = gl_in[0].gl_PointSize;\n"
      "  out_vtx = in_vtx[0];\n"
      "  EmitVertex();\n"
      "  EndPrimitive();\n"
      "}\n";
  point_list_geometry_program_ = CreateGeometryProgram(point_list_shader);
  rect_list_geometry_program_ = CreateGeometryProgram(rect_list_shader);
  quad_list_geometry_program_ = CreateGeometryProgram(quad_list_shader);
  line_quad_list_geometry_program_ =
      CreateGeometryProgram(line_quad_list_shader);
  if (!point_list_geometry_program_ || !rect_list_geometry_program_ ||
      !quad_list_geometry_program_ || !line_quad_list_geometry_program_) {
    return false;
  }

  glEnable(GL_SCISSOR_TEST);
  glClipControl(GL_UPPER_LEFT, GL_ZERO_TO_ONE);
  glPointParameteri(GL_POINT_SPRITE_COORD_ORIGIN, GL_UPPER_LEFT);

  return true;
}

GLuint CommandProcessor::CreateGeometryProgram(const std::string& source) {
  auto source_str = source.c_str();
  GLuint program = glCreateShaderProgramv(GL_GEOMETRY_SHADER, 1, &source_str);

  // Get error log, if we failed to link.
  GLint link_status = 0;
  glGetProgramiv(program, GL_LINK_STATUS, &link_status);
  if (!link_status) {
    GLint log_length = 0;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &log_length);
    std::string info_log;
    info_log.resize(log_length - 1);
    glGetProgramInfoLog(program, log_length, &log_length,
                        const_cast<char*>(info_log.data()));
    XELOGE("Unable to link program: %s", info_log.c_str());
    glDeleteProgram(program);
    return 0;
  }

  return program;
}

void CommandProcessor::ShutdownGL() {
  glDeleteProgram(point_list_geometry_program_);
  glDeleteProgram(rect_list_geometry_program_);
  glDeleteProgram(quad_list_geometry_program_);
  glDeleteProgram(line_quad_list_geometry_program_);
  texture_cache_.Shutdown();
  draw_batcher_.Shutdown();
  scratch_buffer_.Shutdown();
}

void CommandProcessor::InitializeRingBuffer(uint32_t ptr, uint32_t page_count) {
  primary_buffer_ptr_ = ptr;
  // Not sure this is correct, but it's a way to take the page_count back to
  // the number of bytes allocated by the physical alloc.
  uint32_t original_size = 1 << (0x1C - page_count - 1);
  primary_buffer_size_ = original_size;
}

void CommandProcessor::EnableReadPointerWriteBack(uint32_t ptr,
                                                  uint32_t block_size) {
  // CP_RB_RPTR_ADDR Ring Buffer Read Pointer Address 0x70C
  // ptr = RB_RPTR_ADDR, pointer to write back the address to.
  read_ptr_writeback_ptr_ = ptr;
  // CP_RB_CNTL Ring Buffer Control 0x704
  // block_size = RB_BLKSZ, number of quadwords read between updates of the
  //              read pointer.
  read_ptr_update_freq_ = (uint32_t)pow(2.0, (double)block_size) / 4;
}

void CommandProcessor::UpdateWritePointer(uint32_t value) {
  write_ptr_index_ = value;
  SetEvent(write_ptr_index_event_);
}

void CommandProcessor::WriteRegister(uint32_t index, uint32_t value) {
  RegisterFile* regs = register_file_;
  assert_true(index < RegisterFile::kRegisterCount);

  regs->values[index].u32 = value;

  // If this is a COHER register, set the dirty flag.
  // This will block the command processor the next time it WAIT_MEM_REGs and
  // allow us to synchronize the memory.
  if (index == XE_GPU_REG_COHER_STATUS_HOST) {
    regs->values[index].u32 |= 0x80000000ul;
  }

  // Scratch register writeback.
  if (index >= XE_GPU_REG_SCRATCH_REG0 && index <= XE_GPU_REG_SCRATCH_REG7) {
    uint32_t scratch_reg = index - XE_GPU_REG_SCRATCH_REG0;
    if ((1 << scratch_reg) & regs->values[XE_GPU_REG_SCRATCH_UMSK].u32) {
      // Enabled - write to address.
      uint32_t scratch_addr = regs->values[XE_GPU_REG_SCRATCH_ADDR].u32;
      uint32_t mem_addr = scratch_addr + (scratch_reg * 4);
      xe::store_and_swap<uint32_t>(memory_->TranslatePhysical(mem_addr), value);
    }
  }
}

void CommandProcessor::MakeCoherent() {
  SCOPE_profile_cpu_f("gpu");

  // Status host often has 0x01000000 or 0x03000000.
  // This is likely toggling VC (vertex cache) or TC (texture cache).
  // Or, it also has a direction in here maybe - there is probably
  // some way to check for dest coherency (what all the COHER_DEST_BASE_*
  // registers are for).
  // Best docs I've found on this are here:
  // http://amd-dev.wpengine.netdna-cdn.com/wordpress/media/2013/10/R6xx_R7xx_3D.pdf
  // http://cgit.freedesktop.org/xorg/driver/xf86-video-radeonhd/tree/src/r6xx_accel.c?id=3f8b6eccd9dba116cc4801e7f80ce21a879c67d2#n454

  RegisterFile* regs = register_file_;
  auto status_host = regs->values[XE_GPU_REG_COHER_STATUS_HOST].u32;
  auto base_host = regs->values[XE_GPU_REG_COHER_BASE_HOST].u32;
  auto size_host = regs->values[XE_GPU_REG_COHER_SIZE_HOST].u32;

  if (!(status_host & 0x80000000ul)) {
    return;
  }

  // TODO(benvanik): notify resource cache of base->size and type.
  // XELOGD("Make %.8X -> %.8X (%db) coherent", base_host, base_host +
  // size_host, size_host);

  // Mark coherent.
  status_host &= ~0x80000000ul;
  regs->values[XE_GPU_REG_COHER_STATUS_HOST].u32 = status_host;

  scratch_buffer_.ClearCache();
}

void CommandProcessor::PrepareForWait() {
  SCOPE_profile_cpu_f("gpu");

  trace_writer_.Flush();

  // TODO(benvanik): fences and fancy stuff. We should figure out a way to
  // make interrupt callbacks from the GPU so that we don't have to do a full
  // synchronize here.
  glFlush();
  // glFinish();

  if (FLAGS_thread_safe_gl) {
    context_->ClearCurrent();
  }
}

void CommandProcessor::ReturnFromWait() {
  if (FLAGS_thread_safe_gl) {
    context_->MakeCurrent();
  }
}

void CommandProcessor::IssueSwap() {
  IssueSwap(last_swap_width_, last_swap_height_);
}

void CommandProcessor::IssueSwap(uint32_t frontbuffer_width,
                                 uint32_t frontbuffer_height) {
  if (!swap_handler_) {
    return;
  }

  auto& regs = *register_file_;
  SwapParameters swap_params;

  // Lookup the framebuffer in the recently-resolved list.
  // TODO(benvanik): make this much more sophisticated.
  // TODO(benvanik): handle not found cases.
  // TODO(benvanik): handle dirty cases (resolved to sysmem, touched).
  // !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
  // HACK: just use whatever our current framebuffer is.
  swap_params.framebuffer_texture = last_framebuffer_texture_;
  /*swap_params.framebuffer_texture = active_framebuffer_
                                        ? active_framebuffer_->color_targets[0]
                                        : last_framebuffer_texture_;*/

  // Frontbuffer dimensions, if valid.
  swap_params.x = 0;
  swap_params.y = 0;
  swap_params.width = frontbuffer_width ? frontbuffer_width : 1280;
  swap_params.height = frontbuffer_height ? frontbuffer_height : 720;

  PrepareForWait();
  swap_handler_(swap_params);
  ReturnFromWait();

  // Remove any dead textures, etc.
  texture_cache_.Scavenge();
}

class CommandProcessor::RingbufferReader {
 public:
  RingbufferReader(uint8_t* membase, uint32_t base_ptr, uint32_t ptr_mask,
                   uint32_t start_ptr, uint32_t end_ptr)
      : membase_(membase),
        base_ptr_(base_ptr),
        ptr_mask_(ptr_mask),
        start_ptr_(start_ptr),
        end_ptr_(end_ptr),
        ptr_(start_ptr),
        offset_(0) {}

  uint32_t ptr() const { return ptr_; }
  uint32_t offset() const { return offset_; }
  bool can_read() const { return ptr_ != end_ptr_; }

  uint32_t Peek() { return xe::load_and_swap<uint32_t>(membase_ + ptr_); }

  void CheckRead(uint32_t words) {
    assert_true(ptr_ + words * sizeof(uint32_t) <= end_ptr_);
  }

  uint32_t Read() {
    uint32_t value = xe::load_and_swap<uint32_t>(membase_ + ptr_);
    Advance(1);
    return value;
  }

  void Advance(uint32_t words) {
    offset_ += words;
    ptr_ = ptr_ + words * sizeof(uint32_t);
    if (ptr_mask_) {
      ptr_ = base_ptr_ +
             (((ptr_ - base_ptr_) / sizeof(uint32_t)) & ptr_mask_) *
                 sizeof(uint32_t);
    }
  }

  void Skip(uint32_t words) { Advance(words); }

 private:
  uint8_t* membase_;

  uint32_t base_ptr_;
  uint32_t ptr_mask_;
  uint32_t start_ptr_;
  uint32_t end_ptr_;
  uint32_t ptr_;
  uint32_t offset_;
};

void CommandProcessor::ExecutePrimaryBuffer(uint32_t start_index,
                                            uint32_t end_index) {
  SCOPE_profile_cpu_f("gpu");

  // Adjust pointer base.
  uint32_t start_ptr = primary_buffer_ptr_ + start_index * sizeof(uint32_t);
  start_ptr = (primary_buffer_ptr_ & ~0x1FFFFFFF) | (start_ptr & 0x1FFFFFFF);
  uint32_t end_ptr = primary_buffer_ptr_ + end_index * sizeof(uint32_t);
  end_ptr = (primary_buffer_ptr_ & ~0x1FFFFFFF) | (end_ptr & 0x1FFFFFFF);

  trace_writer_.WritePrimaryBufferStart(start_ptr, end_index - start_index);

  // Execute commands!
  uint32_t ptr_mask = (primary_buffer_size_ / sizeof(uint32_t)) - 1;
  RingbufferReader reader(memory_->physical_membase(), primary_buffer_ptr_,
                          ptr_mask, start_ptr, end_ptr);
  while (reader.can_read()) {
    ExecutePacket(&reader);
  }
  if (end_index > start_index) {
    assert_true(reader.offset() == (end_index - start_index));
  }

  trace_writer_.WritePrimaryBufferEnd();
}

void CommandProcessor::ExecuteIndirectBuffer(uint32_t ptr, uint32_t length) {
  SCOPE_profile_cpu_f("gpu");

  trace_writer_.WriteIndirectBufferStart(ptr, length / sizeof(uint32_t));

  // Execute commands!
  uint32_t ptr_mask = 0;
  RingbufferReader reader(memory_->physical_membase(), primary_buffer_ptr_,
                          ptr_mask, ptr, ptr + length * sizeof(uint32_t));
  while (reader.can_read()) {
    ExecutePacket(&reader);
  }

  trace_writer_.WriteIndirectBufferEnd();
}

void CommandProcessor::ExecutePacket(uint32_t ptr, uint32_t count) {
  uint32_t ptr_mask = 0;
  RingbufferReader reader(memory_->physical_membase(), primary_buffer_ptr_,
                          ptr_mask, ptr, ptr + count * sizeof(uint32_t));
  while (reader.can_read()) {
    ExecutePacket(&reader);
  }
}

bool CommandProcessor::ExecutePacket(RingbufferReader* reader) {
  RegisterFile* regs = register_file_;

  const uint32_t packet = reader->Read();
  const uint32_t packet_type = packet >> 30;
  if (packet == 0) {
    trace_writer_.WritePacketStart(reader->ptr() - 4, 1);
    trace_writer_.WritePacketEnd();
    return true;
  }

  switch (packet_type) {
    case 0x00:
      return ExecutePacketType0(reader, packet);
    case 0x01:
      return ExecutePacketType1(reader, packet);
    case 0x02:
      return ExecutePacketType2(reader, packet);
    case 0x03:
      return ExecutePacketType3(reader, packet);
    default:
      assert_unhandled_case(packet_type);
      return false;
  }
}

bool CommandProcessor::ExecutePacketType0(RingbufferReader* reader,
                                          uint32_t packet) {
  // Type-0 packet.
  // Write count registers in sequence to the registers starting at
  // (base_index << 2).

  uint32_t count = ((packet >> 16) & 0x3FFF) + 1;
  trace_writer_.WritePacketStart(reader->ptr() - 4, 1 + count);

  uint32_t base_index = (packet & 0x7FFF);
  uint32_t write_one_reg = (packet >> 15) & 0x1;
  for (uint32_t m = 0; m < count; m++) {
    uint32_t reg_data = reader->Read();
    uint32_t target_index = write_one_reg ? base_index : base_index + m;
    WriteRegister(target_index, reg_data);
  }

  trace_writer_.WritePacketEnd();
  return true;
}

bool CommandProcessor::ExecutePacketType1(RingbufferReader* reader,
                                          uint32_t packet) {
  // Type-1 packet.
  // Contains two registers of data. Type-0 should be more common.
  trace_writer_.WritePacketStart(reader->ptr() - 4, 3);
  uint32_t reg_index_1 = packet & 0x7FF;
  uint32_t reg_index_2 = (packet >> 11) & 0x7FF;
  uint32_t reg_data_1 = reader->Read();
  uint32_t reg_data_2 = reader->Read();
  WriteRegister(reg_index_1, reg_data_1);
  WriteRegister(reg_index_2, reg_data_2);
  trace_writer_.WritePacketEnd();
  return true;
}

bool CommandProcessor::ExecutePacketType2(RingbufferReader* reader,
                                          uint32_t packet) {
  // Type-2 packet.
  // No-op. Do nothing.
  trace_writer_.WritePacketStart(reader->ptr() - 4, 1);
  trace_writer_.WritePacketEnd();
  return true;
}

bool CommandProcessor::ExecutePacketType3(RingbufferReader* reader,
                                          uint32_t packet) {
  // Type-3 packet.
  uint32_t opcode = (packet >> 8) & 0x7F;
  uint32_t count = ((packet >> 16) & 0x3FFF) + 1;
  auto data_start_offset = reader->offset();

  // To handle nesting behavior when tracing we special case indirect buffers.
  if (opcode == PM4_INDIRECT_BUFFER) {
    trace_writer_.WritePacketStart(reader->ptr() - 4, 2);
  } else {
    trace_writer_.WritePacketStart(reader->ptr() - 4, 1 + count);
  }

  // & 1 == predicate - when set, we do bin check to see if we should execute
  // the packet. Only type 3 packets are affected.
  // We also skip predicated swaps, as they are never valid (probably?).
  if (packet & 1) {
    bool any_pass = (bin_select_ & bin_mask_) != 0;
    if (!any_pass || opcode == PM4_XE_SWAP) {
      reader->Skip(count);
      trace_writer_.WritePacketEnd();
      return true;
    }
  }

  bool result = false;
  switch (opcode) {
    case PM4_ME_INIT:
      result = ExecutePacketType3_ME_INIT(reader, packet, count);
      break;
    case PM4_NOP:
      result = ExecutePacketType3_NOP(reader, packet, count);
      break;
    case PM4_INTERRUPT:
      result = ExecutePacketType3_INTERRUPT(reader, packet, count);
      break;
    case PM4_XE_SWAP:
      result = ExecutePacketType3_XE_SWAP(reader, packet, count);
      break;
    case PM4_INDIRECT_BUFFER:
      result = ExecutePacketType3_INDIRECT_BUFFER(reader, packet, count);
      break;
    case PM4_WAIT_REG_MEM:
      result = ExecutePacketType3_WAIT_REG_MEM(reader, packet, count);
      break;
    case PM4_REG_RMW:
      result = ExecutePacketType3_REG_RMW(reader, packet, count);
      break;
    case PM4_COND_WRITE:
      result = ExecutePacketType3_COND_WRITE(reader, packet, count);
      break;
    case PM4_EVENT_WRITE:
      result = ExecutePacketType3_EVENT_WRITE(reader, packet, count);
      break;
    case PM4_EVENT_WRITE_SHD:
      result = ExecutePacketType3_EVENT_WRITE_SHD(reader, packet, count);
      break;
    case PM4_EVENT_WRITE_EXT:
      result = ExecutePacketType3_EVENT_WRITE_EXT(reader, packet, count);
      break;
    case PM4_DRAW_INDX:
      result = ExecutePacketType3_DRAW_INDX(reader, packet, count);
      break;
    case PM4_DRAW_INDX_2:
      result = ExecutePacketType3_DRAW_INDX_2(reader, packet, count);
      break;
    case PM4_SET_CONSTANT:
      result = ExecutePacketType3_SET_CONSTANT(reader, packet, count);
      break;
    case PM4_SET_CONSTANT2:
      result = ExecutePacketType3_SET_CONSTANT2(reader, packet, count);
      break;
    case PM4_LOAD_ALU_CONSTANT:
      result = ExecutePacketType3_LOAD_ALU_CONSTANT(reader, packet, count);
      break;
    case PM4_SET_SHADER_CONSTANTS:
      result = ExecutePacketType3_SET_SHADER_CONSTANTS(reader, packet, count);
      break;
    case PM4_IM_LOAD:
      result = ExecutePacketType3_IM_LOAD(reader, packet, count);
      break;
    case PM4_IM_LOAD_IMMEDIATE:
      result = ExecutePacketType3_IM_LOAD_IMMEDIATE(reader, packet, count);
      break;
    case PM4_INVALIDATE_STATE:
      result = ExecutePacketType3_INVALIDATE_STATE(reader, packet, count);
      break;

    case PM4_SET_BIN_MASK_LO: {
      uint32_t value = reader->Read();
      bin_mask_ = (bin_mask_ & 0xFFFFFFFF00000000ull) | value;
      result = true;
    } break;
    case PM4_SET_BIN_MASK_HI: {
      uint32_t value = reader->Read();
      bin_mask_ =
          (bin_mask_ & 0xFFFFFFFFull) | (static_cast<uint64_t>(value) << 32);
      result = true;
    } break;
    case PM4_SET_BIN_SELECT_LO: {
      uint32_t value = reader->Read();
      bin_select_ = (bin_select_ & 0xFFFFFFFF00000000ull) | value;
      result = true;
    } break;
    case PM4_SET_BIN_SELECT_HI: {
      uint32_t value = reader->Read();
      bin_select_ =
          (bin_select_ & 0xFFFFFFFFull) | (static_cast<uint64_t>(value) << 32);
      result = true;
    } break;

    // Ignored packets - useful if breaking on the default handler below.
    case 0x50:  // 0xC0015000 usually 2 words, 0xFFFFFFFF / 0x00000000
    case 0x51:  // 0xC0015100 usually 2 words, 0xFFFFFFFF / 0xFFFFFFFF
      reader->Skip(count);
      break;

    default:
      reader->Skip(count);
      break;
  }

  trace_writer_.WritePacketEnd();
  assert_true(reader->offset() == data_start_offset + count);
  return result;
}

bool CommandProcessor::ExecutePacketType3_ME_INIT(RingbufferReader* reader,

                                                  uint32_t packet,
                                                  uint32_t count) {
  // initialize CP's micro-engine
  reader->Advance(count);
  return true;
}

bool CommandProcessor::ExecutePacketType3_NOP(RingbufferReader* reader,

                                              uint32_t packet, uint32_t count) {
  // skip N 32-bit words to get to the next packet
  // No-op, ignore some data.
  reader->Advance(count);
  return true;
}

bool CommandProcessor::ExecutePacketType3_INTERRUPT(RingbufferReader* reader,

                                                    uint32_t packet,
                                                    uint32_t count) {
  SCOPE_profile_cpu_f("gpu");

  // generate interrupt from the command stream
  uint32_t cpu_mask = reader->Read();
  for (int n = 0; n < 6; n++) {
    if (cpu_mask & (1 << n)) {
      graphics_system_->DispatchInterruptCallback(1, n);
    }
  }
  return true;
}

bool CommandProcessor::ExecutePacketType3_XE_SWAP(RingbufferReader* reader,

                                                  uint32_t packet,
                                                  uint32_t count) {
  SCOPE_profile_cpu_f("gpu");

  XELOGI("XE_SWAP");

  // Xenia-specific VdSwap hook.
  // VdSwap will post this to tell us we need to swap the screen/fire an
  // interrupt.
  // 63 words here, but only the first has any data.
  uint32_t frontbuffer_ptr = reader->Read();
  uint32_t frontbuffer_width = reader->Read();
  uint32_t frontbuffer_height = reader->Read();
  reader->Advance(count - 3);
  last_swap_width_ = frontbuffer_width;
  last_swap_height_ = frontbuffer_height;

  // Ensure we issue any pending draws.
  draw_batcher_.Flush(DrawBatcher::FlushMode::kMakeCoherent);

  if (swap_mode_ == SwapMode::kNormal) {
    IssueSwap(frontbuffer_width, frontbuffer_height);
  }

  if (trace_writer_.is_open()) {
    trace_writer_.WriteEvent(EventType::kSwap);
    trace_writer_.Flush();
    if (trace_state_ == TraceState::kSingleFrame) {
      trace_state_ = TraceState::kDisabled;
      trace_writer_.Close();
    }
  } else if (trace_state_ == TraceState::kSingleFrame) {
    // New trace request - we only start tracing at the beginning of a frame.
    auto frame_number = L"frame_" + std::to_wstring(counter_);
    auto path = trace_frame_path_ + frame_number;
    trace_writer_.Open(path);
  }
  ++counter_;
  return true;
}

bool CommandProcessor::ExecutePacketType3_INDIRECT_BUFFER(
    RingbufferReader* reader, uint32_t packet, uint32_t count) {
  // indirect buffer dispatch
  uint32_t list_ptr = CpuToGpu(reader->Read());
  uint32_t list_length = reader->Read();
  ExecuteIndirectBuffer(GpuToCpu(list_ptr), list_length);
  return true;
}

bool CommandProcessor::ExecutePacketType3_WAIT_REG_MEM(RingbufferReader* reader,

                                                       uint32_t packet,
                                                       uint32_t count) {
  SCOPE_profile_cpu_f("gpu");

  // wait until a register or memory location is a specific value
  uint32_t wait_info = reader->Read();
  uint32_t poll_reg_addr = reader->Read();
  uint32_t ref = reader->Read();
  uint32_t mask = reader->Read();
  uint32_t wait = reader->Read();
  bool matched = false;
  do {
    uint32_t value;
    if (wait_info & 0x10) {
      // Memory.
      auto endianness = static_cast<Endian>(poll_reg_addr & 0x3);
      poll_reg_addr &= ~0x3;
      value = xe::load<uint32_t>(memory_->TranslatePhysical(poll_reg_addr));
      value = GpuSwap(value, endianness);
      trace_writer_.WriteMemoryRead(CpuToGpu(poll_reg_addr), 4);
    } else {
      // Register.
      assert_true(poll_reg_addr < RegisterFile::kRegisterCount);
      value = register_file_->values[poll_reg_addr].u32;
      if (poll_reg_addr == XE_GPU_REG_COHER_STATUS_HOST) {
        MakeCoherent();
        value = register_file_->values[poll_reg_addr].u32;
      }
    }
    switch (wait_info & 0x7) {
      case 0x0:  // Never.
        matched = false;
        break;
      case 0x1:  // Less than reference.
        matched = (value & mask) < ref;
        break;
      case 0x2:  // Less than or equal to reference.
        matched = (value & mask) <= ref;
        break;
      case 0x3:  // Equal to reference.
        matched = (value & mask) == ref;
        break;
      case 0x4:  // Not equal to reference.
        matched = (value & mask) != ref;
        break;
      case 0x5:  // Greater than or equal to reference.
        matched = (value & mask) >= ref;
        break;
      case 0x6:  // Greater than reference.
        matched = (value & mask) > ref;
        break;
      case 0x7:  // Always
        matched = true;
        break;
    }
    if (!matched) {
      // Wait.
      if (wait >= 0x100) {
        PrepareForWait();
        if (!FLAGS_vsync) {
          // User wants it fast and dangerous.
          SwitchToThread();
        } else {
          Sleep(wait / 0x100);
        }
        MemoryBarrier();
        ReturnFromWait();
      } else {
        SwitchToThread();
      }
    }
  } while (!matched);
  return true;
}

bool CommandProcessor::ExecutePacketType3_REG_RMW(RingbufferReader* reader,

                                                  uint32_t packet,
                                                  uint32_t count) {
  // register read/modify/write
  // ? (used during shader upload and edram setup)
  uint32_t rmw_info = reader->Read();
  uint32_t and_mask = reader->Read();
  uint32_t or_mask = reader->Read();
  uint32_t value = register_file_->values[rmw_info & 0x1FFF].u32;
  if ((rmw_info >> 30) & 0x1) {
    // | reg
    value |= register_file_->values[or_mask & 0x1FFF].u32;
  } else {
    // | imm
    value |= or_mask;
  }
  if ((rmw_info >> 31) & 0x1) {
    // & reg
    value &= register_file_->values[and_mask & 0x1FFF].u32;
  } else {
    // & imm
    value &= and_mask;
  }
  WriteRegister(rmw_info & 0x1FFF, value);
  return true;
}

bool CommandProcessor::ExecutePacketType3_COND_WRITE(RingbufferReader* reader,

                                                     uint32_t packet,
                                                     uint32_t count) {
  // conditional write to memory or register
  uint32_t wait_info = reader->Read();
  uint32_t poll_reg_addr = reader->Read();
  uint32_t ref = reader->Read();
  uint32_t mask = reader->Read();
  uint32_t write_reg_addr = reader->Read();
  uint32_t write_data = reader->Read();
  uint32_t value;
  if (wait_info & 0x10) {
    // Memory.
    auto endianness = static_cast<Endian>(poll_reg_addr & 0x3);
    poll_reg_addr &= ~0x3;
    trace_writer_.WriteMemoryRead(CpuToGpu(poll_reg_addr), 4);
    value = xe::load<uint32_t>(memory_->TranslatePhysical(poll_reg_addr));
    value = GpuSwap(value, endianness);
  } else {
    // Register.
    assert_true(poll_reg_addr < RegisterFile::kRegisterCount);
    value = register_file_->values[poll_reg_addr].u32;
  }
  bool matched = false;
  switch (wait_info & 0x7) {
    case 0x0:  // Never.
      matched = false;
      break;
    case 0x1:  // Less than reference.
      matched = (value & mask) < ref;
      break;
    case 0x2:  // Less than or equal to reference.
      matched = (value & mask) <= ref;
      break;
    case 0x3:  // Equal to reference.
      matched = (value & mask) == ref;
      break;
    case 0x4:  // Not equal to reference.
      matched = (value & mask) != ref;
      break;
    case 0x5:  // Greater than or equal to reference.
      matched = (value & mask) >= ref;
      break;
    case 0x6:  // Greater than reference.
      matched = (value & mask) > ref;
      break;
    case 0x7:  // Always
      matched = true;
      break;
  }
  if (matched) {
    // Write.
    if (wait_info & 0x100) {
      // Memory.
      auto endianness = static_cast<Endian>(write_reg_addr & 0x3);
      write_reg_addr &= ~0x3;
      write_data = GpuSwap(write_data, endianness);
      xe::store(memory_->TranslatePhysical(write_reg_addr), write_data);
      trace_writer_.WriteMemoryWrite(CpuToGpu(write_reg_addr), 4);
    } else {
      // Register.
      WriteRegister(write_reg_addr, write_data);
    }
  }
  return true;
}

bool CommandProcessor::ExecutePacketType3_EVENT_WRITE(RingbufferReader* reader,
                                                      uint32_t packet,
                                                      uint32_t count) {
  // generate an event that creates a write to memory when completed
  uint32_t initiator = reader->Read();
  // Writeback initiator.
  WriteRegister(XE_GPU_REG_VGT_EVENT_INITIATOR, initiator & 0x3F);
  if (count == 1) {
    // Just an event flag? Where does this write?
  } else {
    // Write to an address.
    assert_always();
    reader->Advance(count - 1);
  }
  return true;
}

bool CommandProcessor::ExecutePacketType3_EVENT_WRITE_SHD(
    RingbufferReader* reader, uint32_t packet, uint32_t count) {
  // generate a VS|PS_done event
  uint32_t initiator = reader->Read();
  uint32_t address = reader->Read();
  uint32_t value = reader->Read();
  // Writeback initiator.
  WriteRegister(XE_GPU_REG_VGT_EVENT_INITIATOR, initiator & 0x3F);
  uint32_t data_value;
  if ((initiator >> 31) & 0x1) {
    // Write counter (GPU vblank counter?).
    data_value = counter_;
  } else {
    // Write value.
    data_value = value;
  }
  auto endianness = static_cast<Endian>(address & 0x3);
  address &= ~0x3;
  data_value = GpuSwap(data_value, endianness);
  xe::store(memory_->TranslatePhysical(address), data_value);
  trace_writer_.WriteMemoryWrite(CpuToGpu(address), 4);
  return true;
}

bool CommandProcessor::ExecutePacketType3_EVENT_WRITE_EXT(
    RingbufferReader* reader, uint32_t packet, uint32_t count) {
  // generate a screen extent event
  uint32_t initiator = reader->Read();
  uint32_t address = reader->Read();
  // Writeback initiator.
  WriteRegister(XE_GPU_REG_VGT_EVENT_INITIATOR, initiator & 0x3F);
  auto endianness = static_cast<Endian>(address & 0x3);
  address &= ~0x3;
  // Let us hope we can fake this.
  uint16_t extents[] = {
      0 >> 3,     // min x
      2560 >> 3,  // max x
      0 >> 3,     // min y
      2560 >> 3,  // max y
      0,          // min z
      1,          // max z
  };
  assert_true(endianness == xenos::Endian::k8in16);
  xe::copy_and_swap_16_aligned(
      reinterpret_cast<uint16_t*>(memory_->TranslatePhysical(address)), extents,
      xe::countof(extents));
  trace_writer_.WriteMemoryWrite(CpuToGpu(address), sizeof(extents));
  return true;
}

bool CommandProcessor::ExecutePacketType3_DRAW_INDX(RingbufferReader* reader,
                                                    uint32_t packet,
                                                    uint32_t count) {
  // initiate fetch of index buffer and draw
  // dword0 = viz query info
  uint32_t dword0 = reader->Read();
  uint32_t dword1 = reader->Read();
  uint32_t index_count = dword1 >> 16;
  auto prim_type = static_cast<PrimitiveType>(dword1 & 0x3F);
  uint32_t src_sel = (dword1 >> 6) & 0x3;
  if (src_sel == 0x0) {
    // Indexed draw.
    index_buffer_info_.guest_base = reader->Read();
    uint32_t index_size = reader->Read();
    index_buffer_info_.endianness = static_cast<Endian>(index_size >> 30);
    index_size &= 0x00FFFFFF;
    bool index_32bit = (dword1 >> 11) & 0x1;
    index_buffer_info_.format =
        index_32bit ? IndexFormat::kInt32 : IndexFormat::kInt16;
    index_size *= index_32bit ? 4 : 2;
    index_buffer_info_.length = index_size;
    index_buffer_info_.count = index_count;
  } else if (src_sel == 0x2) {
    // Auto draw.
    index_buffer_info_.guest_base = 0;
    index_buffer_info_.length = 0;
  } else {
    // Unknown source select.
    assert_always();
  }
  draw_index_count_ = index_count;

  bool draw_valid = false;
  if (src_sel == 0x0) {
    // Indexed draw.
    draw_valid = draw_batcher_.BeginDrawElements(prim_type, index_count,
                                                 index_buffer_info_.format);
  } else if (src_sel == 0x2) {
    // Auto draw.
    draw_valid = draw_batcher_.BeginDrawArrays(prim_type, index_count);
  } else {
    // Unknown source select.
    assert_always();
  }
  if (!draw_valid) {
    return false;
  }
  return IssueDraw();
}

bool CommandProcessor::ExecutePacketType3_DRAW_INDX_2(RingbufferReader* reader,

                                                      uint32_t packet,
                                                      uint32_t count) {
  // draw using supplied indices in packet
  uint32_t dword0 = reader->Read();
  uint32_t index_count = dword0 >> 16;
  auto prim_type = static_cast<PrimitiveType>(dword0 & 0x3F);
  uint32_t src_sel = (dword0 >> 6) & 0x3;
  assert_true(src_sel == 0x2);  // 'SrcSel=AutoIndex'
  bool index_32bit = (dword0 >> 11) & 0x1;
  uint32_t indices_size = index_count * (index_32bit ? 4 : 2);
  uint32_t index_ptr = reader->ptr();
  index_buffer_info_.guest_base = 0;
  index_buffer_info_.length = 0;
  reader->Advance(count - 1);
  draw_index_count_ = index_count;
  bool draw_valid = draw_batcher_.BeginDrawArrays(prim_type, index_count);
  if (!draw_valid) {
    return false;
  }
  return IssueDraw();
}

bool CommandProcessor::ExecutePacketType3_SET_CONSTANT(RingbufferReader* reader,
                                                       uint32_t packet,
                                                       uint32_t count) {
  // load constant into chip and to memory
  // PM4_REG(reg) ((0x4 << 16) | (GSL_HAL_SUBBLOCK_OFFSET(reg)))
  //                                     reg - 0x2000
  uint32_t offset_type = reader->Read();
  uint32_t index = offset_type & 0x7FF;
  uint32_t type = (offset_type >> 16) & 0xFF;
  switch (type) {
    case 0:  // ALU
      index += 0x4000;
      break;
    case 1:  // FETCH
      index += 0x4800;
      break;
    case 2:  // BOOL
      index += 0x4900;
      break;
    case 3:  // LOOP
      index += 0x4908;
      break;
    case 4:  // REGISTERS
      index += 0x2000;
      break;
    default:
      assert_always();
      reader->Skip(count - 1);
      return true;
  }
  for (uint32_t n = 0; n < count - 1; n++, index++) {
    uint32_t data = reader->Read();
    WriteRegister(index, data);
  }
  return true;
}

bool CommandProcessor::ExecutePacketType3_SET_CONSTANT2(
    RingbufferReader* reader, uint32_t packet, uint32_t count) {
  uint32_t offset_type = reader->Read();
  uint32_t index = offset_type & 0xFFFF;
  for (uint32_t n = 0; n < count - 1; n++, index++) {
    uint32_t data = reader->Read();
    WriteRegister(index, data);
  }
  return true;
}

bool CommandProcessor::ExecutePacketType3_LOAD_ALU_CONSTANT(
    RingbufferReader* reader, uint32_t packet, uint32_t count) {
  // load constants from memory
  uint32_t address = reader->Read();
  address &= 0x3FFFFFFF;
  uint32_t offset_type = reader->Read();
  uint32_t index = offset_type & 0x7FF;
  uint32_t size_dwords = reader->Read();
  size_dwords &= 0xFFF;
  uint32_t type = (offset_type >> 16) & 0xFF;
  switch (type) {
    case 0:  // ALU
      index += 0x4000;
      break;
    case 1:  // FETCH
      index += 0x4800;
      break;
    case 2:  // BOOL
      index += 0x4900;
      break;
    case 3:  // LOOP
      index += 0x4908;
      break;
    case 4:  // REGISTERS
      index += 0x2000;
      break;
    default:
      assert_always();
      return true;
  }
  trace_writer_.WriteMemoryRead(CpuToGpu(address), size_dwords * 4);
  for (uint32_t n = 0; n < size_dwords; n++, index++) {
    uint32_t data = xe::load_and_swap<uint32_t>(
        memory_->TranslatePhysical(address + n * 4));
    WriteRegister(index, data);
  }
  return true;
}

bool CommandProcessor::ExecutePacketType3_SET_SHADER_CONSTANTS(
    RingbufferReader* reader, uint32_t packet, uint32_t count) {
  uint32_t offset_type = reader->Read();
  uint32_t index = offset_type & 0xFFFF;
  for (uint32_t n = 0; n < count - 1; n++, index++) {
    uint32_t data = reader->Read();
    WriteRegister(index, data);
  }
  return true;
}

bool CommandProcessor::ExecutePacketType3_IM_LOAD(RingbufferReader* reader,
                                                  uint32_t packet,
                                                  uint32_t count) {
  // load sequencer instruction memory (pointer-based)
  uint32_t addr_type = reader->Read();
  auto shader_type = static_cast<ShaderType>(addr_type & 0x3);
  uint32_t addr = addr_type & ~0x3;
  uint32_t start_size = reader->Read();
  uint32_t start = start_size >> 16;
  uint32_t size_dwords = start_size & 0xFFFF;  // dwords
  assert_true(start == 0);
  trace_writer_.WriteMemoryRead(CpuToGpu(addr), size_dwords * 4);
  LoadShader(shader_type, addr, memory_->TranslatePhysical<uint32_t*>(addr),
             size_dwords);
  return true;
}

bool CommandProcessor::ExecutePacketType3_IM_LOAD_IMMEDIATE(
    RingbufferReader* reader, uint32_t packet, uint32_t count) {
  // load sequencer instruction memory (code embedded in packet)
  uint32_t dword0 = reader->Read();
  uint32_t dword1 = reader->Read();
  auto shader_type = static_cast<ShaderType>(dword0);
  uint32_t start_size = dword1;
  uint32_t start = start_size >> 16;
  uint32_t size_dwords = start_size & 0xFFFF;  // dwords
  assert_true(start == 0);
  reader->CheckRead(size_dwords);
  LoadShader(shader_type, reader->ptr(),
             memory_->TranslatePhysical<uint32_t*>(reader->ptr()), size_dwords);
  reader->Advance(size_dwords);
  return true;
}

bool CommandProcessor::ExecutePacketType3_INVALIDATE_STATE(
    RingbufferReader* reader, uint32_t packet, uint32_t count) {
  // selective invalidation of state pointers
  uint32_t mask = reader->Read();
  // driver_->InvalidateState(mask);
  return true;
}

bool CommandProcessor::LoadShader(ShaderType shader_type,
                                  uint32_t guest_address,
                                  const uint32_t* host_address,
                                  uint32_t dword_count) {
  // Hash the input memory and lookup the shader.
  GL4Shader* shader_ptr = nullptr;
  uint64_t hash = XXH64(host_address, dword_count * sizeof(uint32_t), 0);
  auto it = shader_cache_.find(hash);
  if (it != shader_cache_.end()) {
    // Found in the cache.
    // TODO(benvanik): compare bytes? Likelyhood of collision is low.
    shader_ptr = it->second;
  } else {
    // Not found in cache.
    // No translation is performed here, as it depends on program_cntl.
    auto shader = std::make_unique<GL4Shader>(shader_type, hash, host_address,
                                              dword_count);
    shader_ptr = shader.get();
    shader_cache_.insert({hash, shader_ptr});
    all_shaders_.emplace_back(std::move(shader));

    XELOGGPU("Set %s shader at %0.8X (%db):\n%s",
             shader_type == ShaderType::kVertex ? "vertex" : "pixel",
             guest_address, dword_count * 4,
             shader_ptr->ucode_disassembly().c_str());
  }
  switch (shader_type) {
    case ShaderType::kVertex:
      active_vertex_shader_ = shader_ptr;
      break;
    case ShaderType::kPixel:
      active_pixel_shader_ = shader_ptr;
      break;
    default:
      assert_unhandled_case(shader_type);
      return false;
  }
  return true;
}

bool CommandProcessor::IssueDraw() {
#if FINE_GRAINED_DRAW_SCOPES
  SCOPE_profile_cpu_f("gpu");
#endif  // FINE_GRAINED_DRAW_SCOPES

  auto& regs = *register_file_;

  auto enable_mode =
      static_cast<ModeControl>(regs[XE_GPU_REG_RB_MODECONTROL].u32 & 0x7);
  if (enable_mode == ModeControl::kIgnore) {
    // Ignored.
    draw_batcher_.DiscardDraw();
    return true;
  } else if (enable_mode == ModeControl::kCopy) {
    // Special copy handling.
    draw_batcher_.DiscardDraw();
    return IssueCopy();
  }

#define CHECK_ISSUE_UPDATE_STATUS(status, mismatch, error_message) \
  {                                                                \
    if (status == UpdateStatus::kError) {                          \
      XELOGE(error_message);                                       \
      draw_batcher_.DiscardDraw();                                 \
      return false;                                                \
    } else if (status == UpdateStatus::kMismatch) {                \
      mismatch = true;                                             \
    }                                                              \
  }

  UpdateStatus status;
  bool mismatch = false;
  status = UpdateShaders(draw_batcher_.prim_type());
  CHECK_ISSUE_UPDATE_STATUS(status, mismatch, "Unable to prepare draw shaders");
  status = UpdateRenderTargets();
  CHECK_ISSUE_UPDATE_STATUS(status, mismatch, "Unable to setup render targets");
  if (!active_framebuffer_) {
    // No framebuffer, so nothing we do will actually have an effect.
    // Treat it as a no-op.
    // TODO(benvanik): if we have a vs export, still allow it to go.
    draw_batcher_.DiscardDraw();
    return true;
  }

  status = UpdateState();
  CHECK_ISSUE_UPDATE_STATUS(status, mismatch, "Unable to setup render state");
  status = PopulateSamplers();
  CHECK_ISSUE_UPDATE_STATUS(status, mismatch,
                            "Unable to prepare draw samplers");

  status = PopulateIndexBuffer();
  CHECK_ISSUE_UPDATE_STATUS(status, mismatch, "Unable to setup index buffer");
  status = PopulateVertexBuffers();
  CHECK_ISSUE_UPDATE_STATUS(status, mismatch, "Unable to setup vertex buffers");

  if (!draw_batcher_.CommitDraw()) {
    return false;
  }
  if (!has_bindless_vbos_) {
    // TODO(benvanik): find a way to get around glVertexArrayVertexBuffer below.
    draw_batcher_.Flush(DrawBatcher::FlushMode::kMakeCoherent);
  }
  return true;
}

bool CommandProcessor::SetShadowRegister(uint32_t& dest,
                                         uint32_t register_name) {
  uint32_t value = register_file_->values[register_name].u32;
  if (dest == value) {
    return false;
  }
  dest = value;
  return true;
}

bool CommandProcessor::SetShadowRegister(float& dest, uint32_t register_name) {
  float value = register_file_->values[register_name].f32;
  if (dest == value) {
    return false;
  }
  dest = value;
  return true;
}

CommandProcessor::UpdateStatus CommandProcessor::UpdateShaders(
    PrimitiveType prim_type) {
  auto& regs = update_shaders_regs_;

  // These are the constant base addresses/ranges for shaders.
  // We have these hardcoded right now cause nothing seems to differ.
  assert_true(register_file_->values[XE_GPU_REG_SQ_VS_CONST].u32 ==
                  0x000FF000 ||
              register_file_->values[XE_GPU_REG_SQ_VS_CONST].u32 == 0x00000000);
  assert_true(register_file_->values[XE_GPU_REG_SQ_PS_CONST].u32 ==
                  0x000FF100 ||
              register_file_->values[XE_GPU_REG_SQ_PS_CONST].u32 == 0x00000000);

  bool dirty = false;
  dirty |=
      SetShadowRegister(regs.pa_su_sc_mode_cntl, XE_GPU_REG_PA_SU_SC_MODE_CNTL);
  dirty |= SetShadowRegister(regs.sq_program_cntl, XE_GPU_REG_SQ_PROGRAM_CNTL);
  dirty |= regs.vertex_shader != active_vertex_shader_;
  dirty |= regs.pixel_shader != active_pixel_shader_;
  dirty |= regs.prim_type != prim_type;
  if (!dirty) {
    return UpdateStatus::kCompatible;
  }
  regs.vertex_shader = active_vertex_shader_;
  regs.pixel_shader = active_pixel_shader_;
  regs.prim_type = prim_type;

  SCOPE_profile_cpu_f("gpu");

  draw_batcher_.Flush(DrawBatcher::FlushMode::kStateChange);

  xe_gpu_program_cntl_t program_cntl;
  program_cntl.dword_0 = regs.sq_program_cntl;
  if (!active_vertex_shader_->has_prepared()) {
    if (!active_vertex_shader_->PrepareVertexShader(program_cntl)) {
      XELOGE("Unable to prepare vertex shader");
      return UpdateStatus::kError;
    }
  } else if (!active_vertex_shader_->is_valid()) {
    XELOGE("Vertex shader invalid");
    return UpdateStatus::kError;
  }

  if (!active_pixel_shader_->has_prepared()) {
    if (!active_pixel_shader_->PreparePixelShader(program_cntl)) {
      XELOGE("Unable to prepare pixel shader");
      return UpdateStatus::kError;
    }
  } else if (!active_pixel_shader_->is_valid()) {
    XELOGE("Pixel shader invalid");
    return UpdateStatus::kError;
  }

  GLuint vertex_program = active_vertex_shader_->program();
  GLuint fragment_program = active_pixel_shader_->program();

  uint64_t key = (uint64_t(vertex_program) << 32) | fragment_program;
  CachedPipeline* cached_pipeline = nullptr;
  auto it = cached_pipelines_.find(key);
  if (it == cached_pipelines_.end()) {
    // Existing pipeline for these programs not found - create it.
    auto new_pipeline = std::make_unique<CachedPipeline>();
    new_pipeline->vertex_program = vertex_program;
    new_pipeline->fragment_program = fragment_program;
    new_pipeline->handles.default_pipeline = 0;
    cached_pipeline = new_pipeline.get();
    all_pipelines_.emplace_back(std::move(new_pipeline));
    cached_pipelines_.insert({key, cached_pipeline});
  } else {
    // Found a pipeline container - it may or may not have what we want.
    cached_pipeline = it->second;
  }
  if (!cached_pipeline->handles.default_pipeline) {
    // Perhaps it's a bit wasteful to do all of these, but oh well.
    GLuint pipelines[5];
    glCreateProgramPipelines(GLsizei(xe::countof(pipelines)), pipelines);

    glUseProgramStages(pipelines[0], GL_VERTEX_SHADER_BIT, vertex_program);
    glUseProgramStages(pipelines[0], GL_FRAGMENT_SHADER_BIT, fragment_program);
    cached_pipeline->handles.default_pipeline = pipelines[0];

    glUseProgramStages(pipelines[1], GL_VERTEX_SHADER_BIT, vertex_program);
    glUseProgramStages(pipelines[1], GL_GEOMETRY_SHADER_BIT,
                       point_list_geometry_program_);
    glUseProgramStages(pipelines[1], GL_FRAGMENT_SHADER_BIT, fragment_program);
    cached_pipeline->handles.point_list_pipeline = pipelines[1];

    glUseProgramStages(pipelines[2], GL_VERTEX_SHADER_BIT, vertex_program);
    glUseProgramStages(pipelines[2], GL_GEOMETRY_SHADER_BIT,
                       rect_list_geometry_program_);
    glUseProgramStages(pipelines[2], GL_FRAGMENT_SHADER_BIT, fragment_program);
    cached_pipeline->handles.rect_list_pipeline = pipelines[2];

    glUseProgramStages(pipelines[3], GL_VERTEX_SHADER_BIT, vertex_program);
    glUseProgramStages(pipelines[3], GL_GEOMETRY_SHADER_BIT,
                       quad_list_geometry_program_);
    glUseProgramStages(pipelines[3], GL_FRAGMENT_SHADER_BIT, fragment_program);
    cached_pipeline->handles.quad_list_pipeline = pipelines[3];

    glUseProgramStages(pipelines[4], GL_VERTEX_SHADER_BIT, vertex_program);
    glUseProgramStages(pipelines[4], GL_GEOMETRY_SHADER_BIT,
                       line_quad_list_geometry_program_);
    glUseProgramStages(pipelines[4], GL_FRAGMENT_SHADER_BIT, fragment_program);
    cached_pipeline->handles.line_quad_list_pipeline = pipelines[4];

    // This can be set once, as the buffer never changes.
    if (has_bindless_vbos_) {
      glBindVertexArray(active_vertex_shader_->vao());
      glBufferAddressRangeNV(GL_ELEMENT_ARRAY_ADDRESS_NV, 0,
                             scratch_buffer_.gpu_handle(),
                             scratch_buffer_.capacity());
    } else {
      glVertexArrayElementBuffer(active_vertex_shader_->vao(),
                                 scratch_buffer_.handle());
    }
  }

  bool line_mode = false;
  if (((regs.pa_su_sc_mode_cntl >> 3) & 0x3) != 0) {
    uint32_t front_poly_mode = (regs.pa_su_sc_mode_cntl >> 5) & 0x7;
    if (front_poly_mode == 1) {
      line_mode = true;
    }
  }

  GLuint pipeline = cached_pipeline->handles.default_pipeline;
  switch (regs.prim_type) {
    case PrimitiveType::kPointList:
      pipeline = cached_pipeline->handles.point_list_pipeline;
      break;
    case PrimitiveType::kRectangleList:
      pipeline = cached_pipeline->handles.rect_list_pipeline;
      break;
    case PrimitiveType::kQuadList: {
      if (line_mode) {
        pipeline = cached_pipeline->handles.line_quad_list_pipeline;
      } else {
        pipeline = cached_pipeline->handles.quad_list_pipeline;
      }
      break;
    }
  }

  draw_batcher_.ReconfigurePipeline(active_vertex_shader_, active_pixel_shader_,
                                    pipeline);

  glBindProgramPipeline(pipeline);
  glBindVertexArray(active_vertex_shader_->vao());

  return UpdateStatus::kMismatch;
}

CommandProcessor::UpdateStatus CommandProcessor::UpdateRenderTargets() {
  auto& regs = update_render_targets_regs_;

  bool dirty = false;
  dirty |= SetShadowRegister(regs.rb_modecontrol, XE_GPU_REG_RB_MODECONTROL);
  dirty |= SetShadowRegister(regs.rb_surface_info, XE_GPU_REG_RB_SURFACE_INFO);
  dirty |= SetShadowRegister(regs.rb_color_info, XE_GPU_REG_RB_COLOR_INFO);
  dirty |= SetShadowRegister(regs.rb_color1_info, XE_GPU_REG_RB_COLOR1_INFO);
  dirty |= SetShadowRegister(regs.rb_color2_info, XE_GPU_REG_RB_COLOR2_INFO);
  dirty |= SetShadowRegister(regs.rb_color3_info, XE_GPU_REG_RB_COLOR3_INFO);
  dirty |= SetShadowRegister(regs.rb_color_mask, XE_GPU_REG_RB_COLOR_MASK);
  dirty |= SetShadowRegister(regs.rb_depthcontrol, XE_GPU_REG_RB_DEPTHCONTROL);
  dirty |=
      SetShadowRegister(regs.rb_stencilrefmask, XE_GPU_REG_RB_STENCILREFMASK);
  dirty |= SetShadowRegister(regs.rb_depth_info, XE_GPU_REG_RB_DEPTH_INFO);
  if (!dirty) {
    return UpdateStatus::kCompatible;
  }

  SCOPE_profile_cpu_f("gpu");

  draw_batcher_.Flush(DrawBatcher::FlushMode::kStateChange);

  auto enable_mode = static_cast<ModeControl>(regs.rb_modecontrol & 0x7);

  // RB_SURFACE_INFO
  // http://fossies.org/dox/MesaLib-10.3.5/fd2__gmem_8c_source.html
  uint32_t surface_pitch = regs.rb_surface_info & 0x3FFF;
  auto surface_msaa =
      static_cast<MsaaSamples>((regs.rb_surface_info >> 16) & 0x3);

  // Get/create all color render targets, if we are using them.
  // In depth-only mode we don't need them.
  // Note that write mask may be more permissive than we want, so we mix that
  // with the actual targets the pixel shader writes to.
  GLenum draw_buffers[4] = {GL_NONE, GL_NONE, GL_NONE, GL_NONE};
  const auto& shader_targets =
      active_pixel_shader_->alloc_counts().color_targets;
  GLuint color_targets[4] = {kAnyTarget, kAnyTarget, kAnyTarget, kAnyTarget};
  if (enable_mode == ModeControl::kColorDepth) {
    uint32_t color_info[4] = {
        regs.rb_color_info, regs.rb_color1_info, regs.rb_color2_info,
        regs.rb_color3_info,
    };
    // A2XX_RB_COLOR_MASK_WRITE_* == D3DRS_COLORWRITEENABLE
    for (int n = 0; n < xe::countof(color_info); n++) {
      uint32_t write_mask = (regs.rb_color_mask >> (n * 4)) & 0xF;
      if (!write_mask || !shader_targets[n]) {
        // Unused, so keep disabled and set to wildcard so we'll take any
        // framebuffer that has it.
        continue;
      }
      uint32_t color_base = color_info[n] & 0xFFF;
      auto color_format =
          static_cast<ColorRenderTargetFormat>((color_info[n] >> 16) & 0xF);
      color_targets[n] = GetColorRenderTarget(surface_pitch, surface_msaa,
                                              color_base, color_format);
      draw_buffers[n] = GL_COLOR_ATTACHMENT0 + n;
      glColorMaski(n, !!(write_mask & 0x1), !!(write_mask & 0x2),
                   !!(write_mask & 0x4), !!(write_mask & 0x8));
    }
  }

  // Get/create depth buffer, but only if we are going to use it.
  bool uses_depth = (regs.rb_depthcontrol & 0x00000002) ||
                    (regs.rb_depthcontrol & 0x00000004);
  uint32_t stencil_write_mask = (regs.rb_stencilrefmask & 0x00FF0000) >> 16;
  bool uses_stencil =
      (regs.rb_depthcontrol & 0x00000001) || (stencil_write_mask != 0);
  GLuint depth_target = kAnyTarget;
  if (uses_depth || uses_stencil) {
    uint32_t depth_base = regs.rb_depth_info & 0xFFF;
    auto depth_format =
        static_cast<DepthRenderTargetFormat>((regs.rb_depth_info >> 16) & 0x1);
    depth_target = GetDepthRenderTarget(surface_pitch, surface_msaa, depth_base,
                                        depth_format);
    // TODO(benvanik): when a game switches does it expect to keep the same
    //     depth buffer contents?
  }

  // Get/create a framebuffer with the required targets.
  // Note that none may be returned if we really don't need one.
  auto cached_framebuffer = GetFramebuffer(color_targets, depth_target);
  active_framebuffer_ = cached_framebuffer;
  if (active_framebuffer_) {
    // Setup just the targets we want.
    glNamedFramebufferDrawBuffers(cached_framebuffer->framebuffer, 4,
                                  draw_buffers);

    // Make active.
    // TODO(benvanik): can we do this all named?
    // TODO(benvanik): do we want this on READ too?
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, cached_framebuffer->framebuffer);
  }

  return UpdateStatus::kMismatch;
}

CommandProcessor::UpdateStatus CommandProcessor::UpdateState() {
  auto& regs = *register_file_;

  bool mismatch = false;

#define CHECK_UPDATE_STATUS(status, mismatch, error_message) \
  {                                                          \
    if (status == UpdateStatus::kError) {                    \
      XELOGE(error_message);                                 \
      return status;                                         \
    } else if (status == UpdateStatus::kMismatch) {          \
      mismatch = true;                                       \
    }                                                        \
  }

  UpdateStatus status;
  status = UpdateViewportState();
  CHECK_UPDATE_STATUS(status, mismatch, "Unable to update viewport state");
  status = UpdateRasterizerState();
  CHECK_UPDATE_STATUS(status, mismatch, "Unable to update rasterizer state");
  status = UpdateBlendState();
  CHECK_UPDATE_STATUS(status, mismatch, "Unable to update blend state");
  status = UpdateDepthStencilState();
  CHECK_UPDATE_STATUS(status, mismatch, "Unable to update depth/stencil state");

  return mismatch ? UpdateStatus::kMismatch : UpdateStatus::kCompatible;
}

CommandProcessor::UpdateStatus CommandProcessor::UpdateViewportState() {
  auto& reg_file = *register_file_;
  auto& regs = update_viewport_state_regs_;

  bool dirty = false;
  // dirty |= SetShadowRegister(state_regs.pa_cl_clip_cntl,
  //     XE_GPU_REG_PA_CL_CLIP_CNTL);
  dirty |= SetShadowRegister(regs.rb_surface_info, XE_GPU_REG_RB_SURFACE_INFO);
  dirty |= SetShadowRegister(regs.pa_cl_vte_cntl, XE_GPU_REG_PA_CL_VTE_CNTL);
  dirty |=
      SetShadowRegister(regs.pa_su_sc_mode_cntl, XE_GPU_REG_PA_SU_SC_MODE_CNTL);
  dirty |= SetShadowRegister(regs.pa_sc_window_offset,
                             XE_GPU_REG_PA_SC_WINDOW_OFFSET);
  dirty |= SetShadowRegister(regs.pa_sc_window_scissor_tl,
                             XE_GPU_REG_PA_SC_WINDOW_SCISSOR_TL);
  dirty |= SetShadowRegister(regs.pa_sc_window_scissor_br,
                             XE_GPU_REG_PA_SC_WINDOW_SCISSOR_BR);
  dirty |= SetShadowRegister(regs.pa_cl_vport_xoffset,
                             XE_GPU_REG_PA_CL_VPORT_XOFFSET);
  dirty |= SetShadowRegister(regs.pa_cl_vport_yoffset,
                             XE_GPU_REG_PA_CL_VPORT_YOFFSET);
  dirty |= SetShadowRegister(regs.pa_cl_vport_zoffset,
                             XE_GPU_REG_PA_CL_VPORT_ZOFFSET);
  dirty |=
      SetShadowRegister(regs.pa_cl_vport_xscale, XE_GPU_REG_PA_CL_VPORT_XSCALE);
  dirty |=
      SetShadowRegister(regs.pa_cl_vport_yscale, XE_GPU_REG_PA_CL_VPORT_YSCALE);
  dirty |=
      SetShadowRegister(regs.pa_cl_vport_zscale, XE_GPU_REG_PA_CL_VPORT_ZSCALE);

  // Much of this state machine is extracted from:
  // https://github.com/freedreno/mesa/blob/master/src/mesa/drivers/dri/r200/r200_state.c
  // http://fossies.org/dox/MesaLib-10.3.5/fd2__gmem_8c_source.html
  // http://www.x.org/docs/AMD/old/evergreen_3D_registers_v2.pdf

  // http://www.x.org/docs/AMD/old/evergreen_3D_registers_v2.pdf
  // VTX_XY_FMT = true: the incoming X, Y have already been multiplied by 1/W0.
  //            = false: multiply the X, Y coordinates by 1/W0.
  // VTX_Z_FMT = true: the incoming Z has already been multiplied by 1/W0.
  //           = false: multiply the Z coordinate by 1/W0.
  // VTX_W0_FMT = true: the incoming W0 is not 1/W0. Perform the reciprocal to
  //                    get 1/W0.
  draw_batcher_.set_vtx_fmt((regs.pa_cl_vte_cntl >> 8) & 0x1 ? 1.0f : 0.0f,
                            (regs.pa_cl_vte_cntl >> 9) & 0x1 ? 1.0f : 0.0f,
                            (regs.pa_cl_vte_cntl >> 10) & 0x1 ? 1.0f : 0.0f);

  // Done in VS, no need to flush state.
  if ((regs.pa_cl_vte_cntl & (1 << 0)) > 0) {
    draw_batcher_.set_window_scalar(1.0f, 1.0f);
  } else {
    draw_batcher_.set_window_scalar(1.0f / 2560.0f, -1.0f / 2560.0f);
  }

  if (!dirty) {
    return UpdateStatus::kCompatible;
  }

  draw_batcher_.Flush(DrawBatcher::FlushMode::kStateChange);

  // Clipping.
  // https://github.com/freedreno/amd-gpu/blob/master/include/reg/yamato/14/yamato_genenum.h#L1587
  // bool clip_enabled = ((regs.pa_cl_clip_cntl >> 17) & 0x1) == 0;
  // bool dx_clip = ((regs.pa_cl_clip_cntl >> 19) & 0x1) == 0x1;
  //// TODO(benvanik): depth range?
  // if (dx_clip) {
  //  glClipControl(GL_UPPER_LEFT, GL_ZERO_TO_ONE);
  //} else {
  //  glClipControl(GL_LOWER_LEFT, GL_NEGATIVE_ONE_TO_ONE);
  //}

  // Window parameters.
  // http://ftp.tku.edu.tw/NetBSD/NetBSD-current/xsrc/external/mit/xf86-video-ati/dist/src/r600_reg_auto_r6xx.h
  // See r200UpdateWindow:
  // https://github.com/freedreno/mesa/blob/master/src/mesa/drivers/dri/r200/r200_state.c
  int16_t window_offset_x = 0;
  int16_t window_offset_y = 0;
  if ((regs.pa_su_sc_mode_cntl >> 16) & 1) {
    window_offset_x = regs.pa_sc_window_offset & 0x7FFF;
    window_offset_y = (regs.pa_sc_window_offset >> 16) & 0x7FFF;
    if (window_offset_x & 0x4000) {
      window_offset_x |= 0x8000;
    }
    if (window_offset_y & 0x4000) {
      window_offset_y |= 0x8000;
    }
  }

  GLint ws_x = regs.pa_sc_window_scissor_tl & 0x7FFF;
  GLint ws_y = (regs.pa_sc_window_scissor_tl >> 16) & 0x7FFF;
  GLsizei ws_w = (regs.pa_sc_window_scissor_br & 0x7FFF) - ws_x;
  GLsizei ws_h = ((regs.pa_sc_window_scissor_br >> 16) & 0x7FFF) - ws_y;
  ws_x += window_offset_x;
  ws_y += window_offset_y;
  glScissorIndexed(0, ws_x, ws_y, ws_w, ws_h);

  // HACK: no clue where to get these values.
  // RB_SURFACE_INFO
  auto surface_msaa =
      static_cast<MsaaSamples>((regs.rb_surface_info >> 16) & 0x3);
  // TODO(benvanik): ??
  float window_width_scalar = 1;
  float window_height_scalar = 1;
  switch (surface_msaa) {
    case MsaaSamples::k1X:
      break;
    case MsaaSamples::k2X:
      window_width_scalar = 2;
      break;
    case MsaaSamples::k4X:
      window_width_scalar = 2;
      window_height_scalar = 2;
      break;
  }

  // Whether each of the viewport settings are enabled.
  // http://www.x.org/docs/AMD/old/evergreen_3D_registers_v2.pdf
  bool vport_xscale_enable = (regs.pa_cl_vte_cntl & (1 << 0)) > 0;
  bool vport_xoffset_enable = (regs.pa_cl_vte_cntl & (1 << 1)) > 0;
  bool vport_yscale_enable = (regs.pa_cl_vte_cntl & (1 << 2)) > 0;
  bool vport_yoffset_enable = (regs.pa_cl_vte_cntl & (1 << 3)) > 0;
  bool vport_zscale_enable = (regs.pa_cl_vte_cntl & (1 << 4)) > 0;
  bool vport_zoffset_enable = (regs.pa_cl_vte_cntl & (1 << 5)) > 0;
  assert_true(vport_xscale_enable == vport_yscale_enable ==
              vport_zscale_enable == vport_xoffset_enable ==
              vport_yoffset_enable == vport_zoffset_enable);

  if (vport_xscale_enable) {
    float texel_offset_x = 0.0f;
    float texel_offset_y = 0.0f;
    float vox = vport_xoffset_enable ? regs.pa_cl_vport_xoffset : 0;
    float voy = vport_yoffset_enable ? regs.pa_cl_vport_yoffset : 0;
    float voz = vport_zoffset_enable ? regs.pa_cl_vport_zoffset : 0;
    float vsx = vport_xscale_enable ? regs.pa_cl_vport_xscale : 1;
    float vsy = vport_yscale_enable ? regs.pa_cl_vport_yscale : 1;
    float vsz = vport_zscale_enable ? regs.pa_cl_vport_zscale : 1;
    window_width_scalar = window_height_scalar = 1;
    float vpw = 2 * window_width_scalar * vsx;
    float vph = -2 * window_height_scalar * vsy;
    float vpx = window_width_scalar * vox - vpw / 2 + window_offset_x;
    float vpy = window_height_scalar * voy - vph / 2 + window_offset_y;
    glViewportIndexedf(0, vpx + texel_offset_x, vpy + texel_offset_y, vpw, vph);
  } else {
    float texel_offset_x = 0.0f;
    float texel_offset_y = 0.0f;
    float vpw = 2 * 2560.0f * window_width_scalar;
    float vph = 2 * 2560.0f * window_height_scalar;
    float vpx = -2560.0f * window_width_scalar + window_offset_x;
    float vpy = -2560.0f * window_height_scalar + window_offset_y;
    glViewportIndexedf(0, vpx + texel_offset_x, vpy + texel_offset_y, vpw, vph);
  }
  float voz = vport_zoffset_enable ? regs.pa_cl_vport_zoffset : 0;
  float vsz = vport_zscale_enable ? regs.pa_cl_vport_zscale : 1;
  glDepthRangef(voz, voz + vsz);

  return UpdateStatus::kMismatch;
}

CommandProcessor::UpdateStatus CommandProcessor::UpdateRasterizerState() {
  auto& regs = update_rasterizer_state_regs_;

  bool dirty = false;
  dirty |=
      SetShadowRegister(regs.pa_su_sc_mode_cntl, XE_GPU_REG_PA_SU_SC_MODE_CNTL);
  dirty |= SetShadowRegister(regs.pa_sc_screen_scissor_tl,
                             XE_GPU_REG_PA_SC_SCREEN_SCISSOR_TL);
  dirty |= SetShadowRegister(regs.pa_sc_screen_scissor_br,
                             XE_GPU_REG_PA_SC_SCREEN_SCISSOR_BR);
  dirty |= SetShadowRegister(regs.multi_prim_ib_reset_index,
                             XE_GPU_REG_VGT_MULTI_PRIM_IB_RESET_INDX);
  if (!dirty) {
    return UpdateStatus::kCompatible;
  }

  SCOPE_profile_cpu_f("gpu");

  draw_batcher_.Flush(DrawBatcher::FlushMode::kStateChange);

  // Scissoring.
  // TODO(benvanik): is this used? we are using scissoring for window scissor.
  if (regs.pa_sc_screen_scissor_tl != 0 &&
      regs.pa_sc_screen_scissor_br != 0x20002000) {
    assert_always();
    // glEnable(GL_SCISSOR_TEST);
    // TODO(benvanik): signed?
    int32_t screen_scissor_x = regs.pa_sc_screen_scissor_tl & 0x7FFF;
    int32_t screen_scissor_y = (regs.pa_sc_screen_scissor_tl >> 16) & 0x7FFF;
    int32_t screen_scissor_w =
        regs.pa_sc_screen_scissor_br & 0x7FFF - screen_scissor_x;
    int32_t screen_scissor_h =
        (regs.pa_sc_screen_scissor_br >> 16) & 0x7FFF - screen_scissor_y;
    glScissor(screen_scissor_x, screen_scissor_y, screen_scissor_w,
              screen_scissor_h);
  } else {
    // glDisable(GL_SCISSOR_TEST);
  }

  switch (regs.pa_su_sc_mode_cntl & 0x3) {
    case 0:
      glDisable(GL_CULL_FACE);
      break;
    case 1:
      glEnable(GL_CULL_FACE);
      glCullFace(GL_FRONT);
      break;
    case 2:
      glEnable(GL_CULL_FACE);
      glCullFace(GL_BACK);
      break;
  }
  if (regs.pa_su_sc_mode_cntl & 0x4) {
    glFrontFace(GL_CW);
  } else {
    glFrontFace(GL_CCW);
  }

  static const GLenum kFillModes[3] = {
      GL_POINT, GL_LINE, GL_FILL,
  };
  bool poly_mode = ((regs.pa_su_sc_mode_cntl >> 3) & 0x3) != 0;
  if (poly_mode) {
    uint32_t front_poly_mode = (regs.pa_su_sc_mode_cntl >> 5) & 0x7;
    uint32_t back_poly_mode = (regs.pa_su_sc_mode_cntl >> 8) & 0x7;
    // GL only supports both matching.
    assert_true(front_poly_mode == back_poly_mode);
    glPolygonMode(GL_FRONT_AND_BACK, kFillModes[front_poly_mode]);
  } else {
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
  }

  if (regs.pa_su_sc_mode_cntl & (1 << 19)) {
    glProvokingVertex(GL_LAST_VERTEX_CONVENTION);
  } else {
    glProvokingVertex(GL_FIRST_VERTEX_CONVENTION);
  }

  if (regs.pa_su_sc_mode_cntl & (1 << 21)) {
    glEnable(GL_PRIMITIVE_RESTART);
  } else {
    glDisable(GL_PRIMITIVE_RESTART);
  }
  glPrimitiveRestartIndex(regs.multi_prim_ib_reset_index);

  return UpdateStatus::kMismatch;
}

CommandProcessor::UpdateStatus CommandProcessor::UpdateBlendState() {
  auto& reg_file = *register_file_;
  auto& regs = update_blend_state_regs_;

  // Alpha testing -- ALPHAREF, ALPHAFUNC, ALPHATESTENABLE
  // Deprecated in GL, implemented in shader.
  // if(ALPHATESTENABLE && frag_out.a [<=/ALPHAFUNC] ALPHAREF) discard;
  uint32_t color_control = reg_file[XE_GPU_REG_RB_COLORCONTROL].u32;
  draw_batcher_.set_alpha_test((color_control & 0x4) != 0,  // ALPAHTESTENABLE
                               color_control & 0x7,         // ALPHAFUNC
                               reg_file[XE_GPU_REG_RB_ALPHA_REF].f32);

  bool dirty = false;
  dirty |=
      SetShadowRegister(regs.rb_blendcontrol[0], XE_GPU_REG_RB_BLENDCONTROL_0);
  dirty |=
      SetShadowRegister(regs.rb_blendcontrol[1], XE_GPU_REG_RB_BLENDCONTROL_1);
  dirty |=
      SetShadowRegister(regs.rb_blendcontrol[2], XE_GPU_REG_RB_BLENDCONTROL_2);
  dirty |=
      SetShadowRegister(regs.rb_blendcontrol[3], XE_GPU_REG_RB_BLENDCONTROL_3);
  dirty |= SetShadowRegister(regs.rb_blend_rgba[0], XE_GPU_REG_RB_BLEND_RED);
  dirty |= SetShadowRegister(regs.rb_blend_rgba[1], XE_GPU_REG_RB_BLEND_GREEN);
  dirty |= SetShadowRegister(regs.rb_blend_rgba[2], XE_GPU_REG_RB_BLEND_BLUE);
  dirty |= SetShadowRegister(regs.rb_blend_rgba[3], XE_GPU_REG_RB_BLEND_ALPHA);
  if (!dirty) {
    return UpdateStatus::kCompatible;
  }

  SCOPE_profile_cpu_f("gpu");

  draw_batcher_.Flush(DrawBatcher::FlushMode::kStateChange);

  static const GLenum blend_map[] = {
      /*  0 */ GL_ZERO,
      /*  1 */ GL_ONE,
      /*  2 */ GL_ZERO,  // ?
      /*  3 */ GL_ZERO,  // ?
      /*  4 */ GL_SRC_COLOR,
      /*  5 */ GL_ONE_MINUS_SRC_COLOR,
      /*  6 */ GL_SRC_ALPHA,
      /*  7 */ GL_ONE_MINUS_SRC_ALPHA,
      /*  8 */ GL_DST_COLOR,
      /*  9 */ GL_ONE_MINUS_DST_COLOR,
      /* 10 */ GL_DST_ALPHA,
      /* 11 */ GL_ONE_MINUS_DST_ALPHA,
      /* 12 */ GL_CONSTANT_COLOR,
      /* 13 */ GL_ONE_MINUS_CONSTANT_COLOR,
      /* 14 */ GL_CONSTANT_ALPHA,
      /* 15 */ GL_ONE_MINUS_CONSTANT_ALPHA,
      /* 16 */ GL_SRC_ALPHA_SATURATE,
  };
  static const GLenum blend_op_map[] = {
      /*  0 */ GL_FUNC_ADD,
      /*  1 */ GL_FUNC_SUBTRACT,
      /*  2 */ GL_MIN,
      /*  3 */ GL_MAX,
      /*  4 */ GL_FUNC_REVERSE_SUBTRACT,
  };
  for (int i = 0; i < xe::countof(regs.rb_blendcontrol); ++i) {
    uint32_t blend_control = regs.rb_blendcontrol[i];
    // A2XX_RB_BLEND_CONTROL_COLOR_SRCBLEND
    auto src_blend = blend_map[(blend_control & 0x0000001F) >> 0];
    // A2XX_RB_BLEND_CONTROL_COLOR_DESTBLEND
    auto dest_blend = blend_map[(blend_control & 0x00001F00) >> 8];
    // A2XX_RB_BLEND_CONTROL_COLOR_COMB_FCN
    auto blend_op = blend_op_map[(blend_control & 0x000000E0) >> 5];
    // A2XX_RB_BLEND_CONTROL_ALPHA_SRCBLEND
    auto src_blend_alpha = blend_map[(blend_control & 0x001F0000) >> 16];
    // A2XX_RB_BLEND_CONTROL_ALPHA_DESTBLEND
    auto dest_blend_alpha = blend_map[(blend_control & 0x1F000000) >> 24];
    // A2XX_RB_BLEND_CONTROL_ALPHA_COMB_FCN
    auto blend_op_alpha = blend_op_map[(blend_control & 0x00E00000) >> 21];
    // A2XX_RB_COLORCONTROL_BLEND_DISABLE ?? Can't find this!
    // Just guess based on actions.
    bool blend_enable =
        !((src_blend == GL_ONE) && (dest_blend == GL_ZERO) &&
          (blend_op == GL_FUNC_ADD) && (src_blend_alpha == GL_ONE) &&
          (dest_blend_alpha == GL_ZERO) && (blend_op_alpha == GL_FUNC_ADD));
    if (blend_enable) {
      glEnablei(GL_BLEND, i);
      glBlendEquationSeparatei(i, blend_op, blend_op_alpha);
      glBlendFuncSeparatei(i, src_blend, dest_blend, src_blend_alpha,
                           dest_blend_alpha);
    } else {
      glDisablei(GL_BLEND, i);
    }
  }

  glBlendColor(regs.rb_blend_rgba[0], regs.rb_blend_rgba[1],
               regs.rb_blend_rgba[2], regs.rb_blend_rgba[3]);

  return UpdateStatus::kMismatch;
}

CommandProcessor::UpdateStatus CommandProcessor::UpdateDepthStencilState() {
  auto& regs = update_depth_stencil_state_regs_;

  bool dirty = false;
  dirty |= SetShadowRegister(regs.rb_depthcontrol, XE_GPU_REG_RB_DEPTHCONTROL);
  dirty |=
      SetShadowRegister(regs.rb_stencilrefmask, XE_GPU_REG_RB_STENCILREFMASK);
  if (!dirty) {
    return UpdateStatus::kCompatible;
  }

  SCOPE_profile_cpu_f("gpu");

  draw_batcher_.Flush(DrawBatcher::FlushMode::kStateChange);

  static const GLenum compare_func_map[] = {
      /*  0 */ GL_NEVER,
      /*  1 */ GL_LESS,
      /*  2 */ GL_EQUAL,
      /*  3 */ GL_LEQUAL,
      /*  4 */ GL_GREATER,
      /*  5 */ GL_NOTEQUAL,
      /*  6 */ GL_GEQUAL,
      /*  7 */ GL_ALWAYS,
  };
  static const GLenum stencil_op_map[] = {
      /*  0 */ GL_KEEP,
      /*  1 */ GL_ZERO,
      /*  2 */ GL_REPLACE,
      /*  3 */ GL_INCR_WRAP,
      /*  4 */ GL_DECR_WRAP,
      /*  5 */ GL_INVERT,
      /*  6 */ GL_INCR,
      /*  7 */ GL_DECR,
  };
  // A2XX_RB_DEPTHCONTROL_Z_ENABLE
  if (regs.rb_depthcontrol & 0x00000002) {
    glEnable(GL_DEPTH_TEST);
  } else {
    glDisable(GL_DEPTH_TEST);
  }
  // glDisable(GL_DEPTH_TEST);
  // A2XX_RB_DEPTHCONTROL_Z_WRITE_ENABLE
  glDepthMask((regs.rb_depthcontrol & 0x00000004) ? GL_TRUE : GL_FALSE);
  // A2XX_RB_DEPTHCONTROL_EARLY_Z_ENABLE
  // ?
  // A2XX_RB_DEPTHCONTROL_ZFUNC
  glDepthFunc(compare_func_map[(regs.rb_depthcontrol & 0x00000070) >> 4]);
  // A2XX_RB_DEPTHCONTROL_STENCIL_ENABLE
  if (regs.rb_depthcontrol & 0x00000001) {
    glEnable(GL_STENCIL_TEST);
  } else {
    glDisable(GL_STENCIL_TEST);
  }
  // RB_STENCILREFMASK_STENCILREF
  uint32_t stencil_ref = (regs.rb_stencilrefmask & 0x000000FF);
  // RB_STENCILREFMASK_STENCILMASK
  uint32_t stencil_read_mask = (regs.rb_stencilrefmask & 0x0000FF00) >> 8;
  // RB_STENCILREFMASK_STENCILWRITEMASK
  glStencilMask((regs.rb_stencilrefmask & 0x00FF0000) >> 16);
  // A2XX_RB_DEPTHCONTROL_BACKFACE_ENABLE
  bool backface_enabled = (regs.rb_depthcontrol & 0x00000080) != 0;
  if (backface_enabled) {
    // A2XX_RB_DEPTHCONTROL_STENCILFUNC
    glStencilFuncSeparate(
        GL_FRONT, compare_func_map[(regs.rb_depthcontrol & 0x00000700) >> 8],
        stencil_ref, stencil_read_mask);
    // A2XX_RB_DEPTHCONTROL_STENCILFAIL
    // A2XX_RB_DEPTHCONTROL_STENCILZFAIL
    // A2XX_RB_DEPTHCONTROL_STENCILZPASS
    glStencilOpSeparate(
        GL_FRONT, stencil_op_map[(regs.rb_depthcontrol & 0x00003800) >> 11],
        stencil_op_map[(regs.rb_depthcontrol & 0x000E0000) >> 17],
        stencil_op_map[(regs.rb_depthcontrol & 0x0001C000) >> 14]);
    // A2XX_RB_DEPTHCONTROL_STENCILFUNC_BF
    glStencilFuncSeparate(
        GL_BACK, compare_func_map[(regs.rb_depthcontrol & 0x00700000) >> 20],
        stencil_ref, stencil_read_mask);
    // A2XX_RB_DEPTHCONTROL_STENCILFAIL_BF
    // A2XX_RB_DEPTHCONTROL_STENCILZFAIL_BF
    // A2XX_RB_DEPTHCONTROL_STENCILZPASS_BF
    glStencilOpSeparate(
        GL_BACK, stencil_op_map[(regs.rb_depthcontrol & 0x03800000) >> 23],
        stencil_op_map[(regs.rb_depthcontrol & 0xE0000000) >> 29],
        stencil_op_map[(regs.rb_depthcontrol & 0x1C000000) >> 26]);
  } else {
    // Backfaces disabled - treat backfaces as frontfaces.
    glStencilFunc(compare_func_map[(regs.rb_depthcontrol & 0x00000700) >> 8],
                  stencil_ref, stencil_read_mask);
    glStencilOp(stencil_op_map[(regs.rb_depthcontrol & 0x00003800) >> 11],
                stencil_op_map[(regs.rb_depthcontrol & 0x000E0000) >> 17],
                stencil_op_map[(regs.rb_depthcontrol & 0x0001C000) >> 14]);
  }

  return UpdateStatus::kMismatch;
}

CommandProcessor::UpdateStatus CommandProcessor::PopulateIndexBuffer() {
  auto& regs = *register_file_;
  auto& info = index_buffer_info_;
  if (!info.guest_base) {
    // No index buffer or auto draw.
    return UpdateStatus::kCompatible;
  }

#if FINE_GRAINED_DRAW_SCOPES
  SCOPE_profile_cpu_f("gpu");
#endif  // FINE_GRAINED_DRAW_SCOPES

  // Min/max index ranges for clamping. This is often [0g,FFFF|FFFFFF].
  // All indices should be clamped to [min,max]. May be a way to do this in GL.
  uint32_t min_index = regs[XE_GPU_REG_VGT_MIN_VTX_INDX].u32;
  uint32_t max_index = regs[XE_GPU_REG_VGT_MAX_VTX_INDX].u32;
  assert_true(min_index == 0);
  assert_true(max_index == 0xFFFF || max_index == 0xFFFFFF);

  assert_true(info.endianness == Endian::k8in16 ||
              info.endianness == Endian::k8in32);

  trace_writer_.WriteMemoryRead(info.guest_base, info.length);

  size_t total_size =
      info.count * (info.format == IndexFormat::kInt32 ? sizeof(uint32_t)
                                                       : sizeof(uint16_t));
  CircularBuffer::Allocation allocation;
  if (!scratch_buffer_.AcquireCached(info.guest_base, total_size,
                                     &allocation)) {
    if (info.format == IndexFormat::kInt32) {
      auto dest = reinterpret_cast<uint32_t*>(allocation.host_ptr);
      auto src = memory_->TranslatePhysical<const uint32_t*>(info.guest_base);
      xe::copy_and_swap_32_aligned(dest, src, info.count);
    } else {
      auto dest = reinterpret_cast<uint16_t*>(allocation.host_ptr);
      auto src = memory_->TranslatePhysical<const uint16_t*>(info.guest_base);
      xe::copy_and_swap_16_aligned(dest, src, info.count);
    }
    draw_batcher_.set_index_buffer(allocation);
    scratch_buffer_.Commit(std::move(allocation));
  } else {
    draw_batcher_.set_index_buffer(allocation);
  }

  return UpdateStatus::kCompatible;
}

CommandProcessor::UpdateStatus CommandProcessor::PopulateVertexBuffers() {
#if FINE_GRAINED_DRAW_SCOPES
  SCOPE_profile_cpu_f("gpu");
#endif  // FINE_GRAINED_DRAW_SCOPES

  auto& regs = *register_file_;
  assert_not_null(active_vertex_shader_);

  uint32_t el_index = 0;
  const auto& buffer_inputs = active_vertex_shader_->buffer_inputs();
  for (uint32_t buffer_index = 0; buffer_index < buffer_inputs.count;
       ++buffer_index) {
    const auto& desc = buffer_inputs.descs[buffer_index];
    int r = XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0 + (desc.fetch_slot / 3) * 6;
    const auto group = reinterpret_cast<xe_gpu_fetch_group_t*>(&regs.values[r]);
    const xe_gpu_vertex_fetch_t* fetch = nullptr;
    switch (desc.fetch_slot % 3) {
      case 0:
        fetch = &group->vertex_fetch_0;
        break;
      case 1:
        fetch = &group->vertex_fetch_1;
        break;
      case 2:
        fetch = &group->vertex_fetch_2;
        break;
    }
    assert_true(fetch->endian == 2);

    size_t valid_range = size_t(fetch->size * 4);

    trace_writer_.WriteMemoryRead(fetch->address << 2, valid_range);

    CircularBuffer::Allocation allocation;
    if (!scratch_buffer_.AcquireCached(fetch->address << 2, valid_range,
                                       &allocation)) {
      // Copy and byte swap the entire buffer.
      // We could be smart about this to save GPU bandwidth by building a CRC
      // as we copy and only if it differs from the previous value committing
      // it (and if it matches just discard and reuse).
      xe::copy_and_swap_32_aligned(
          reinterpret_cast<uint32_t*>(allocation.host_ptr),
          memory_->TranslatePhysical<const uint32_t*>(fetch->address << 2),
          valid_range / 4);

      if (!has_bindless_vbos_) {
        // TODO(benvanik): if we could find a way to avoid this, we could use
        // multidraw without flushing.
        glVertexArrayVertexBuffer(active_vertex_shader_->vao(), buffer_index,
                                  scratch_buffer_.handle(), allocation.offset,
                                  desc.stride_words * 4);
      }

      if (has_bindless_vbos_) {
        for (uint32_t i = 0; i < desc.element_count; ++i, ++el_index) {
          const auto& el = desc.elements[i];
          draw_batcher_.set_vertex_buffer(el_index, 0, desc.stride_words * 4,
                                          allocation);
        }
      }

      scratch_buffer_.Commit(std::move(allocation));
    } else {
      if (!has_bindless_vbos_) {
        // TODO(benvanik): if we could find a way to avoid this, we could use
        // multidraw without flushing.
        glVertexArrayVertexBuffer(active_vertex_shader_->vao(), buffer_index,
                                  scratch_buffer_.handle(), allocation.offset,
                                  desc.stride_words * 4);
      }

      if (has_bindless_vbos_) {
        for (uint32_t i = 0; i < desc.element_count; ++i, ++el_index) {
          const auto& el = desc.elements[i];
          draw_batcher_.set_vertex_buffer(el_index, 0, desc.stride_words * 4,
                                          allocation);
        }
      }
    }
  }

  return UpdateStatus::kCompatible;
}

CommandProcessor::UpdateStatus CommandProcessor::PopulateSamplers() {
#if FINE_GRAINED_DRAW_SCOPES
  SCOPE_profile_cpu_f("gpu");
#endif  // FINE_GRAINED_DRAW_SCOPES

  auto& regs = *register_file_;

  bool mismatch = false;

  // VS and PS samplers are shared, but may be used exclusively.
  // We walk each and setup lazily.
  bool has_setup_sampler[32] = {false};

  // Vertex texture samplers.
  const auto& vertex_sampler_inputs = active_vertex_shader_->sampler_inputs();
  for (size_t i = 0; i < vertex_sampler_inputs.count; ++i) {
    const auto& desc = vertex_sampler_inputs.descs[i];
    if (has_setup_sampler[desc.fetch_slot]) {
      continue;
    }
    has_setup_sampler[desc.fetch_slot] = true;
    auto status = PopulateSampler(desc);
    if (status == UpdateStatus::kError) {
      return status;
    } else if (status == UpdateStatus::kMismatch) {
      mismatch = true;
    }
  }

  // Pixel shader texture sampler.
  const auto& pixel_sampler_inputs = active_pixel_shader_->sampler_inputs();
  for (size_t i = 0; i < pixel_sampler_inputs.count; ++i) {
    const auto& desc = pixel_sampler_inputs.descs[i];
    if (has_setup_sampler[desc.fetch_slot]) {
      continue;
    }
    has_setup_sampler[desc.fetch_slot] = true;
    auto status = PopulateSampler(desc);
    if (status == UpdateStatus::kError) {
      return UpdateStatus::kError;
    } else if (status == UpdateStatus::kMismatch) {
      mismatch = true;
    }
  }

  return mismatch ? UpdateStatus::kMismatch : UpdateStatus::kCompatible;
}

CommandProcessor::UpdateStatus CommandProcessor::PopulateSampler(
    const Shader::SamplerDesc& desc) {
  auto& regs = *register_file_;
  int r = XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0 + desc.fetch_slot * 6;
  auto group = reinterpret_cast<const xe_gpu_fetch_group_t*>(&regs.values[r]);
  auto& fetch = group->texture_fetch;

  // Reset slot.
  // If we fail, we still draw but with an invalid texture.
  draw_batcher_.set_texture_sampler(desc.fetch_slot, 0);

  if (FLAGS_disable_textures) {
    return UpdateStatus::kCompatible;
  }

  // ?
  if (!fetch.type) {
    return UpdateStatus::kCompatible;
  }
  assert_true(fetch.type == 0x2);

  TextureInfo texture_info;
  if (!TextureInfo::Prepare(fetch, &texture_info)) {
    XELOGE("Unable to parse texture fetcher info");
    return UpdateStatus::kCompatible;  // invalid texture used
  }
  SamplerInfo sampler_info;
  if (!SamplerInfo::Prepare(fetch, desc.tex_fetch, &sampler_info)) {
    XELOGE("Unable to parse sampler info");
    return UpdateStatus::kCompatible;  // invalid texture used
  }

  trace_writer_.WriteMemoryRead(texture_info.guest_address,
                                texture_info.input_length);

  auto entry_view = texture_cache_.Demand(texture_info, sampler_info);
  if (!entry_view) {
    // Unable to create/fetch/etc.
    XELOGE("Failed to demand texture");
    return UpdateStatus::kCompatible;
  }

  // Shaders will use bindless to fetch right from it.
  draw_batcher_.set_texture_sampler(desc.fetch_slot,
                                    entry_view->texture_sampler_handle);

  return UpdateStatus::kCompatible;
}

bool CommandProcessor::IssueCopy() {
  SCOPE_profile_cpu_f("gpu");
  auto& regs = *register_file_;

  // This is used to resolve surfaces, taking them from EDRAM render targets
  // to system memory. It can optionally clear color/depth surfaces, too.
  // The command buffer has stuff for actually doing this by drawing, however
  // we should be able to do it without that much easier.

  uint32_t copy_control = regs[XE_GPU_REG_RB_COPY_CONTROL].u32;
  // Render targets 0-3, 4 = depth
  uint32_t copy_src_select = copy_control & 0x7;
  bool color_clear_enabled = (copy_control >> 8) & 0x1;
  bool depth_clear_enabled = (copy_control >> 9) & 0x1;
  auto copy_command = static_cast<CopyCommand>((copy_control >> 20) & 0x3);

  uint32_t copy_dest_info = regs[XE_GPU_REG_RB_COPY_DEST_INFO].u32;
  auto copy_dest_endian = static_cast<Endian128>(copy_dest_info & 0x7);
  uint32_t copy_dest_array = (copy_dest_info >> 3) & 0x1;
  assert_true(copy_dest_array == 0);
  uint32_t copy_dest_slice = (copy_dest_info >> 4) & 0x7;
  assert_true(copy_dest_slice == 0);
  auto copy_dest_format =
      static_cast<ColorFormat>((copy_dest_info >> 7) & 0x3F);
  uint32_t copy_dest_number = (copy_dest_info >> 13) & 0x7;
  // assert_true(copy_dest_number == 0); // ?
  uint32_t copy_dest_bias = (copy_dest_info >> 16) & 0x3F;
  // assert_true(copy_dest_bias == 0);
  uint32_t copy_dest_swap = (copy_dest_info >> 25) & 0x1;

  uint32_t copy_dest_base = regs[XE_GPU_REG_RB_COPY_DEST_BASE].u32;
  uint32_t copy_dest_pitch = regs[XE_GPU_REG_RB_COPY_DEST_PITCH].u32;
  uint32_t copy_dest_height = (copy_dest_pitch >> 16) & 0x3FFF;
  copy_dest_pitch &= 0x3FFF;

  // None of this is supported yet:
  uint32_t copy_surface_slice = regs[XE_GPU_REG_RB_COPY_SURFACE_SLICE].u32;
  assert_true(copy_surface_slice == 0);
  uint32_t copy_func = regs[XE_GPU_REG_RB_COPY_FUNC].u32;
  assert_true(copy_func == 0);
  uint32_t copy_ref = regs[XE_GPU_REG_RB_COPY_REF].u32;
  assert_true(copy_ref == 0);
  uint32_t copy_mask = regs[XE_GPU_REG_RB_COPY_MASK].u32;
  assert_true(copy_mask == 0);

  // RB_SURFACE_INFO
  // http://fossies.org/dox/MesaLib-10.3.5/fd2__gmem_8c_source.html
  uint32_t surface_info = regs[XE_GPU_REG_RB_SURFACE_INFO].u32;
  uint32_t surface_pitch = surface_info & 0x3FFF;
  auto surface_msaa = static_cast<MsaaSamples>((surface_info >> 16) & 0x3);

  // Depending on the source, pick the buffer we'll be sourcing.
  // We then query for a cached framebuffer setup with that buffer active.
  TextureFormat src_format = TextureFormat::kUnknown;
  GLuint color_targets[4] = {kAnyTarget, kAnyTarget, kAnyTarget, kAnyTarget};
  GLuint depth_target = kAnyTarget;
  if (copy_src_select <= 3) {
    // Source from a color target.
    uint32_t color_info[4] = {
        regs[XE_GPU_REG_RB_COLOR_INFO].u32, regs[XE_GPU_REG_RB_COLOR1_INFO].u32,
        regs[XE_GPU_REG_RB_COLOR2_INFO].u32,
        regs[XE_GPU_REG_RB_COLOR3_INFO].u32,
    };
    uint32_t color_base = color_info[copy_src_select] & 0xFFF;
    auto color_format = static_cast<ColorRenderTargetFormat>(
        (color_info[copy_src_select] >> 16) & 0xF);
    color_targets[copy_src_select] = GetColorRenderTarget(
        surface_pitch, surface_msaa, color_base, color_format);
    src_format = ColorRenderTargetToTextureFormat(color_format);
  } else {
    // Source from depth/stencil.
    uint32_t depth_info = regs[XE_GPU_REG_RB_DEPTH_INFO].u32;
    uint32_t depth_base = depth_info & 0xFFF;
    auto depth_format =
        static_cast<DepthRenderTargetFormat>((depth_info >> 16) & 0x1);
    depth_target = GetDepthRenderTarget(surface_pitch, surface_msaa, depth_base,
                                        depth_format);
    src_format = DepthRenderTargetToTextureFormat(depth_format);
  }
  auto source_framebuffer = GetFramebuffer(color_targets, depth_target);
  if (!source_framebuffer) {
    // If we get here we are likely missing some state checks.
    assert_always("No framebuffer for copy source? no-op copy?");
    XELOGE("No framebuffer for copy source");
    return false;
  }

  GLenum read_format;
  GLenum read_type;
  switch (copy_dest_format) {
    case ColorFormat::k_2_10_10_10:
      read_format = GL_RGB10_A2;
      read_type = GL_UNSIGNED_INT_10_10_10_2;
      break;
    case ColorFormat::k_4_4_4_4:
      read_format = GL_RGBA4;
      read_type = GL_UNSIGNED_SHORT_4_4_4_4;
      break;
    case ColorFormat::k_5_6_5:
      read_format = GL_RGB565;
      read_type = GL_UNSIGNED_SHORT_5_6_5;
      break;
    case ColorFormat::k_8:
      read_format = GL_R8;
      read_type = GL_UNSIGNED_BYTE;
      break;
    case ColorFormat::k_8_8_8_8:
      read_format = copy_dest_swap ? GL_BGRA : GL_RGBA;
      read_type = GL_UNSIGNED_BYTE;
      break;
    case ColorFormat::k_16:
      read_format = GL_R16;
      read_type = GL_UNSIGNED_SHORT;
      break;
    case ColorFormat::k_16_FLOAT:
      read_format = GL_R16;
      read_type = GL_HALF_FLOAT;
      break;
    case ColorFormat::k_16_16:
      read_format = GL_RG16;
      read_type = GL_UNSIGNED_SHORT;
      break;
    case ColorFormat::k_16_16_FLOAT:
      read_format = GL_RG16;
      read_type = GL_HALF_FLOAT;
      break;
    case ColorFormat::k_16_16_16_16_FLOAT:
      read_format = GL_RGBA;
      read_type = GL_HALF_FLOAT;
      break;
    case ColorFormat::k_32_FLOAT:
      read_format = GL_R32F;
      read_type = GL_FLOAT;
      break;
    case ColorFormat::k_32_32_FLOAT:
      read_format = GL_RG32F;
      read_type = GL_FLOAT;
      break;
    default:
      assert_unhandled_case(copy_dest_format);
      return false;
  }

  // TODO(benvanik): swap channel ordering on copy_dest_swap
  //                 Can we use GL swizzles for this?

  // Swap byte order during read.
  // TODO(benvanik): handle other endian modes.
  switch (copy_dest_endian) {
    case Endian128::kUnspecified:
      glPixelStorei(GL_PACK_SWAP_BYTES, GL_FALSE);
      break;
    case Endian128::k8in32:
      glPixelStorei(GL_PACK_SWAP_BYTES, GL_TRUE);
      break;
    default:
      // assert_unhandled_case(copy_dest_endian);
      glPixelStorei(GL_PACK_SWAP_BYTES, GL_TRUE);
      break;
  }

  // TODO(benvanik): tweak alignments/strides.
  // glPixelStorei(GL_PACK_ALIGNMENT, 1);
  // glPixelStorei(GL_PACK_ROW_LENGTH, 0);
  // glPixelStorei(GL_PACK_IMAGE_HEIGHT, 0);

  // TODO(benvanik): any way to scissor this? a200 has:
  // REG_A2XX_RB_COPY_DEST_OFFSET = A2XX_RB_COPY_DEST_OFFSET_X(tile->xoff) |
  //                                A2XX_RB_COPY_DEST_OFFSET_Y(tile->yoff);
  // but I can't seem to find something similar.
  uint32_t dest_logical_width = copy_dest_pitch;
  uint32_t dest_logical_height = copy_dest_height;
  uint32_t dest_block_width = xe::round_up(dest_logical_width, 32);
  uint32_t dest_block_height = xe::round_up(dest_logical_height, 32);

  uint32_t window_offset = regs[XE_GPU_REG_PA_SC_WINDOW_OFFSET].u32;
  int16_t window_offset_x = window_offset & 0x7FFF;
  int16_t window_offset_y = (window_offset >> 16) & 0x7FFF;
  if (window_offset_x & 0x4000) {
    window_offset_x |= 0x8000;
  }
  if (window_offset_y & 0x4000) {
    window_offset_y |= 0x8000;
  }

  // HACK: vertices to use are always in vf0.
  int copy_vertex_fetch_slot = 0;
  int r =
      XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0 + (copy_vertex_fetch_slot / 3) * 6;
  const auto group = reinterpret_cast<xe_gpu_fetch_group_t*>(&regs.values[r]);
  const xe_gpu_vertex_fetch_t* fetch = nullptr;
  switch (copy_vertex_fetch_slot % 3) {
    case 0:
      fetch = &group->vertex_fetch_0;
      break;
    case 1:
      fetch = &group->vertex_fetch_1;
      break;
    case 2:
      fetch = &group->vertex_fetch_2;
      break;
  }
  assert_true(fetch->type == 3);
  assert_true(fetch->endian == 2);
  assert_true(fetch->size == 6);
  const uint8_t* vertex_addr = memory_->TranslatePhysical(fetch->address << 2);
  trace_writer_.WriteMemoryRead(fetch->address << 2, fetch->size * 4);
  int32_t dest_min_x = int32_t((std::min(
      std::min(
          GpuSwap(xe::load<float>(vertex_addr + 0), Endian(fetch->endian)),
          GpuSwap(xe::load<float>(vertex_addr + 8), Endian(fetch->endian))),
      GpuSwap(xe::load<float>(vertex_addr + 16), Endian(fetch->endian)))));
  int32_t dest_max_x = int32_t((std::max(
      std::max(
          GpuSwap(xe::load<float>(vertex_addr + 0), Endian(fetch->endian)),
          GpuSwap(xe::load<float>(vertex_addr + 8), Endian(fetch->endian))),
      GpuSwap(xe::load<float>(vertex_addr + 16), Endian(fetch->endian)))));
  int32_t dest_min_y = int32_t((std::min(
      std::min(
          GpuSwap(xe::load<float>(vertex_addr + 4), Endian(fetch->endian)),
          GpuSwap(xe::load<float>(vertex_addr + 12), Endian(fetch->endian))),
      GpuSwap(xe::load<float>(vertex_addr + 20), Endian(fetch->endian)))));
  int32_t dest_max_y = int32_t((std::max(
      std::max(
          GpuSwap(xe::load<float>(vertex_addr + 4), Endian(fetch->endian)),
          GpuSwap(xe::load<float>(vertex_addr + 12), Endian(fetch->endian))),
      GpuSwap(xe::load<float>(vertex_addr + 20), Endian(fetch->endian)))));
  Rect2D dest_rect(dest_min_x, dest_min_y, dest_max_x - dest_min_x,
                   dest_max_y - dest_min_y);
  Rect2D src_rect(0, 0, dest_rect.width, dest_rect.height);

  // The dest base address passed in has already been offset by the window
  // offset, so to ensure texture lookup works we need to offset it.
  // TODO(benvanik): allow texture cache to lookup partial textures.
  // TODO(benvanik): change based on format.
  int32_t dest_offset = window_offset_y * copy_dest_pitch * 4;
  dest_offset += window_offset_x * 32 * 4;
  copy_dest_base += dest_offset;

  // Destination pointer in guest memory.
  // We have GL throw bytes directly into it.
  // TODO(benvanik): copy to staging texture then PBO back?
  void* ptr = memory_->TranslatePhysical(copy_dest_base);

  // Make active so glReadPixels reads from us.
  switch (copy_command) {
    case CopyCommand::kRaw: {
      // This performs a byte-for-byte copy of the textures from src to dest
      // with no conversion. Byte swapping may still occur.
      if (copy_src_select <= 3) {
        // Source from a bound render target.
        // TODO(benvanik): RAW copy.
        last_framebuffer_texture_ = texture_cache_.CopyTexture(
            context_->blitter(), copy_dest_base, dest_logical_width,
            dest_logical_height, dest_block_width, dest_block_height,
            ColorFormatToTextureFormat(copy_dest_format),
            copy_dest_swap ? true : false, color_targets[copy_src_select],
            src_rect, dest_rect);
        if (!FLAGS_disable_framebuffer_readback) {
          // glReadPixels(x, y, w, h, read_format, read_type, ptr);
        }
      } else {
        // Source from the bound depth/stencil target.
        // TODO(benvanik): RAW copy.
        texture_cache_.CopyTexture(context_->blitter(), copy_dest_base,
                                   dest_logical_width, dest_logical_height,
                                   dest_block_width, dest_block_height,
                                   src_format, copy_dest_swap ? true : false,
                                   depth_target, src_rect, dest_rect);
        if (!FLAGS_disable_framebuffer_readback) {
          // glReadPixels(x, y, w, h, GL_DEPTH_STENCIL, read_type, ptr);
        }
      }
      break;
    }
    case CopyCommand::kConvert: {
      if (copy_src_select <= 3) {
        // Source from a bound render target.
        // Either copy the readbuffer into an existing texture or create a new
        // one in the cache so we can service future upload requests.
        last_framebuffer_texture_ = texture_cache_.ConvertTexture(
            context_->blitter(), copy_dest_base, dest_logical_width,
            dest_logical_height, dest_block_width, dest_block_height,
            ColorFormatToTextureFormat(copy_dest_format),
            copy_dest_swap ? true : false, color_targets[copy_src_select],
            src_rect, dest_rect);
        if (!FLAGS_disable_framebuffer_readback) {
          // glReadPixels(x, y, w, h, read_format, read_type, ptr);
        }
      } else {
        // Source from the bound depth/stencil target.
        texture_cache_.ConvertTexture(context_->blitter(), copy_dest_base,
                                      dest_logical_width, dest_logical_height,
                                      dest_block_width, dest_block_height,
                                      src_format, copy_dest_swap ? true : false,
                                      depth_target, src_rect, dest_rect);
        if (!FLAGS_disable_framebuffer_readback) {
          // glReadPixels(x, y, w, h, GL_DEPTH_STENCIL, read_type, ptr);
        }
      }
      break;
    }
    case CopyCommand::kConstantOne:
    case CopyCommand::kNull:
    default:
      // assert_unhandled_case(copy_command);
      return false;
  }

  // Perform any requested clears.
  uint32_t copy_depth_clear = regs[XE_GPU_REG_RB_DEPTH_CLEAR].u32;
  uint32_t copy_color_clear = regs[XE_GPU_REG_RB_COLOR_CLEAR].u32;
  uint32_t copy_color_clear_low = regs[XE_GPU_REG_RB_COLOR_CLEAR_LOW].u32;
  assert_true(copy_color_clear == copy_color_clear_low);

  if (color_clear_enabled) {
    // Clear the render target we selected for copy.
    assert_true(copy_src_select < 3);
    // TODO(benvanik): verify color order.
    float color[] = {(copy_color_clear & 0xFF) / 255.0f,
                     ((copy_color_clear >> 8) & 0xFF) / 255.0f,
                     ((copy_color_clear >> 16) & 0xFF) / 255.0f,
                     ((copy_color_clear >> 24) & 0xFF) / 255.0f};
    // TODO(benvanik): remove query.
    GLboolean old_color_mask[4];
    glGetBooleani_v(GL_COLOR_WRITEMASK, copy_src_select, old_color_mask);
    glColorMaski(copy_src_select, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClearNamedFramebufferfv(source_framebuffer->framebuffer, GL_COLOR,
                              copy_src_select, color);
    glColorMaski(copy_src_select, old_color_mask[0], old_color_mask[1],
                 old_color_mask[2], old_color_mask[3]);
  }

  // TODO(benvanik): figure out real condition here (maybe when color cleared?)
  // HACK: things seem to need their depth buffer cleared a lot more
  // than as indicated by the depth_clear_enabled flag.
  // if (depth_clear_enabled) {
  if (depth_target != kAnyTarget) {
    // Clear the current depth buffer.
    // TODO(benvanik): verify format.
    GLfloat depth = {(copy_depth_clear & 0xFFFFFF00) / float(0xFFFFFF00)};
    GLint stencil = copy_depth_clear & 0xFF;
    GLint old_draw_framebuffer;
    GLboolean old_depth_mask;
    GLint old_stencil_mask;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &old_draw_framebuffer);
    glGetBooleanv(GL_DEPTH_WRITEMASK, &old_depth_mask);
    glGetIntegerv(GL_STENCIL_WRITEMASK, &old_stencil_mask);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, source_framebuffer->framebuffer);
    glDepthMask(GL_TRUE);
    glStencilMask(0xFF);
    // HACK: this should work, but throws INVALID_ENUM on nvidia drivers.
    /* glClearNamedFramebufferfi(source_framebuffer->framebuffer,
                             GL_DEPTH_STENCIL,
                             depth, stencil);*/
    glClearBufferfi(GL_DEPTH_STENCIL, 0, depth, stencil);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, old_draw_framebuffer);
    glDepthMask(old_depth_mask);
    glStencilMask(old_stencil_mask);
  }

  return true;
}

GLuint CommandProcessor::GetColorRenderTarget(uint32_t pitch,
                                              MsaaSamples samples,
                                              uint32_t base,
                                              ColorRenderTargetFormat format) {
  // Because we don't know the height of anything, we allocate at full res.
  // At 2560x2560, it's impossible for EDRAM to fit anymore.
  uint32_t width = 2560;
  uint32_t height = 2560;

  // NOTE: we strip gamma formats down to normal ones.
  if (format == ColorRenderTargetFormat::k_8_8_8_8_GAMMA) {
    format = ColorRenderTargetFormat::k_8_8_8_8;
  }

  for (auto& it = cached_color_render_targets_.begin();
       it != cached_color_render_targets_.end(); ++it) {
    if (it->base == base && it->width == width && it->height == height &&
        it->format == format) {
      return it->texture;
    }
  }
  cached_color_render_targets_.push_back(CachedColorRenderTarget());
  auto cached = &cached_color_render_targets_.back();
  cached->base = base;
  cached->width = width;
  cached->height = height;
  cached->format = format;

  GLenum internal_format;
  switch (format) {
    case ColorRenderTargetFormat::k_8_8_8_8:
    case ColorRenderTargetFormat::k_8_8_8_8_GAMMA:
      internal_format = GL_RGBA8;
      break;
    case ColorRenderTargetFormat::k_2_10_10_10:
    case ColorRenderTargetFormat::k_2_10_10_10_unknown:
      internal_format = GL_RGB10_A2UI;
      break;
    case ColorRenderTargetFormat::k_2_10_10_10_FLOAT:
    case ColorRenderTargetFormat::k_2_10_10_10_FLOAT_unknown:
      internal_format = GL_RGB10_A2;
      break;
    case ColorRenderTargetFormat::k_16_16:
      internal_format = GL_RG16;
      break;
    case ColorRenderTargetFormat::k_16_16_FLOAT:
      internal_format = GL_RG16F;
      break;
    case ColorRenderTargetFormat::k_16_16_16_16:
      internal_format = GL_RGBA16;
      break;
    case ColorRenderTargetFormat::k_16_16_16_16_FLOAT:
      internal_format = GL_RGBA16F;
      break;
    case ColorRenderTargetFormat::k_32_FLOAT:
      internal_format = GL_R32F;
      break;
    case ColorRenderTargetFormat::k_32_32_FLOAT:
      internal_format = GL_RG32F;
      break;
    default:
      assert_unhandled_case(format);
      return 0;
  }

  glCreateTextures(GL_TEXTURE_2D, 1, &cached->texture);
  glTextureStorage2D(cached->texture, 1, internal_format, width, height);

  return cached->texture;
}

GLuint CommandProcessor::GetDepthRenderTarget(uint32_t pitch,
                                              MsaaSamples samples,
                                              uint32_t base,
                                              DepthRenderTargetFormat format) {
  uint32_t width = 2560;
  uint32_t height = 2560;

  for (auto& it = cached_depth_render_targets_.begin();
       it != cached_depth_render_targets_.end(); ++it) {
    if (it->base == base && it->width == width && it->height == height &&
        it->format == format) {
      return it->texture;
    }
  }
  cached_depth_render_targets_.push_back(CachedDepthRenderTarget());
  auto cached = &cached_depth_render_targets_.back();
  cached->base = base;
  cached->width = width;
  cached->height = height;
  cached->format = format;

  GLenum internal_format;
  switch (format) {
    case DepthRenderTargetFormat::kD24S8:
      internal_format = GL_DEPTH24_STENCIL8;
      break;
    case DepthRenderTargetFormat::kD24FS8:
      // TODO(benvanik): not supported in GL?
      internal_format = GL_DEPTH24_STENCIL8;
      break;
    default:
      assert_unhandled_case(format);
      return 0;
  }

  glCreateTextures(GL_TEXTURE_2D, 1, &cached->texture);
  glTextureStorage2D(cached->texture, 1, internal_format, width, height);

  return cached->texture;
}

CommandProcessor::CachedFramebuffer* CommandProcessor::GetFramebuffer(
    GLuint color_targets[4], GLuint depth_target) {
  for (auto& it = cached_framebuffers_.begin();
       it != cached_framebuffers_.end(); ++it) {
    if ((depth_target == kAnyTarget || it->depth_target == depth_target) &&
        (color_targets[0] == kAnyTarget ||
         it->color_targets[0] == color_targets[0]) &&
        (color_targets[1] == kAnyTarget ||
         it->color_targets[1] == color_targets[1]) &&
        (color_targets[2] == kAnyTarget ||
         it->color_targets[2] == color_targets[2]) &&
        (color_targets[3] == kAnyTarget ||
         it->color_targets[3] == color_targets[3])) {
      return &*it;
    }
  }

  GLuint real_color_targets[4];
  bool any_set = false;
  for (int i = 0; i < 4; ++i) {
    if (color_targets[i] == kAnyTarget) {
      real_color_targets[i] = 0;
    } else {
      any_set = true;
      real_color_targets[i] = color_targets[i];
    }
  }
  GLuint real_depth_target;
  if (depth_target == kAnyTarget) {
    real_depth_target = 0;
  } else {
    any_set = true;
    real_depth_target = depth_target;
  }
  if (!any_set) {
    // No framebuffer required.
    return nullptr;
  }

  cached_framebuffers_.push_back(CachedFramebuffer());
  auto cached = &cached_framebuffers_.back();
  glCreateFramebuffers(1, &cached->framebuffer);
  for (int i = 0; i < 4; ++i) {
    cached->color_targets[i] = real_color_targets[i];
    glNamedFramebufferTexture(cached->framebuffer, GL_COLOR_ATTACHMENT0 + i,
                              real_color_targets[i], 0);
  }
  cached->depth_target = real_depth_target;
  glNamedFramebufferTexture(cached->framebuffer, GL_DEPTH_STENCIL_ATTACHMENT,
                            real_depth_target, 0);

  return cached;
}

}  // namespace gl4
}  // namespace gpu
}  // namespace xe
