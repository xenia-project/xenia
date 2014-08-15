/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XDB_PROTOCOL_H_
#define XDB_PROTOCOL_H_

#include <cstdint>

#include <alloy/vec128.h>
#include <poly/atomic.h>

namespace xdb {
namespace protocol {

using vec128_t = alloy::vec128_t;

#pragma pack(push, 4)

enum class EventType : uint8_t {
  END_OF_STREAM = 0,

  PROCESS_START,
  PROCESS_EXIT,
  MODULE_LOAD,
  MODULE_UNLOAD,
  THREAD_CREATE,
  THREAD_INFO,
  THREAD_EXIT,

  FUNCTION_COMPILED,

  OUTPUT_STRING,

  KERNEL_CALL,
  KERNEL_CALL_RETURN,
  USER_CALL,
  USER_CALL_RETURN,

  INSTR,
  INSTR_R8,
  INSTR_R8_R8,
  INSTR_R8_R16,
  INSTR_R16,
  INSTR_R16_R8,
  INSTR_R16_R16,
};

template <typename T>
struct Event {
  static T* Append(uint64_t trace_base) {
    if (!trace_base) {
      return nullptr;
    }
    uint64_t ptr = poly::atomic_exchange_add(
        sizeof(T), reinterpret_cast<uint64_t*>(trace_base));
    return reinterpret_cast<T*>(ptr);
  }

  static const T* Get(const void* ptr) {
    return reinterpret_cast<const T*>(ptr);
  }
};

struct ProcessStartEvent : public Event<ProcessStartEvent> {
  EventType type;
  uint64_t membase;
  char launch_path[256];
};

struct ProcessExitEvent : public Event<ProcessExitEvent> {
  EventType type;
};

struct ModuleInfo {
  uint32_t module_id;
};

struct ModuleLoadEvent : public Event<ModuleLoadEvent> {
  EventType type;
  uint32_t module_id;
  ModuleInfo module_info;
};

struct ModuleUnloadEvent : public Event<ModuleUnloadEvent> {
  EventType type;
  uint32_t module_id;
};

struct Registers {
  uint64_t lr;
  uint64_t ctr;
  uint32_t xer;
  uint32_t cr[8];
  uint32_t fpscr;
  uint32_t vscr;
  uint64_t gpr[32];
  double fpr[32];
  alloy::vec128_t vr[128];
};

struct ThreadCreateEvent : public Event<ThreadCreateEvent> {
  EventType type;
  uint16_t thread_id;
  Registers registers;
};

struct ThreadInfoEvent : public Event<ThreadInfoEvent> {
  EventType type;
  uint16_t thread_id;
};

struct ThreadExitEvent : public Event<ThreadExitEvent> {
  EventType type;
  uint16_t thread_id;
};

struct FunctionCompiledEvent : public Event<FunctionCompiledEvent> {
  EventType type;
  uint8_t _reserved;
  uint16_t flags;  // RECOMPILED? user/extern? etc
  uint32_t address;
  uint32_t length;
  // timing?
};

struct OutputStringEvent : public Event<OutputStringEvent> {
  EventType type;
  uint16_t thread_id;
  // ?
};

struct KernelCallEvent : public Event<KernelCallEvent> {
  EventType type;
  uint8_t _reserved;
  uint16_t thread_id;
  uint16_t module_id;
  uint16_t ordinal;
};
struct KernelCallReturnEvent : public Event<KernelCallReturnEvent> {
  EventType type;
  uint8_t _reserved;
  uint16_t thread_id;
};

struct UserCallEvent : public Event<UserCallEvent> {
  EventType type;
  uint8_t call_type;  // TAIL?
  uint16_t thread_id;
  uint32_t address;
};
struct UserCallReturnEvent : public Event<UserCallReturnEvent> {
  EventType type;
  uint8_t _reserved;
  uint16_t thread_id;
};

struct InstrEvent : public Event<InstrEvent> {
  EventType type;
  uint8_t _reserved;
  uint16_t thread_id;
  uint32_t address;
};
struct InstrEventR8 : public Event<InstrEventR8> {
  EventType type;
  uint8_t dest_reg;
  uint16_t thread_id;
  uint32_t address;
  uint64_t dest_value;
};
struct InstrEventR8R8 : public Event<InstrEventR8R8> {
  EventType type;
  uint8_t _reserved0;
  uint16_t thread_id;
  uint32_t address;
  uint8_t dest_reg_0;
  uint8_t dest_reg_1;
  uint16_t _reserved1;
  uint64_t dest_value_0;
  uint64_t dest_value_1;
};
struct InstrEventR8R16 : public Event<InstrEventR8R16> {
  EventType type;
  uint8_t _reserved0;
  uint16_t thread_id;
  uint32_t address;
  uint8_t dest_reg_0;
  uint8_t dest_reg_1;
  uint16_t _reserved1;
  uint64_t dest_value_0;
  vec128_t dest_value_1;
};
struct InstrEventR16 : public Event<InstrEventR16> {
  EventType type;
  uint8_t dest_reg;
  uint16_t thread_id;
  uint32_t address;
  vec128_t dest_value;
};
struct InstrEventR16R8 : public Event<InstrEventR16R8> {
  EventType type;
  uint8_t _reserved0;
  uint16_t thread_id;
  uint32_t address;
  uint8_t dest_reg_0;
  uint8_t dest_reg_1;
  uint16_t _reserved1;
  vec128_t dest_value_0;
  uint64_t dest_value_1;
};
struct InstrEventR16R16 : public Event<InstrEventR16R16> {
  EventType type;
  uint8_t _reserved0;
  uint16_t thread_id;
  uint32_t address;
  uint8_t dest_reg_0;
  uint8_t dest_reg_1;
  uint16_t _reserved1;
  vec128_t dest_value_0;
  vec128_t dest_value_1;
};

#pragma pack(pop)

}  // namespace protocol
}  // namespace xdb

#endif  // XDB_PROTOCOL_H_
