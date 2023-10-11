/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/packet_disassembler.h"

#include "xenia/gpu/xenos.h"

namespace xe {
namespace gpu {

using namespace xe::gpu::xenos;

PacketCategory PacketDisassembler::GetPacketCategory(const uint8_t* base_ptr) {
  const uint32_t packet = xe::load_and_swap<uint32_t>(base_ptr);
  const uint32_t packet_type = packet >> 30;
  switch (packet_type) {
    case 0x00:
    case 0x01:
    case 0x02: {
      return PacketCategory::kGeneric;
    }
    case 0x03: {
      uint32_t opcode = (packet >> 8) & 0x7F;
      switch (opcode) {
        case PM4_DRAW_INDX:
        case PM4_DRAW_INDX_2:
          return PacketCategory::kDraw;
        case PM4_XE_SWAP:
          return PacketCategory::kSwap;
        default:
          return PacketCategory::kGeneric;
      }
    }
    default: {
      assert_unhandled_case(packet_type);
      return PacketCategory::kGeneric;
    }
  }
}

bool PacketDisassembler::DisasmPacketType0(const uint8_t* base_ptr,
                                           uint32_t packet,
                                           PacketInfo* out_info) {
  static const PacketTypeInfo type_0_info = {PacketCategory::kGeneric,
                                             "PM4_TYPE0"};
  out_info->type_info = &type_0_info;

  uint32_t count = ((packet >> 16) & 0x3FFF) + 1;
  out_info->count = 1 + count;
  auto ptr = base_ptr + 4;

  uint32_t base_index = (packet & 0x7FFF);
  uint32_t write_one_reg = (packet >> 15) & 0x1;
  out_info->actions.reserve(count);
  for (uint32_t m = 0; m < count; m++) {
    uint32_t reg_data = xe::load_and_swap<uint32_t>(ptr);
    uint32_t target_index = write_one_reg ? base_index : base_index + m;
    out_info->actions.emplace_back(
        PacketAction::RegisterWrite(target_index, reg_data));
    ptr += 4;
  }

  return true;
}

bool PacketDisassembler::DisasmPacketType1(const uint8_t* base_ptr,
                                           uint32_t packet,
                                           PacketInfo* out_info) {
  static const PacketTypeInfo type_1_info = {PacketCategory::kGeneric,
                                             "PM4_TYPE1"};
  out_info->type_info = &type_1_info;

  out_info->count = 1 + 2;
  auto ptr = base_ptr + 4;
  out_info->actions.reserve(2);
  uint32_t reg_index_1 = packet & 0x7FF;
  uint32_t reg_index_2 = (packet >> 11) & 0x7FF;
  uint32_t reg_data_1 = xe::load_and_swap<uint32_t>(ptr);
  uint32_t reg_data_2 = xe::load_and_swap<uint32_t>(ptr + 4);
  out_info->actions.emplace_back(
      PacketAction::RegisterWrite(reg_index_1, reg_data_1));
  out_info->actions.emplace_back(
      PacketAction::RegisterWrite(reg_index_2, reg_data_2));

  return true;
}

bool PacketDisassembler::DisasmPacketType2(const uint8_t* base_ptr,
                                           uint32_t packet,
                                           PacketInfo* out_info) {
  static const PacketTypeInfo type_2_info = {PacketCategory::kGeneric,
                                             "PM4_TYPE2"};
  out_info->type_info = &type_2_info;

  out_info->count = 1;

  return true;
}

bool PacketDisassembler::DisasmPacketType3(const uint8_t* base_ptr,
                                           uint32_t packet,
                                           PacketInfo* out_info) {
  static const PacketTypeInfo type_3_unknown_info = {PacketCategory::kGeneric,
                                                     "PM4_TYPE3_UNKNOWN"};
  out_info->type_info = &type_3_unknown_info;

  uint32_t opcode = (packet >> 8) & 0x7F;
  uint32_t count = ((packet >> 16) & 0x3FFF) + 1;
  out_info->count = 1 + count;
  auto ptr = base_ptr + 4;

  if (packet & 1) {
    out_info->predicated = true;
  }

  using Type = PacketAction::Type;
  bool result = true;

  auto& out_actions = out_info->actions;
  out_actions.reserve(1);
  switch (opcode) {
    case PM4_ME_INIT: {
      // initialize CP's micro-engine
      static const PacketTypeInfo op_info = {PacketCategory::kGeneric,
                                             "PM4_ME_INIT"};

      out_actions.emplace_back(PacketAction::MeInit((uint32_t*)ptr, count));

      out_info->type_info = &op_info;
      break;
    }
    case PM4_NOP: {
      // skip N 32-bit words to get to the next packet
      // No-op, ignore some data.
      static const PacketTypeInfo op_info = {PacketCategory::kGeneric,
                                             "PM4_NOP"};
      out_info->type_info = &op_info;
      break;
    }
    case PM4_INTERRUPT: {
      // generate interrupt from the command stream
      static const PacketTypeInfo op_info = {PacketCategory::kGeneric,
                                             "PM4_INTERRUPT"};
      out_info->type_info = &op_info;

      PacketAction intaction;
      intaction.type = Type::kGenInterrupt;
      intaction.gen_interrupt.cpu_mask = xe::load_and_swap<uint32_t>(ptr + 0);
      out_actions.emplace_back(std::move(intaction));
      break;
    }
    case PM4_XE_SWAP: {
      // Xenia-specific VdSwap hook.
      // VdSwap will post this to tell us we need to swap the screen/fire an
      // interrupt.
      // 63 words here, but only the first has any data.
      static const PacketTypeInfo op_info = {PacketCategory::kSwap,
                                             "PM4_XE_SWAP"};
      out_info->type_info = &op_info;

      PacketAction xsa;
      xsa.type = Type::kXeSwap;
      xsa.xe_swap.frontbuffer_ptr = xe::load_and_swap<uint32_t>(ptr + 0);

      out_actions.emplace_back(std::move(xsa));

      break;
    }
    case PM4_INDIRECT_BUFFER:
    case PM4_INDIRECT_BUFFER_PFD: {
      // indirect buffer dispatch
      static const PacketTypeInfo op_info = {PacketCategory::kGeneric,
                                             "PM4_INDIRECT_BUFFER"};
      out_info->type_info = &op_info;

      PacketAction iba;
      iba.type = Type::kIndirBuffer;
      iba.indir_buffer.list_ptr = xe::load_and_swap<uint32_t>(ptr);
      iba.indir_buffer.list_length = xe::load_and_swap<uint32_t>(ptr + 4);
      out_actions.emplace_back(std::move(iba));

      break;
    }
    case PM4_WAIT_REG_MEM: {
      // wait until a register or memory location is a specific value
      static const PacketTypeInfo op_info = {PacketCategory::kGeneric,
                                             "PM4_WAIT_REG_MEM"};
      out_info->type_info = &op_info;

      PacketAction wait_action;
      wait_action.type = PacketAction::Type::kWaitRegMem;
      auto& wrm = wait_action.wait_reg_mem;
      wrm.wait_info = xe::load_and_swap<uint32_t>(ptr + 0);
      wrm.poll_reg_addr = xe::load_and_swap<uint32_t>(ptr + 4);
      wrm.ref = xe::load_and_swap<uint32_t>(ptr + 8);
      wrm.mask = xe::load_and_swap<uint32_t>(ptr + 12);
      wrm.wait = xe::load_and_swap<uint32_t>(ptr + 16);

      out_actions.emplace_back(std::move(wait_action));

      break;
    }
    case PM4_REG_RMW: {
      // register read/modify/write
      // ? (used during shader upload and edram setup)
      static const PacketTypeInfo op_info = {PacketCategory::kGeneric,
                                             "PM4_REG_RMW"};
      out_info->type_info = &op_info;
      PacketAction rmw_action;
      rmw_action.type = PacketAction::Type::kRegRmw;
      auto& rmw = rmw_action.reg_rmw;

      uint32_t rmw_info = xe::load_and_swap<uint32_t>(ptr + 0);

      rmw.rmw_info = rmw_info;

      rmw.and_mask = xe::load_and_swap<uint32_t>(ptr + 4);
      rmw.or_mask = xe::load_and_swap<uint32_t>(ptr + 8);
      out_actions.emplace_back(std::move(rmw_action));
      break;
    }
    case PM4_COND_WRITE: {
      // conditional write to memory or register
      static const PacketTypeInfo op_info = {PacketCategory::kGeneric,
                                             "PM4_COND_WRITE"};
      out_info->type_info = &op_info;
      PacketAction cwr_action;
      cwr_action.type = PacketAction::Type::kCondWrite;

      auto& cwr = cwr_action.cond_write;

      cwr.wait_info = xe::load_and_swap<uint32_t>(ptr + 0);
      cwr.poll_reg_addr = xe::load_and_swap<uint32_t>(ptr + 4);
      cwr.ref = xe::load_and_swap<uint32_t>(ptr + 8);
      cwr.mask = xe::load_and_swap<uint32_t>(ptr + 12);
      cwr.write_reg_addr = xe::load_and_swap<uint32_t>(ptr + 16);
      cwr.write_data = xe::load_and_swap<uint32_t>(ptr + 20);

      out_actions.emplace_back(std::move(cwr_action));
      break;
    }
    case PM4_EVENT_WRITE: {
      // generate an event that creates a write to memory when completed
      static const PacketTypeInfo op_info = {PacketCategory::kGeneric,
                                             "PM4_EVENT_WRITE"};
      out_info->type_info = &op_info;
      PacketAction evw_action;
      evw_action.type = Type::kEventWrite;
      evw_action.event_write.initiator = xe::load_and_swap<uint32_t>(ptr + 0);

      out_actions.emplace_back(std::move(evw_action));

      break;
    }
    case PM4_EVENT_WRITE_SHD: {
      // generate a VS|PS_done event
      static const PacketTypeInfo op_info = {PacketCategory::kGeneric,
                                             "PM4_EVENT_WRITE_SHD"};
      out_info->type_info = &op_info;
      PacketAction evws_action;
      evws_action.type = Type::kEventWriteSHD;
      auto& evws = evws_action.event_write_shd;

      evws.initiator = xe::load_and_swap<uint32_t>(ptr + 0);
      evws.address = xe::load_and_swap<uint32_t>(ptr + 4);
      evws.value = xe::load_and_swap<uint32_t>(ptr + 8);

      out_actions.emplace_back(std::move(evws_action));
      break;
    }
    case PM4_EVENT_WRITE_EXT: {
      // generate a screen extent event
      static const PacketTypeInfo op_info = {PacketCategory::kGeneric,
                                             "PM4_EVENT_WRITE_EXT"};
      out_info->type_info = &op_info;

      PacketAction eve_action;
      eve_action.type = Type::kEventWriteExt;
      auto& eve = eve_action.event_write_ext;
      uint32_t unk0 = xe::load_and_swap<uint32_t>(ptr + 0);
      uint32_t unk1 = xe::load_and_swap<uint32_t>(ptr + 4);

      eve.unk0 = unk0;
      eve.unk1 = unk1;
      out_actions.emplace_back(std::move(eve_action));

      break;
    }
    case PM4_DRAW_INDX: {
      // initiate fetch of index buffer and draw
      // dword0 = viz query info
      static const PacketTypeInfo op_info = {PacketCategory::kDraw,
                                             "PM4_DRAW_INDX"};
      out_info->type_info = &op_info;
      uint32_t dword0 = xe::load_and_swap<uint32_t>(ptr + 0);
      uint32_t dword1 = xe::load_and_swap<uint32_t>(ptr + 4);
      uint32_t index_count = dword1 >> 16;
      auto prim_type = static_cast<xenos::PrimitiveType>(dword1 & 0x3F);
      uint32_t src_sel = (dword1 >> 6) & 0x3;

      PacketAction di_action;
      di_action.type = Type::kDrawIndx;

      auto& di = di_action.draw_indx;
      di.dword0 = dword0;
      di.dword1 = dword1;
      di.index_count = index_count;
      di.prim_type = prim_type;
      di.src_sel = src_sel;
      if (src_sel == 0x0) {
        // Indexed draw.
        uint32_t guest_base = xe::load_and_swap<uint32_t>(ptr + 8);
        uint32_t index_size = xe::load_and_swap<uint32_t>(ptr + 12);
        auto endianness = static_cast<xenos::Endian>(index_size >> 30);
        index_size &= 0x00FFFFFF;
        bool index_32bit = (dword1 >> 11) & 0x1;
        index_size *= index_32bit ? 4 : 2;
        di.index_size = index_size;
        di.guest_base = guest_base;
        di.endianness = endianness;

      } else if (src_sel == 0x2) {
        // Auto draw.
      } else {
        // Unknown source select.
        assert_always();
      }
      out_actions.emplace_back(std::move(di_action));

      break;
    }
    case PM4_DRAW_INDX_2: {
      // draw using supplied indices in packet
      static const PacketTypeInfo op_info = {PacketCategory::kDraw,
                                             "PM4_DRAW_INDX_2"};
      out_info->type_info = &op_info;
      uint32_t dword0 = xe::load_and_swap<uint32_t>(ptr + 0);
      uint32_t index_count = dword0 >> 16;
      auto prim_type = static_cast<xenos::PrimitiveType>(dword0 & 0x3F);
      uint32_t src_sel = (dword0 >> 6) & 0x3;
      assert_true(src_sel == 0x2);  // 'SrcSel=AutoIndex'
      bool index_32bit = (dword0 >> 11) & 0x1;
      uint32_t indices_size = index_count * (index_32bit ? 4 : 2);
      auto index_ptr = ptr + 4;

      PacketAction di2_action;
      di2_action.type = Type::kDrawIndx2;
      di2_action.words.reserve(index_count);
      auto& di2 = di2_action.draw_indx2;

      di2.dword0 = dword0;
      di2.index_count = index_count;
      di2.indices_size = indices_size;
      di2.prim_type = prim_type;
      di2.src_sel = src_sel;

      if (index_32bit) {
        di2_action.InjectBeWords((uint32_t*)index_ptr, index_count);
      } else {
        di2_action.InjectBeHalfwordsAsWords((uint16_t*)index_ptr, index_count);
      }
      out_actions.emplace_back(std::move(di2_action));
      break;
    }
    case PM4_SET_CONSTANT: {
      // load constant into chip and to memory
      // PM4_REG(reg) ((0x4 << 16) | (GSL_HAL_SUBBLOCK_OFFSET(reg)))
      //                                     reg - 0x2000
      static const PacketTypeInfo op_info = {PacketCategory::kGeneric,
                                             "PM4_SET_CONSTANT"};
      out_info->type_info = &op_info;
      uint32_t offset_type = xe::load_and_swap<uint32_t>(ptr + 0);
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
          result = false;
          break;
      }
      out_actions.reserve(count - 1);
      for (uint32_t n = 0; n < count - 1; n++, index++) {
        uint32_t data = xe::load_and_swap<uint32_t>(ptr + 4 + n * 4);
        out_actions.emplace_back(PacketAction::RegisterWrite(index, data));
      }
      break;
    }
    case PM4_SET_CONSTANT2: {
      static const PacketTypeInfo op_info = {PacketCategory::kGeneric,
                                             "PM4_SET_CONSTANT2"};
      out_info->type_info = &op_info;
      uint32_t offset_type = xe::load_and_swap<uint32_t>(ptr + 0);
      uint32_t index = offset_type & 0xFFFF;

      out_actions.reserve(count - 1);

      for (uint32_t n = 0; n < count - 1; n++, index++) {
        uint32_t data = xe::load_and_swap<uint32_t>(ptr + 4 + n * 4);
        out_actions.emplace_back(PacketAction::RegisterWrite(index, data));
      }
      return true;
      break;
    }
    case PM4_LOAD_ALU_CONSTANT: {
      // load constants from memory
      static const PacketTypeInfo op_info = {PacketCategory::kGeneric,
                                             "PM4_LOAD_ALU_CONSTANT"};
      out_info->type_info = &op_info;
      uint32_t address = xe::load_and_swap<uint32_t>(ptr + 0);
      address &= 0x3FFFFFFF;
      uint32_t offset_type = xe::load_and_swap<uint32_t>(ptr + 4);
      uint32_t index = offset_type & 0x7FF;
      uint32_t size_dwords = xe::load_and_swap<uint32_t>(ptr + 8);
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
      out_actions.reserve(size_dwords);

      for (uint32_t n = 0; n < size_dwords; n++, index++) {
        // Hrm, ?
        // xe::load_and_swap<uint32_t>(membase_ + GpuToCpu(address + n * 4));
        uint32_t data = 0xDEADBEEF;
        out_actions.emplace_back(PacketAction::RegisterWrite(index, data));
      }
      break;
    }
    case PM4_SET_SHADER_CONSTANTS: {
      static const PacketTypeInfo op_info = {PacketCategory::kGeneric,
                                             "PM4_SET_SHADER_CONSTANTS"};
      out_info->type_info = &op_info;
      uint32_t offset_type = xe::load_and_swap<uint32_t>(ptr + 0);
      uint32_t index = offset_type & 0xFFFF;

      out_actions.reserve(count - 1);

      for (uint32_t n = 0; n < count - 1; n++, index++) {
        uint32_t data = xe::load_and_swap<uint32_t>(ptr + 4 + n * 4);
        out_actions.emplace_back(PacketAction::RegisterWrite(index, data));
      }
      return true;
    }
    case PM4_IM_LOAD: {
      // load sequencer instruction memory (pointer-based)
      static const PacketTypeInfo op_info = {PacketCategory::kGeneric,
                                             "PM4_IM_LOAD"};
      out_info->type_info = &op_info;
      uint32_t addr_type = xe::load_and_swap<uint32_t>(ptr + 0);
      auto shader_type = static_cast<xenos::ShaderType>(addr_type & 0x3);
      uint32_t addr = addr_type & ~0x3;
      uint32_t start_size = xe::load_and_swap<uint32_t>(ptr + 4);
      uint32_t start = start_size >> 16;
      uint32_t size_dwords = start_size & 0xFFFF;  // dwords

      PacketAction iml_action;
      iml_action.type = Type::kImLoad;
      auto& iml = iml_action.im_load;
      iml.addr = addr;
      iml.shader_type = shader_type;
      iml.size_dwords = size_dwords;
      iml.start = start;

      out_actions.emplace_back(std::move(iml_action));

      assert_true(start == 0);
      break;
    }
    case PM4_IM_LOAD_IMMEDIATE: {
      // load sequencer instruction memory (code embedded in packet)
      static const PacketTypeInfo op_info = {PacketCategory::kGeneric,
                                             "PM4_IM_LOAD_IMMEDIATE"};
      out_info->type_info = &op_info;
      uint32_t dword0 = xe::load_and_swap<uint32_t>(ptr + 0);
      uint32_t dword1 = xe::load_and_swap<uint32_t>(ptr + 4);
      auto shader_type = static_cast<xenos::ShaderType>(dword0);
      uint32_t start_size = dword1;
      uint32_t start = start_size >> 16;
      uint32_t size_dwords = start_size & 0xFFFF;  // dwords

      PacketAction imi_action;
      imi_action.type = Type::kImLoadImmediate;
      imi_action.words.reserve(size_dwords);
      auto& imi = imi_action.im_load_imm;
      imi.shader_type = shader_type;
      imi.size_dwords = size_dwords;
      imi.start = start;

      imi_action.InjectBeWords(reinterpret_cast<const uint32_t*>(ptr + 8),
                               size_dwords);

      out_actions.emplace_back(std::move(imi_action));

      assert_true(start == 0);
      break;
    }
    case PM4_INVALIDATE_STATE: {
      // selective invalidation of state pointers
      static const PacketTypeInfo op_info = {PacketCategory::kGeneric,
                                             "PM4_INVALIDATE_STATE"};
      out_info->type_info = &op_info;
      uint32_t mask = xe::load_and_swap<uint32_t>(ptr + 0);

      PacketAction inv_action;
      inv_action.type = Type::kInvalidateState;
      inv_action.invalidate_state.state_mask = mask;

      out_actions.emplace_back(std::move(inv_action));
      break;
    }
    case PM4_SET_BIN_MASK_LO: {
      static const PacketTypeInfo op_info = {PacketCategory::kGeneric,
                                             "PM4_SET_BIN_MASK_LO"};
      out_info->type_info = &op_info;
      uint32_t value = xe::load_and_swap<uint32_t>(ptr);
      // bin_mask_ = (bin_mask_ & 0xFFFFFFFF00000000ull) | value;

      PacketAction action;
      action.type = Type::kSetBinMaskLo;
      action.lohi_op.value = value;
      out_actions.emplace_back(std::move(action));

      break;
    }
    case PM4_SET_BIN_MASK_HI: {
      static const PacketTypeInfo op_info = {PacketCategory::kGeneric,
                                             "PM4_SET_BIN_MASK_HI"};
      out_info->type_info = &op_info;
      uint32_t value = xe::load_and_swap<uint32_t>(ptr);

      PacketAction action;
      action.type = Type::kSetBinMaskHi;
      action.lohi_op.value = value;
      out_actions.emplace_back(std::move(action));
      break;
    }
    case PM4_SET_BIN_SELECT_LO: {
      static const PacketTypeInfo op_info = {PacketCategory::kGeneric,
                                             "PM4_SET_BIN_SELECT_LO"};
      out_info->type_info = &op_info;
      uint32_t value = xe::load_and_swap<uint32_t>(ptr);
      PacketAction action;
      action.type = Type::kSetBinSelectLo;
      action.lohi_op.value = value;
      out_actions.emplace_back(std::move(action));

      break;
    }
    case PM4_SET_BIN_SELECT_HI: {
      static const PacketTypeInfo op_info = {PacketCategory::kGeneric,
                                             "PM4_SET_BIN_SELECT_HI"};
      out_info->type_info = &op_info;
      uint32_t value = xe::load_and_swap<uint32_t>(ptr);

      PacketAction action;
      action.type = Type::kSetBinSelectHi;
      action.lohi_op.value = value;
      out_actions.emplace_back(std::move(action));
      break;
    }
    case PM4_CONTEXT_UPDATE: {
      uint32_t value = xe::load_and_swap<uint32_t>(ptr);
      PacketAction ctxu;
      ctxu.type = Type::kContextUpdate;
      ctxu.context_update.maybe_unused = value;
      out_actions.emplace_back(std::move(ctxu));
      break;
    }
    case PM4_WAIT_FOR_IDLE: {
      uint32_t value = xe::load_and_swap<uint32_t>(ptr);
      PacketAction wfi;
      wfi.type = Type::kWaitForIdle;
      wfi.wait_for_idle.probably_unused = value;
      out_actions.emplace_back(std::move(wfi));
      break;
    }

    case PM4_VIZ_QUERY: {
      uint32_t value = xe::load_and_swap<uint32_t>(ptr);

      PacketAction vzq;
      vzq.type = Type::kVizQuery;
      vzq.vizquery.dword0 = value;
      vzq.vizquery.id = value & 0x3F;
      vzq.vizquery.end = !!(value & 0x100);
      out_actions.emplace_back(std::move(vzq));
      break;
    }
    case PM4_EVENT_WRITE_ZPD: {
      uint32_t value = xe::load_and_swap<uint32_t>(ptr);

      PacketAction evz;
      evz.type = Type::kEventWriteZPD;
      evz.event_write_zpd.initiator = value;
      out_actions.emplace_back(std::move(evz));
      break;
    }
    case PM4_MEM_WRITE: {
      PacketAction mwr;
      mwr.type = Type::kMemWrite;
      mwr.words.reserve(count - 1);

      uint32_t write_addr = xe::load_and_swap<uint32_t>(ptr);
      auto endianness = static_cast<xenos::Endian>(write_addr & 0x3);
      auto addr = write_addr & ~0x3;

      mwr.mem_write.addr = addr;
      mwr.mem_write.endianness = endianness;

      // can't use injectbewords here, have to apply gpuswap to each element
      for (uint32_t i = 0; i < count - 1; i++) {
        uint32_t write_data = xe::load_and_swap<uint32_t>(&ptr[(i + 1) * 4]);
        write_data = GpuSwap(write_data, endianness);
        mwr.words.push_back(write_data);
        write_addr += 4;
      }

      out_actions.emplace_back(std::move(mwr));
      break;
    }

    case PM4_REG_TO_MEM: {
      PacketAction r2m;
      r2m.type = Type::kRegToMem;
      r2m.reg2mem.reg_addr = xe::load_and_swap<uint32_t>(ptr);
      uint32_t dword1 = xe::load_and_swap<uint32_t>(ptr + 4);
      r2m.reg2mem.endianness = static_cast<xenos::Endian>(dword1 & 0x3);
      r2m.reg2mem.mem_addr = dword1 & (~3U);
      out_actions.emplace_back(std::move(r2m));
      break;
    }

    // Ignored packets - useful if breaking on the default handler below.
    case 0x50: {  // 0xC0015000 usually 2 words, 0xFFFFFFFF / 0x00000000
      static const PacketTypeInfo op_info = {PacketCategory::kGeneric,
                                             "PM4_TYPE3_0x50"};
      out_info->type_info = &op_info;
      break;
    }
    case 0x51: {  // 0xC0015100 usually 2 words, 0xFFFFFFFF / 0xFFFFFFFF
      static const PacketTypeInfo op_info = {PacketCategory::kGeneric,
                                             "PM4_TYPE3_0x51"};
      out_info->type_info = &op_info;
      break;
    }
    default: {
      result = false;
      break;
    }
  }

  return result;
}

bool PacketDisassembler::DisasmPacket(const uint8_t* base_ptr,
                                      PacketInfo* out_info) {
  const uint32_t packet = xe::load_and_swap<uint32_t>(base_ptr);
  const uint32_t packet_type = packet >> 30;
  switch (packet_type) {
    case 0x00:
      return DisasmPacketType0(base_ptr, packet, out_info);
    case 0x01:
      return DisasmPacketType1(base_ptr, packet, out_info);
    case 0x02:
      return DisasmPacketType2(base_ptr, packet, out_info);
    case 0x03:
      return DisasmPacketType3(base_ptr, packet, out_info);
    default:
      assert_unhandled_case(packet_type);
      return false;
  }
}

}  //  namespace gpu
}  //  namespace xe
