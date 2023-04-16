/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/backend/x64/x64_tracers.h"

#include <cinttypes>

#include "xenia/base/logging.h"
#include "xenia/base/vec128.h"
#include "xenia/cpu/backend/x64/x64_emitter.h"
#include "xenia/cpu/processor.h"
#include "xenia/cpu/thread_state.h"

namespace xe {
namespace cpu {
namespace backend {
namespace x64 {

#define ITRACE 0
#define DTRACE 0

#define TARGET_THREAD 0

bool trace_enabled = true;

#define THREAD_MATCH \
  (!TARGET_THREAD || ppc_context->thread_id == TARGET_THREAD)
#define IFLUSH()
#define IPRINT(s)                    \
  if (trace_enabled && THREAD_MATCH) \
  xe::logging::AppendLogLine(xe::LogLevel::Debug, 't', s, xe::LogSrc::Cpu)
#define DFLUSH()
#define DPRINT(...)                  \
  if (trace_enabled && THREAD_MATCH) \
  xe::logging::AppendLogLineFormat(xe::LogSrc::Cpu, xe::LogLevel::Debug, 't', \
                                   __VA_ARGS__)

uint32_t GetTracingMode() {
  uint32_t mode = 0;
#if ITRACE
  mode |= TRACING_INSTR;
#endif  // ITRACE
#if DTRACE
  mode |= TRACING_DATA;
#endif  // DTRACE
  return mode;
}

void TraceString(void* raw_context, const char* str) {
  auto ppc_context = reinterpret_cast<ppc::PPCContext*>(raw_context);
  IPRINT(str);
  IFLUSH();
}

void TraceContextLoadI8(void* raw_context, uint64_t offset, uint8_t value) {
  auto ppc_context = reinterpret_cast<ppc::PPCContext*>(raw_context);
  DPRINT("{} ({:X}) = ctx i8 +{}\n", (int8_t)value, value, offset);
}
void TraceContextLoadI16(void* raw_context, uint64_t offset, uint16_t value) {
  auto ppc_context = reinterpret_cast<ppc::PPCContext*>(raw_context);
  DPRINT("{} ({:X}) = ctx i16 +{}\n", (int16_t)value, value, offset);
}
void TraceContextLoadI32(void* raw_context, uint64_t offset, uint32_t value) {
  auto ppc_context = reinterpret_cast<ppc::PPCContext*>(raw_context);
  DPRINT("{} ({:X}) = ctx i32 +{}\n", (int32_t)value, value, offset);
}
void TraceContextLoadI64(void* raw_context, uint64_t offset, uint64_t value) {
  auto ppc_context = reinterpret_cast<ppc::PPCContext*>(raw_context);
  DPRINT("{} ({:X}) = ctx i64 +{}\n", (int64_t)value, value, offset);
}
void TraceContextLoadF32(void* raw_context, uint64_t offset, __m128 value) {
  auto ppc_context = reinterpret_cast<ppc::PPCContext*>(raw_context);
  DPRINT("{} ({:X}) = ctx f32 +{}\n", xe::m128_f32<0>(value),
         xe::m128_i32<0>(value), offset);
}
void TraceContextLoadF64(void* raw_context, uint64_t offset,
                         const double* value) {
  auto ppc_context = reinterpret_cast<ppc::PPCContext*>(raw_context);
  auto v = _mm_loadu_pd(value);
  DPRINT("{} ({:X}) = ctx f64 +{}\n", xe::m128_f64<0>(v), xe::m128_i64<0>(v),
         offset);
}
void TraceContextLoadV128(void* raw_context, uint64_t offset, __m128 value) {
  auto ppc_context = reinterpret_cast<ppc::PPCContext*>(raw_context);
  DPRINT("[{}, {}, {}, {}] [{:08X}, {:08X}, {:08X}, {:08X}] = ctx v128 +{}\n",
         xe::m128_f32<0>(value), xe::m128_f32<1>(value), xe::m128_f32<2>(value),
         xe::m128_f32<3>(value), xe::m128_i32<0>(value), xe::m128_i32<1>(value),
         xe::m128_i32<2>(value), xe::m128_i32<3>(value), offset);
}

void TraceContextStoreI8(void* raw_context, uint64_t offset, uint8_t value) {
  auto ppc_context = reinterpret_cast<ppc::PPCContext*>(raw_context);
  DPRINT("ctx i8 +{} = {} ({:X})\n", offset, (int8_t)value, value);
}
void TraceContextStoreI16(void* raw_context, uint64_t offset, uint16_t value) {
  auto ppc_context = reinterpret_cast<ppc::PPCContext*>(raw_context);
  DPRINT("ctx i16 +{} = {} ({:X})\n", offset, (int16_t)value, value);
}
void TraceContextStoreI32(void* raw_context, uint64_t offset, uint32_t value) {
  auto ppc_context = reinterpret_cast<ppc::PPCContext*>(raw_context);
  DPRINT("ctx i32 +{} = {} ({:X})\n", offset, (int32_t)value, value);
}
void TraceContextStoreI64(void* raw_context, uint64_t offset, uint64_t value) {
  auto ppc_context = reinterpret_cast<ppc::PPCContext*>(raw_context);
  DPRINT("ctx i64 +{} = {} ({:X})\n", offset, (int64_t)value, value);
}
void TraceContextStoreF32(void* raw_context, uint64_t offset, __m128 value) {
  auto ppc_context = reinterpret_cast<ppc::PPCContext*>(raw_context);
  DPRINT("ctx f32 +{} = {} ({:X})\n", offset, xe::m128_f32<0>(value),
         xe::m128_i32<0>(value));
}
void TraceContextStoreF64(void* raw_context, uint64_t offset,
                          const double* value) {
  auto ppc_context = reinterpret_cast<ppc::PPCContext*>(raw_context);
  auto v = _mm_loadu_pd(value);
  DPRINT("ctx f64 +{} = {} ({:X})\n", offset, xe::m128_f64<0>(v),
         xe::m128_i64<0>(v));
}
void TraceContextStoreV128(void* raw_context, uint64_t offset, __m128 value) {
  auto ppc_context = reinterpret_cast<ppc::PPCContext*>(raw_context);
  DPRINT("ctx v128 +{} = [{}, {}, {}, {}] [{:08X}, {:08X}, {:08X}, {:08X}]\n",
         offset, xe::m128_f32<0>(value), xe::m128_f32<1>(value),
         xe::m128_f32<2>(value), xe::m128_f32<3>(value), xe::m128_i32<0>(value),
         xe::m128_i32<1>(value), xe::m128_i32<2>(value),
         xe::m128_i32<3>(value));
}

void TraceMemoryLoadI8(void* raw_context, uint32_t address, uint8_t value) {
  auto ppc_context = reinterpret_cast<ppc::PPCContext*>(raw_context);
  DPRINT("{} ({:X}) = load.i8 {:08X}\n", (int8_t)value, value, address);
}
void TraceMemoryLoadI16(void* raw_context, uint32_t address, uint16_t value) {
  auto ppc_context = reinterpret_cast<ppc::PPCContext*>(raw_context);
  DPRINT("{} ({:X}) = load.i16 {:08X}\n", (int16_t)value, value, address);
}
void TraceMemoryLoadI32(void* raw_context, uint32_t address, uint32_t value) {
  auto ppc_context = reinterpret_cast<ppc::PPCContext*>(raw_context);
  DPRINT("{} ({:X}) = load.i32 {:08X}\n", (int32_t)value, value, address);
}
void TraceMemoryLoadI64(void* raw_context, uint32_t address, uint64_t value) {
  auto ppc_context = reinterpret_cast<ppc::PPCContext*>(raw_context);
  DPRINT("{} ({:X}) = load.i64 {:08X}\n", (int64_t)value, value, address);
}
void TraceMemoryLoadF32(void* raw_context, uint32_t address, __m128 value) {
  auto ppc_context = reinterpret_cast<ppc::PPCContext*>(raw_context);
  DPRINT("{} ({:X}) = load.f32 {:08X}\n", xe::m128_f32<0>(value),
         xe::m128_i32<0>(value), address);
}
void TraceMemoryLoadF64(void* raw_context, uint32_t address, __m128 value) {
  auto ppc_context = reinterpret_cast<ppc::PPCContext*>(raw_context);
  DPRINT("{} ({:X}) = load.f64 {:08X}\n", xe::m128_f64<0>(value),
         xe::m128_i64<0>(value), address);
}
void TraceMemoryLoadV128(void* raw_context, uint32_t address, __m128 value) {
  auto ppc_context = reinterpret_cast<ppc::PPCContext*>(raw_context);
  DPRINT(
      "[{}, {}, {}, {}] [{:08X}, {:08X}, {:08X}, {:08X}] = load.v128 {:08X}\n",
      xe::m128_f32<0>(value), xe::m128_f32<1>(value), xe::m128_f32<2>(value),
      xe::m128_f32<3>(value), xe::m128_i32<0>(value), xe::m128_i32<1>(value),
      xe::m128_i32<2>(value), xe::m128_i32<3>(value), address);
}

void TraceMemoryStoreI8(void* raw_context, uint32_t address, uint8_t value) {
  auto ppc_context = reinterpret_cast<ppc::PPCContext*>(raw_context);
  DPRINT("store.i8 {:08X} = {} ({:X})\n", address, (int8_t)value, value);
}
void TraceMemoryStoreI16(void* raw_context, uint32_t address, uint16_t value) {
  auto ppc_context = reinterpret_cast<ppc::PPCContext*>(raw_context);
  DPRINT("store.i16 {:08X} = {} ({:X})\n", address, (int16_t)value, value);
}
void TraceMemoryStoreI32(void* raw_context, uint32_t address, uint32_t value) {
  auto ppc_context = reinterpret_cast<ppc::PPCContext*>(raw_context);
  DPRINT("store.i32 {:08X} = {} ({:X})\n", address, (int32_t)value, value);
}
void TraceMemoryStoreI64(void* raw_context, uint32_t address, uint64_t value) {
  auto ppc_context = reinterpret_cast<ppc::PPCContext*>(raw_context);
  DPRINT("store.i64 {:08X} = {} ({:X})\n", address, (int64_t)value, value);
}
void TraceMemoryStoreF32(void* raw_context, uint32_t address, __m128 value) {
  auto ppc_context = reinterpret_cast<ppc::PPCContext*>(raw_context);
  DPRINT("store.f32 {:08X} = {} ({:X})\n", address, xe::m128_f32<0>(value),
         xe::m128_i32<0>(value));
}
void TraceMemoryStoreF64(void* raw_context, uint32_t address, __m128 value) {
  auto ppc_context = reinterpret_cast<ppc::PPCContext*>(raw_context);
  DPRINT("store.f64 {:08X} = {} ({:X})\n", address, xe::m128_f64<0>(value),
         xe::m128_i64<0>(value));
}
void TraceMemoryStoreV128(void* raw_context, uint32_t address, __m128 value) {
  auto ppc_context = reinterpret_cast<ppc::PPCContext*>(raw_context);
  DPRINT(
      "store.v128 {:08X} = [{}, {}, {}, {}] [{:08X}, {:08X}, {:08X}, {:08X}]\n",
      address, xe::m128_f32<0>(value), xe::m128_f32<1>(value),
      xe::m128_f32<2>(value), xe::m128_f32<3>(value), xe::m128_i32<0>(value),
      xe::m128_i32<1>(value), xe::m128_i32<2>(value), xe::m128_i32<3>(value));
}

void TraceMemset(void* raw_context, uint32_t address, uint8_t value,
                 uint32_t length) {
  auto ppc_context = reinterpret_cast<ppc::PPCContext*>(raw_context);
  DPRINT("memset {:08X}-{:08X} ({}) = {:02X}", address, address + length,
         length, value);
}

}  // namespace x64
}  // namespace backend
}  // namespace cpu
}  // namespace xe
