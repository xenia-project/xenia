/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_BACKEND_A64_A64_TRACERS_H_
#define XENIA_CPU_BACKEND_A64_A64_TRACERS_H_

#include <arm_neon.h>
#include <cstdint>

namespace xe {
namespace cpu {
namespace backend {
namespace a64 {
class A64Emitter;

enum TracingMode {
  TRACING_INSTR = (1 << 1),
  TRACING_DATA = (1 << 2),
};

uint32_t GetTracingMode();
inline bool IsTracingInstr() { return (GetTracingMode() & TRACING_INSTR) != 0; }
inline bool IsTracingData() { return (GetTracingMode() & TRACING_DATA) != 0; }

void TraceString(void* raw_context, const char* str);

void TraceContextLoadI8(void* raw_context, uint64_t offset, uint8_t value);
void TraceContextLoadI16(void* raw_context, uint64_t offset, uint16_t value);
void TraceContextLoadI32(void* raw_context, uint64_t offset, uint32_t value);
void TraceContextLoadI64(void* raw_context, uint64_t offset, uint64_t value);
void TraceContextLoadF32(void* raw_context, uint64_t offset, float32x4_t value);
void TraceContextLoadF64(void* raw_context, uint64_t offset,
                         const double* value);
void TraceContextLoadV128(void* raw_context, uint64_t offset,
                          float32x4_t value);

void TraceContextStoreI8(void* raw_context, uint64_t offset, uint8_t value);
void TraceContextStoreI16(void* raw_context, uint64_t offset, uint16_t value);
void TraceContextStoreI32(void* raw_context, uint64_t offset, uint32_t value);
void TraceContextStoreI64(void* raw_context, uint64_t offset, uint64_t value);
void TraceContextStoreF32(void* raw_context, uint64_t offset,
                          float32x4_t value);
void TraceContextStoreF64(void* raw_context, uint64_t offset,
                          const double* value);
void TraceContextStoreV128(void* raw_context, uint64_t offset,
                           float32x4_t value);

void TraceMemoryLoadI8(void* raw_context, uint32_t address, uint8_t value);
void TraceMemoryLoadI16(void* raw_context, uint32_t address, uint16_t value);
void TraceMemoryLoadI32(void* raw_context, uint32_t address, uint32_t value);
void TraceMemoryLoadI64(void* raw_context, uint32_t address, uint64_t value);
void TraceMemoryLoadF32(void* raw_context, uint32_t address, float32x4_t value);
void TraceMemoryLoadF64(void* raw_context, uint32_t address, float64x2_t value);
void TraceMemoryLoadV128(void* raw_context, uint32_t address,
                         float32x4_t value);

void TraceMemoryStoreI8(void* raw_context, uint32_t address, uint8_t value);
void TraceMemoryStoreI16(void* raw_context, uint32_t address, uint16_t value);
void TraceMemoryStoreI32(void* raw_context, uint32_t address, uint32_t value);
void TraceMemoryStoreI64(void* raw_context, uint32_t address, uint64_t value);
void TraceMemoryStoreF32(void* raw_context, uint32_t address,
                         float32x4_t value);
void TraceMemoryStoreF64(void* raw_context, uint32_t address,
                         float64x2_t value);
void TraceMemoryStoreV128(void* raw_context, uint32_t address,
                          float32x4_t value);

void TraceMemset(void* raw_context, uint32_t address, uint8_t value,
                 uint32_t length);

}  // namespace a64
}  // namespace backend
}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_BACKEND_A64_A64_TRACERS_H_
