/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_TRACE_WRITER_H_
#define XENIA_GPU_TRACE_WRITER_H_

#include <filesystem>
#include <set>
#include <string>

#include "xenia/gpu/registers.h"
#include "xenia/gpu/trace_protocol.h"

// only enable trace writer in debug builds, measured hit from the trace
// function calls (even if they just immediately return) is 0.40-0.60% cpu time
// total. with inlining they just bloat the caller and negatively impact
// register allocation for the caller
#ifdef NDEBUG
#define XE_ENABLE_TRACE_WRITER_INSTRUMENTATION 0
#else
#define XE_ENABLE_TRACE_WRITER_INSTRUMENTATION 1
#endif

namespace xe {
namespace gpu {

class TraceWriter {
 public:
#if XE_ENABLE_TRACE_WRITER_INSTRUMENTATION == 1
  explicit TraceWriter(uint8_t* membase);
  ~TraceWriter();

  bool is_open() const { return file_ != nullptr; }

  bool Open(const std::filesystem::path& path, uint32_t title_id);
  void Flush();
  void Close();

  void WritePrimaryBufferStart(uint32_t base_ptr, uint32_t count);
  void WritePrimaryBufferEnd();
  void WriteIndirectBufferStart(uint32_t base_ptr, uint32_t count);
  void WriteIndirectBufferEnd();
  void WritePacketStart(uint32_t base_ptr, uint32_t count);
  void WritePacketEnd();
  void WriteMemoryRead(uint32_t base_ptr, size_t length,
                       const void* host_ptr = nullptr);
  void WriteMemoryReadCached(uint32_t base_ptr, size_t length);
  void WriteMemoryReadCachedNop(uint32_t base_ptr, size_t length);
  void WriteMemoryWrite(uint32_t base_ptr, size_t length,
                        const void* host_ptr = nullptr);
  void WriteEdramSnapshot(const void* snapshot);
  void WriteEvent(EventCommand::Type event_type);
  void WriteRegisters(uint32_t first_register, const uint32_t* register_values,
                      uint32_t register_count, bool execute_callbacks_on_play);
  void WriteGammaRamp(const reg::DC_LUT_30_COLOR* gamma_ramp_256_entry_table,
                      const reg::DC_LUT_PWL_DATA* gamma_ramp_pwl_rgb,
                      uint32_t gamma_ramp_rw_component);

 private:
  void WriteMemoryCommand(TraceCommandType type, uint32_t base_ptr,
                          size_t length, const void* host_ptr = nullptr);

  std::set<uint64_t> cached_memory_reads_;
  uint8_t* membase_;
  FILE* file_;

  bool compress_output_ = true;
  size_t compression_threshold_ = 1024;  // Min. number of bytes to compress.

#else
  // this could be annoying to maintain if new methods are added or the
  // signatures change
  constexpr explicit TraceWriter(uint8_t* membase) {}

  static constexpr bool is_open() { return false; }

  static constexpr bool Open(const std::filesystem::path& path,
                             uint32_t title_id) {
    return false;
  }
  static constexpr void Flush() {}
  static constexpr void Close() {}

  static constexpr void WritePrimaryBufferStart(uint32_t base_ptr,
                                                uint32_t count) {}
  static constexpr void WritePrimaryBufferEnd() {}
  static constexpr void WriteIndirectBufferStart(uint32_t base_ptr,
                                                 uint32_t count) {}
  static constexpr void WriteIndirectBufferEnd() {}
  static constexpr void WritePacketStart(uint32_t base_ptr, uint32_t count) {}
  static constexpr void WritePacketEnd() {}
  static constexpr void WriteMemoryRead(uint32_t base_ptr, size_t length,
                                        const void* host_ptr = nullptr) {}
  static constexpr void WriteMemoryReadCached(uint32_t base_ptr,
                                              size_t length) {}
  static constexpr void WriteMemoryReadCachedNop(uint32_t base_ptr,
                                                 size_t length) {}
  static constexpr void WriteMemoryWrite(uint32_t base_ptr, size_t length,
                                         const void* host_ptr = nullptr) {}
  static constexpr void WriteEdramSnapshot(const void* snapshot) {}
  static constexpr void WriteEvent(EventCommand::Type event_type) {}
  static constexpr void WriteRegisters(uint32_t first_register,
                                       const uint32_t* register_values,
                                       uint32_t register_count,
                                       bool execute_callbacks_on_play) {}
  static constexpr void WriteGammaRamp(
      const reg::DC_LUT_30_COLOR* gamma_ramp_256_entry_table,
      const reg::DC_LUT_PWL_DATA* gamma_ramp_pwl_rgb,
      uint32_t gamma_ramp_rw_component) {}

#endif
};

}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_TRACE_WRITER_H_
