/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_PACKET_DISASSEMBLER_H_
#define XENIA_GPU_PACKET_DISASSEMBLER_H_

#include <vector>

#include "xenia/gpu/register_file.h"
#include "xenia/gpu/trace_protocol.h"
#include "xenia/gpu/trace_reader.h"
#include "xenia/memory.h"

namespace xe {
namespace gpu {

enum class PacketCategory {
  kGeneric,
  kDraw,
  kSwap,
};

struct PacketTypeInfo {
  PacketCategory category;
  const char* name;
};

struct PacketAction {
  enum class Type {
    kRegisterWrite,
    kSetBinMask,
    kSetBinSelect,
    kSetBinMaskLo,
    kSetBinMaskHi,
    kSetBinSelectLo,
    kSetBinSelectHi,
    kMeInit,
    kGenInterrupt,
    kWaitRegMem,
    kRegRmw,
    kCondWrite,
    kEventWrite,
    kEventWriteSHD,
    kEventWriteExt,
    kDrawIndx,
    kDrawIndx2,
    kInvalidateState,
    kImLoad,
    kImLoadImmediate,
    kContextUpdate,
    kWaitForIdle,
    kVizQuery,
    kEventWriteZPD,
    kMemWrite,
    kRegToMem,
    kIndirBuffer,
    kXeSwap
  };
  Type type;

  union {
    struct {
      uint32_t index;
      RegisterFile::RegisterValue value;
    } register_write;
    struct {
      uint64_t value;
    } set_bin_mask;
    struct {
      uint64_t value;
    } set_bin_select;
    struct {
      uint32_t cpu_mask;
    } gen_interrupt;

    struct {
      uint32_t wait_info;
      uint32_t poll_reg_addr;
      uint32_t ref;
      uint32_t mask;
      uint32_t wait;
    } wait_reg_mem;
    struct {
      uint32_t rmw_info, and_mask, or_mask;
    } reg_rmw;

    struct {
      uint32_t wait_info;
      uint32_t poll_reg_addr;
      uint32_t ref;
      uint32_t mask;
      uint32_t write_reg_addr;
      uint32_t write_data;
    } cond_write;

    struct {
      uint32_t initiator;
    } event_write;

    struct {
      uint32_t initiator, address, value;
    } event_write_shd;

    struct {
      uint32_t unk0, unk1;
    } event_write_ext;

    struct {
      uint32_t initiator;
    } event_write_zpd;
    struct {
      uint32_t dword0, dword1, index_count;
      xenos::PrimitiveType prim_type;
      uint32_t src_sel;
      uint32_t guest_base;
      uint32_t index_size;
      xenos::Endian endianness;

    } draw_indx;

    struct {
      uint32_t dword0;
      uint32_t index_count;
      xenos::PrimitiveType prim_type;
      uint32_t src_sel;
      uint32_t indices_size;

    } draw_indx2;

    struct {
      uint32_t state_mask;
    } invalidate_state;

    struct {
      xenos::ShaderType shader_type;
      uint32_t addr;
      uint32_t start;
      uint32_t size_dwords;
    } im_load;
    struct {
      xenos::ShaderType shader_type;
      uint32_t start;
      uint32_t size_dwords;
    } im_load_imm;

    // for bin select hi/lo binmask hi/lo
    struct {
      uint32_t value;
    } lohi_op;
    struct {
      // i think waitforidle is just being pad to a qword
      uint32_t probably_unused;
    } wait_for_idle;

    struct {
      uint32_t maybe_unused;
    } context_update;

    struct {
      uint32_t dword0;

      uint32_t id;
      bool end;
    } vizquery;

    struct {
      uint32_t addr;
      xenos::Endian endianness;
    } mem_write;

    struct {
      uint32_t reg_addr;
      uint32_t mem_addr;
      xenos::Endian endianness;
    } reg2mem;
    struct {
      uint32_t frontbuffer_ptr;
    } xe_swap;
    struct {
      uint32_t list_ptr;
      uint32_t list_length;
    } indir_buffer;
  };

  std::vector<uint32_t> words;

  static PacketAction RegisterWrite(uint32_t index, uint32_t value) {
    PacketAction action;
    action.type = Type::kRegisterWrite;
    action.register_write.index = index;
    action.register_write.value.u32 = value;
    return action;
  }

  static PacketAction SetBinMask(uint64_t value) {
    PacketAction action;
    action.type = Type::kSetBinMask;
    action.set_bin_mask.value = value;
    return action;
  }

  static PacketAction SetBinSelect(uint64_t value) {
    PacketAction action;
    action.type = Type::kSetBinSelect;
    action.set_bin_select.value = value;
    return action;
  }

  void InjectBeWords(const uint32_t* values, uint32_t count) {
    for (unsigned i = 0; i < count; ++i) {
      words.push_back(xe::load_and_swap<uint32_t>(&values[i]));
    }
  }
  void InjectBeHalfwordsAsWords(const uint16_t* values, uint32_t count) {
    for (unsigned i = 0; i < count; ++i) {
      words.push_back(
          static_cast<uint32_t>(xe::load_and_swap<uint16_t>(&values[i])));
    }
  }

  static PacketAction MeInit(const uint32_t* values, uint32_t count) {
    PacketAction action;
    action.type = Type::kMeInit;
    action.InjectBeWords(values, count);
    return action;
  }

  static PacketAction Interrupt(uint32_t cpu_mask) {
    PacketAction action;
    action.type = Type::kGenInterrupt;
    action.gen_interrupt.cpu_mask = cpu_mask;
    return action;
  }
};

struct PacketInfo {
  const PacketTypeInfo* type_info;
  bool predicated;
  uint32_t count;
  std::vector<PacketAction> actions;
};

class PacketDisassembler {
 public:
  static PacketCategory GetPacketCategory(const uint8_t* base_ptr);

  static bool DisasmPacketType0(const uint8_t* base_ptr, uint32_t packet,
                                PacketInfo* out_info);
  static bool DisasmPacketType1(const uint8_t* base_ptr, uint32_t packet,
                                PacketInfo* out_info);
  static bool DisasmPacketType2(const uint8_t* base_ptr, uint32_t packet,
                                PacketInfo* out_info);
  static bool DisasmPacketType3(const uint8_t* base_ptr, uint32_t packet,
                                PacketInfo* out_info);
  static bool DisasmPacket(const uint8_t* base_ptr, PacketInfo* out_info);
};

}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_PACKET_DISASSEMBLER_H_
