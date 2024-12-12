#pragma once

#if !defined(NDEBUG)
#define XE_ENABLE_PM4_DISASM 1
#endif

using namespace xe::gpu::xenos;
void COMMAND_PROCESSOR::ExecuteIndirectBuffer(uint32_t ptr,
                                              uint32_t count) XE_RESTRICT {
  SCOPE_profile_cpu_f("gpu");

  trace_writer_.WriteIndirectBufferStart(ptr, count * sizeof(uint32_t));
  if (count != 0) {
    RingBuffer old_reader = reader_;

    // Execute commands!
    new (&reader_)
        RingBuffer(memory_->TranslatePhysical(ptr), count * sizeof(uint32_t));
    reader_.set_write_offset(count * sizeof(uint32_t));
    // prefetch the wraparound range
    // it likely is already in L3 cache, but in a zen system it may be another
    // chiplets l3
    reader_.BeginPrefetchedRead<swcache::PrefetchTag::Level2>(
        COMMAND_PROCESSOR::GetCurrentRingReadCount());
    do {
      if (COMMAND_PROCESSOR::ExecutePacket()) {
        continue;
      } else {
        // Return up a level if we encounter a bad packet.
        XELOGE("**** INDIRECT RINGBUFFER: Failed to execute packet.");
        assert_always();
        // break;
      }
    } while (reader_.read_count());

    trace_writer_.WriteIndirectBufferEnd();
    reader_ = old_reader;
  } else {
    // rare, but i've seen it happen! (and then a division by 0 occurs)
    return;
  }
}
XE_NOINLINE
static void LOGU32s(logging::LoggerBatch<LogLevel::Debug>& logger,
                    const std::vector<uint32_t>& values) {
  bool first = true;
#if 0
  XELOGD("[ ");
  for (auto&& val : values) {
    if (first) {
      XELOGD("0x{:08X}", val);
      first = false;
    } else {
      XELOGD(", 0x{:08X}", val);
    }
  }
  XELOGD(" ]");
#else

  for (auto&& val : values) {
    if (first) {
      logger("0x{:08X}", val);
      first = false;
    } else {
      logger(", 0x{:08X}", val);
    }
  }
#endif
}

std::string GenerateRegnameForPm4Print(uint32_t reg) {
  auto reg_info = RegisterFile::GetRegisterInfo(reg);

  if (reg_info) {
    return reg_info->name;
  } else {
    return fmt::format("Unknown_Reg_{:04X}", reg);
  }
}

XE_NOINLINE
void COMMAND_PROCESSOR::DisassembleCurrentPacket() XE_RESTRICT {
  xe::gpu::PacketInfo packet_info;

  logging::LoggerBatch<LogLevel::Debug> logger{};

  if (PacketDisassembler::DisasmPacket(reader_.buffer() + reader_.read_offset(),
                                       &packet_info)) {
    logger("CP - {}, count {}, predicated = {}\n", packet_info.type_info->name,
           packet_info.count, packet_info.predicated);

#define LOG_ACTION_FIELD(__type, name) \
  logger("\t" #name " = {:08X}\n", static_cast<uint32_t>(action.__type.name))
#define LOG_ACTION_FIELD_DEC(__type, name) \
  logger("\t" #name " = {}\n", static_cast<size_t>(action.__type.name))

#define LOG_ENDIANNESS(__type, name) \
  logger("\t" #name " = {}\n",       \
         xenos::GetEndianEnglishDescription(action.__type.name))
#define LOG_PRIMTYPE(__type, name) \
  logger("\t" #name " = {}\n",     \
         xenos::GetPrimitiveTypeEnglishDescription(action.__type.name))
    for (auto&& action : packet_info.actions) {
      using PType = PacketAction::Type;
      switch (action.type) {
        case PType::kRegisterWrite:
          break;
        case PType::kSetBinMask:
          logger("\tSetBinMask {}\n", action.set_bin_mask.value);
          break;
        case PType::kSetBinSelect:
          logger("\tSetBinSelect {}\n", action.set_bin_select.value);
          break;
        case PType::kMeInit:
          logger("\tMeInit - ");
          LOGU32s(logger, action.words);
          logger("\n");
          break;
        case PType::kGenInterrupt:
          logger("\tGenInterrupt for cpu mask {:04X}\n",
                 action.gen_interrupt.cpu_mask);
          break;
        case PType::kSetBinMaskHi:
        case PType::kSetBinMaskLo:
        case PType::kSetBinSelectHi:
        case PType::kSetBinSelectLo:
          LOG_ACTION_FIELD(lohi_op, value);
          break;
        case PType::kWaitRegMem:
          LOG_ACTION_FIELD(wait_reg_mem, wait_info);
          LOG_ACTION_FIELD(wait_reg_mem, poll_reg_addr);
          LOG_ACTION_FIELD(wait_reg_mem, ref);
          LOG_ACTION_FIELD(wait_reg_mem, mask);
          LOG_ACTION_FIELD(wait_reg_mem, wait);
          break;
        case PType::kRegRmw: {
          uint32_t rmw_info = action.reg_rmw.rmw_info;
          uint32_t and_mask = action.reg_rmw.and_mask;
          uint32_t or_mask = action.reg_rmw.or_mask;

          uint32_t and_mask_is_reg = (rmw_info >> 31) & 0x1;
          uint32_t or_mask_is_reg = (rmw_info >> 30) & 0x1;

          std::string and_mask_str;

          if (and_mask_is_reg) {
            and_mask_str = GenerateRegnameForPm4Print(and_mask & 0x1FFF);
          } else {
            and_mask_str = fmt::format("0x{:08X}", and_mask);
          }
          std::string or_mask_str;

          if (or_mask_is_reg) {
            or_mask_str = GenerateRegnameForPm4Print(or_mask & 0x1FFF);
          } else {
            or_mask_str = fmt::format("0x{:08X}", or_mask);
          }

          std::string dest = GenerateRegnameForPm4Print(rmw_info & 0x1FFF);

          logger("\t{} = ({} & {}) | {}\n", dest, dest, and_mask_str,
                 or_mask_str);
          LOG_ACTION_FIELD(reg_rmw, rmw_info);
          LOG_ACTION_FIELD(reg_rmw, and_mask);
          LOG_ACTION_FIELD(reg_rmw, or_mask);

          break;
        }
        case PType::kCondWrite:
          LOG_ACTION_FIELD(cond_write, wait_info);
          LOG_ACTION_FIELD(cond_write, poll_reg_addr);
          LOG_ACTION_FIELD(cond_write, ref);
          LOG_ACTION_FIELD(cond_write, mask);
          LOG_ACTION_FIELD(cond_write, write_reg_addr);
          LOG_ACTION_FIELD(cond_write, write_data);
          break;

        case PType::kEventWrite:
          LOG_ACTION_FIELD(event_write, initiator);
          break;
        case PType::kEventWriteSHD:
          LOG_ACTION_FIELD(event_write_shd, initiator);
          LOG_ACTION_FIELD(event_write_shd, address);
          LOG_ACTION_FIELD(event_write_shd, value);
          break;
        case PType::kEventWriteExt:
          LOG_ACTION_FIELD(event_write_ext, unk0);
          LOG_ACTION_FIELD(event_write_ext, unk1);
          break;
        case PType::kDrawIndx:
          LOG_ACTION_FIELD(draw_indx, dword0);
          LOG_ACTION_FIELD(draw_indx, dword1);
          LOG_ACTION_FIELD_DEC(draw_indx, index_count);
          LOG_PRIMTYPE(draw_indx, prim_type);
          LOG_ACTION_FIELD(draw_indx, src_sel);
          LOG_ACTION_FIELD(draw_indx, guest_base);
          LOG_ACTION_FIELD_DEC(draw_indx, index_size);
          LOG_ENDIANNESS(draw_indx, endianness);
          break;
        case PType::kDrawIndx2:
          LOG_ACTION_FIELD(draw_indx2, dword0);
          LOG_ACTION_FIELD_DEC(draw_indx2, index_count);
          LOG_PRIMTYPE(draw_indx2, prim_type);
          LOG_ACTION_FIELD(draw_indx2, src_sel);
          LOG_ACTION_FIELD_DEC(draw_indx2, indices_size);
          logger("Indices = ");
          LOGU32s(logger, action.words);
          logger("\n");
          break;
        case PType::kInvalidateState:
          LOG_ACTION_FIELD(invalidate_state, state_mask);
          break;
        case PType::kImLoad:
          LOG_ACTION_FIELD(im_load, shader_type);
          LOG_ACTION_FIELD(im_load, addr);
          LOG_ACTION_FIELD(im_load, start);
          LOG_ACTION_FIELD_DEC(im_load, size_dwords);
          break;
        case PType::kImLoadImmediate:
          LOG_ACTION_FIELD_DEC(im_load_imm, shader_type);
          LOG_ACTION_FIELD(im_load_imm, start);
          LOG_ACTION_FIELD_DEC(im_load_imm, size_dwords);
          logger("Shader instruction words = ");
          LOGU32s(logger, action.words);
          logger("\n");
          break;
        case PType::kWaitForIdle:
          LOG_ACTION_FIELD(wait_for_idle, probably_unused);
          break;
        case PType::kContextUpdate:
          LOG_ACTION_FIELD(context_update, maybe_unused);
          break;
        case PType::kVizQuery:
          LOG_ACTION_FIELD_DEC(vizquery, id);
          LOG_ACTION_FIELD_DEC(vizquery, end);
          LOG_ACTION_FIELD(vizquery, dword0);
          break;
        case PType::kEventWriteZPD:
          LOG_ACTION_FIELD(event_write_zpd, initiator);

          break;
        case PType::kMemWrite:
          LOG_ACTION_FIELD(mem_write, addr);
          LOG_ENDIANNESS(mem_write, endianness);
          logger("Values to write (with GpuSwap pre-applied) = ");
          LOGU32s(logger, action.words);
          logger("\n");
          break;
        case PType::kRegToMem:
          logger("{}\n", GenerateRegnameForPm4Print(action.reg2mem.reg_addr));
          LOG_ACTION_FIELD(reg2mem, mem_addr);
          LOG_ENDIANNESS(reg2mem, endianness);
          break;
        case PType::kIndirBuffer:
          LOG_ACTION_FIELD(indir_buffer, list_ptr);
          LOG_ACTION_FIELD_DEC(indir_buffer, list_length);
          break;
        case PType::kXeSwap:
          LOG_ACTION_FIELD(xe_swap, frontbuffer_ptr);
          break;
      }
    }
  } else {
    logger("Unknown packet! Failed to disassemble.\n");
  }
  logger.submit('d');
}
bool COMMAND_PROCESSOR::ExecutePacket() {
#if XE_ENABLE_PM4_DISASM == 1
  if (cvars::disassemble_pm4 && logging::internal::ShouldLog(LogLevel::Debug)) {
    COMMAND_PROCESSOR::DisassembleCurrentPacket();
  }
#endif
  const uint32_t packet = reader_.ReadAndSwap<uint32_t>();
  const uint32_t packet_type = packet >> 30;

  XE_LIKELY_IF(packet && packet != 0x0BADF00D) {
    XE_LIKELY_IF((packet != 0xCDCDCDCD)) {
    actually_execute_packet:
      // chrispy: reorder checks by probability
      XE_LIKELY_IF(packet_type == 3) {
        return COMMAND_PROCESSOR::ExecutePacketType3(packet);
      }
      else {
        if (packet_type ==
            0) {  // dont know whether 0 or 1 are the next most frequent
          return COMMAND_PROCESSOR::ExecutePacketType0(packet);
        } else {
          if (packet_type == 1) {
            return COMMAND_PROCESSOR::ExecutePacketType1(packet);
          } else {
            // originally there was a default case that msvc couldn't optimize
            // away because it doesnt have value range analysis but in reality
            // there is no default, a uint32_t >> 30 only has 4 possible values
            // and all are covered here
            // return COMMAND_PROCESSOR::ExecutePacketType2(packet);
            // executepackettype2 is identical
            goto handle_bad_packet;
          }
        }
      }
    }
    else {
      XELOGW("GPU packet is CDCDCDCD - probably read uninitialized memory!");
      goto actually_execute_packet;
    }
  }
  else {
  handle_bad_packet:
    trace_writer_.WritePacketStart(uint32_t(reader_.read_ptr() - 4), 1);
    trace_writer_.WritePacketEnd();
    return true;
  }
}
XE_NOINLINE
XE_COLD
bool COMMAND_PROCESSOR::ExecutePacketType0_CountOverflow(uint32_t count) {
  XELOGE("ExecutePacketType0 overflow (read count {:08X}, packet count {:08X})",
         COMMAND_PROCESSOR::GetCurrentRingReadCount(),
         count * sizeof(uint32_t));
  return false;
}
/*
    Todo: optimize this function this one along with execute packet type III are
   the most frequently called functions for PM4
*/
XE_NOINLINE
bool COMMAND_PROCESSOR::ExecutePacketType0(uint32_t packet) XE_RESTRICT {
  // Type-0 packet.
  // Write count registers in sequence to the registers starting at
  // (base_index << 2).

  uint32_t count = ((packet >> 16) & 0x3FFF) + 1;

  if (COMMAND_PROCESSOR::GetCurrentRingReadCount() >=
      count * sizeof(uint32_t)) {
    trace_writer_.WritePacketStart(uint32_t(reader_.read_ptr() - 4), 1 + count);

    uint32_t base_index = (packet & 0x7FFF);
    uint32_t write_one_reg = (packet >> 15) & 0x1;

    if (!write_one_reg) {
      COMMAND_PROCESSOR::WriteRegisterRangeFromRing(&reader_, base_index,
                                                    count);

    } else {
      COMMAND_PROCESSOR::WriteOneRegisterFromRing(base_index, count);
    }

    trace_writer_.WritePacketEnd();
    return true;
  } else {
    return COMMAND_PROCESSOR::ExecutePacketType0_CountOverflow(count);
  }
}
XE_NOINLINE
bool COMMAND_PROCESSOR::ExecutePacketType1(uint32_t packet) XE_RESTRICT {
  // Type-1 packet.
  // Contains two registers of data. Type-0 should be more common.
  trace_writer_.WritePacketStart(uint32_t(reader_.read_ptr() - 4), 3);
  uint32_t reg_index_1 = packet & 0x7FF;
  uint32_t reg_index_2 = (packet >> 11) & 0x7FF;
  uint32_t reg_data_1 = reader_.ReadAndSwap<uint32_t>();
  uint32_t reg_data_2 = reader_.ReadAndSwap<uint32_t>();
  COMMAND_PROCESSOR::WriteRegister(reg_index_1, reg_data_1);
  COMMAND_PROCESSOR::WriteRegister(reg_index_2, reg_data_2);
  trace_writer_.WritePacketEnd();
  return true;
}

bool COMMAND_PROCESSOR::ExecutePacketType2(uint32_t packet) XE_RESTRICT {
  // Type-2 packet.
  // No-op. Do nothing.
  trace_writer_.WritePacketStart(uint32_t(reader_.read_ptr() - 4), 1);
  trace_writer_.WritePacketEnd();
  return true;
}
XE_FORCEINLINE
XE_NOALIAS
uint32_t COMMAND_PROCESSOR::GetCurrentRingReadCount() {
  return reader_.read_count();
}
XE_NOINLINE
XE_COLD
bool COMMAND_PROCESSOR::ExecutePacketType3_CountOverflow(uint32_t count) {
  XELOGE("ExecutePacketType3 overflow (read count {:08X}, packet count {:08X})",
         COMMAND_PROCESSOR::GetCurrentRingReadCount(),
         count * sizeof(uint32_t));
  return false;
}
XE_NOINLINE
bool COMMAND_PROCESSOR::ExecutePacketType3(uint32_t packet) XE_RESTRICT {
  // Type-3 packet.
  uint32_t opcode = (packet >> 8) & 0x7F;
  uint32_t count = ((packet >> 16) & 0x3FFF) + 1;
  auto data_start_offset = reader_.read_offset();

  if (COMMAND_PROCESSOR::GetCurrentRingReadCount() >=
      count * sizeof(uint32_t)) {
    // To handle nesting behavior when tracing we special case indirect buffers.
    if (opcode == PM4_INDIRECT_BUFFER) {
      trace_writer_.WritePacketStart(uint32_t(reader_.read_ptr() - 4), 2);
    } else {
      trace_writer_.WritePacketStart(uint32_t(reader_.read_ptr() - 4),
                                     1 + count);
    }

    // & 1 == predicate - when set, we do bin check to see if we should execute
    // the packet. Only type 3 packets are affected.
    // We also skip predicated swaps, as they are never valid (probably?).
    if (packet & 1) {
      bool any_pass = (bin_select_ & bin_mask_) != 0;
      if (!any_pass || opcode == PM4_XE_SWAP) {
        reader_.AdvanceRead(count * sizeof(uint32_t));
        trace_writer_.WritePacketEnd();
        return true;
      }
    }

    bool result = false;
    switch (opcode) {
      case PM4_ME_INIT:
        result = COMMAND_PROCESSOR::ExecutePacketType3_ME_INIT(packet, count);
        break;
      case PM4_NOP:
        result = COMMAND_PROCESSOR::ExecutePacketType3_NOP(packet, count);
        break;
      case PM4_INTERRUPT:
        result = COMMAND_PROCESSOR::ExecutePacketType3_INTERRUPT(packet, count);
        break;
      case PM4_XE_SWAP:
        result = COMMAND_PROCESSOR::ExecutePacketType3_XE_SWAP(packet, count);
        break;
      case PM4_INDIRECT_BUFFER:
      case PM4_INDIRECT_BUFFER_PFD:
        result = COMMAND_PROCESSOR::ExecutePacketType3_INDIRECT_BUFFER(packet,
                                                                       count);
        break;
      case PM4_WAIT_REG_MEM:
        result =
            COMMAND_PROCESSOR::ExecutePacketType3_WAIT_REG_MEM(packet, count);
        break;
      case PM4_REG_RMW:
        result = COMMAND_PROCESSOR::ExecutePacketType3_REG_RMW(packet, count);
        break;
      case PM4_REG_TO_MEM:
        result =
            COMMAND_PROCESSOR::ExecutePacketType3_REG_TO_MEM(packet, count);
        break;
      case PM4_MEM_WRITE:
        result = COMMAND_PROCESSOR::ExecutePacketType3_MEM_WRITE(packet, count);
        break;
      case PM4_COND_WRITE:
        result =
            COMMAND_PROCESSOR::ExecutePacketType3_COND_WRITE(packet, count);
        break;
      case PM4_EVENT_WRITE:
        result =
            COMMAND_PROCESSOR::ExecutePacketType3_EVENT_WRITE(packet, count);
        break;
      case PM4_EVENT_WRITE_SHD:
        result = COMMAND_PROCESSOR::ExecutePacketType3_EVENT_WRITE_SHD(packet,
                                                                       count);
        break;
      case PM4_EVENT_WRITE_EXT:
        result = COMMAND_PROCESSOR::ExecutePacketType3_EVENT_WRITE_EXT(packet,
                                                                       count);
        break;
      case PM4_EVENT_WRITE_ZPD:
        result = COMMAND_PROCESSOR::ExecutePacketType3_EVENT_WRITE_ZPD(packet,
                                                                       count);
        break;
      case PM4_DRAW_INDX:
        result = COMMAND_PROCESSOR::ExecutePacketType3_DRAW_INDX(packet, count);
        break;
      case PM4_DRAW_INDX_2:
        result =
            COMMAND_PROCESSOR::ExecutePacketType3_DRAW_INDX_2(packet, count);
        break;
      case PM4_SET_CONSTANT:
        result =
            COMMAND_PROCESSOR::ExecutePacketType3_SET_CONSTANT(packet, count);
        break;
      case PM4_SET_CONSTANT2:
        result =
            COMMAND_PROCESSOR::ExecutePacketType3_SET_CONSTANT2(packet, count);
        break;
      case PM4_LOAD_ALU_CONSTANT:
        result = COMMAND_PROCESSOR::ExecutePacketType3_LOAD_ALU_CONSTANT(packet,
                                                                         count);
        break;
      case PM4_SET_SHADER_CONSTANTS:
        result = COMMAND_PROCESSOR::ExecutePacketType3_SET_SHADER_CONSTANTS(
            packet, count);
        break;
      case PM4_IM_LOAD:
        result = COMMAND_PROCESSOR::ExecutePacketType3_IM_LOAD(packet, count);
        break;
      case PM4_IM_LOAD_IMMEDIATE:
        result = COMMAND_PROCESSOR::ExecutePacketType3_IM_LOAD_IMMEDIATE(packet,
                                                                         count);
        break;
      case PM4_INVALIDATE_STATE:
        result = COMMAND_PROCESSOR::ExecutePacketType3_INVALIDATE_STATE(packet,
                                                                        count);
        break;
      case PM4_VIZ_QUERY:
        result = COMMAND_PROCESSOR::ExecutePacketType3_VIZ_QUERY(packet, count);
        break;

      case PM4_SET_BIN_MASK_LO: {
        uint32_t value = reader_.ReadAndSwap<uint32_t>();
        bin_mask_ = (bin_mask_ & 0xFFFFFFFF00000000ull) | value;
        result = true;
      } break;
      case PM4_SET_BIN_MASK_HI: {
        uint32_t value = reader_.ReadAndSwap<uint32_t>();
        bin_mask_ =
            (bin_mask_ & 0xFFFFFFFFull) | (static_cast<uint64_t>(value) << 32);
        result = true;
      } break;
      case PM4_SET_BIN_SELECT_LO: {
        uint32_t value = reader_.ReadAndSwap<uint32_t>();
        bin_select_ = (bin_select_ & 0xFFFFFFFF00000000ull) | value;
        result = true;
      } break;
      case PM4_SET_BIN_SELECT_HI: {
        uint32_t value = reader_.ReadAndSwap<uint32_t>();
        bin_select_ = (bin_select_ & 0xFFFFFFFFull) |
                      (static_cast<uint64_t>(value) << 32);
        result = true;
      } break;
      case PM4_SET_BIN_MASK: {
        assert_true(count == 2);
        uint64_t val_hi = reader_.ReadAndSwap<uint32_t>();
        uint64_t val_lo = reader_.ReadAndSwap<uint32_t>();
        bin_mask_ = (val_hi << 32) | val_lo;
        result = true;
      } break;
      case PM4_SET_BIN_SELECT: {
        assert_true(count == 2);
        uint64_t val_hi = reader_.ReadAndSwap<uint32_t>();
        uint64_t val_lo = reader_.ReadAndSwap<uint32_t>();
        bin_select_ = (val_hi << 32) | val_lo;
        result = true;
      } break;
      case PM4_CONTEXT_UPDATE: {
        assert_true(count == 1);
        uint32_t value = reader_.ReadAndSwap<uint32_t>();
        XELOGGPU("GPU context update = {:08X}", value);
        assert_true(value == 0);
        result = true;
        break;
      }
      case PM4_WAIT_FOR_IDLE: {
        // This opcode is used by 5454084E while going / being ingame.
        assert_true(count == 1);
        uint32_t value = reader_.ReadAndSwap<uint32_t>();
        XELOGGPU("GPU wait for idle = {:08X}", value);
        result = true;
        break;
      }

      default:
        return COMMAND_PROCESSOR::HitUnimplementedOpcode(opcode, count);
    }

    trace_writer_.WritePacketEnd();
#if XE_ENABLE_TRACE_WRITER_INSTRUMENTATION == 1

    if (opcode == PM4_XE_SWAP) {
      // End the trace writer frame.
      if (trace_writer_.is_open()) {
        trace_writer_.WriteEvent(EventCommand::Type::kSwap);
        trace_writer_.Flush();
        if (trace_state_ == TraceState::kSingleFrame) {
          trace_state_ = TraceState::kDisabled;
          trace_writer_.Close();
        }
      } else if (trace_state_ == TraceState::kSingleFrame) {
        // New trace request - we only start tracing at the beginning of a
        // frame.
        uint32_t title_id = kernel_state_->GetExecutableModule()->title_id();
        auto file_name = fmt::format("{:08X}_{}.xtr", title_id, counter_ - 1);
        auto path = trace_frame_path_ / file_name;
        trace_writer_.Open(path, title_id);
        InitializeTrace();
      }
    }
#endif

    assert_true(reader_.read_offset() ==
                (data_start_offset + (count * sizeof(uint32_t))) %
                    reader_.capacity());
    return result;
  } else {
    return COMMAND_PROCESSOR::ExecutePacketType3_CountOverflow(count);
  }
}

XE_NOINLINE
XE_COLD
bool COMMAND_PROCESSOR::HitUnimplementedOpcode(uint32_t opcode,
                                               uint32_t count) XE_RESTRICT {
  XELOGGPU("Unimplemented GPU OPCODE: 0x{:02X}\t\tCOUNT: {}\n", opcode, count);
  assert_always();
  reader_.AdvanceRead(count * sizeof(uint32_t));
  trace_writer_.WritePacketEnd();
  return false;
}
XE_NOINLINE
bool COMMAND_PROCESSOR::ExecutePacketType3_ME_INIT(uint32_t packet,
                                                   uint32_t count) XE_RESTRICT {
  // initialize CP's micro-engine
  me_bin_.resize(count);
  for (uint32_t i = 0; i < count; i++) {
    me_bin_[i] = reader_.ReadAndSwap<uint32_t>();
  }
  return true;
}

bool COMMAND_PROCESSOR::ExecutePacketType3_NOP(uint32_t packet,
                                               uint32_t count) XE_RESTRICT {
  // skip N 32-bit words to get to the next packet
  // No-op, ignore some data.
  reader_.AdvanceRead(count * sizeof(uint32_t));
  return true;
}
XE_NOINLINE
bool COMMAND_PROCESSOR::ExecutePacketType3_INTERRUPT(
    uint32_t packet, uint32_t count) XE_RESTRICT {
  SCOPE_profile_cpu_f("gpu");

  // generate interrupt from the command stream
  uint32_t cpu_mask = reader_.ReadAndSwap<uint32_t>();
  for (int n = 0; n < 6; n++) {
    if (cpu_mask & (1 << n)) {
      graphics_system_->DispatchInterruptCallback(1, n);
    }
  }
  return true;
}
XE_NOINLINE
bool COMMAND_PROCESSOR::ExecutePacketType3_XE_SWAP(uint32_t packet,
                                                   uint32_t count) XE_RESTRICT {
  SCOPE_profile_cpu_f("gpu");

  Profiler::Flip();

  // Xenia-specific VdSwap hook.
  // VdSwap will post this to tell us we need to swap the screen/fire an
  // interrupt.
  // 63 words here, but only the first has any data.
  uint32_t magic = reader_.ReadAndSwap<fourcc_t>();
  assert_true(magic == kSwapSignature);

  // TODO(benvanik): only swap frontbuffer ptr.
  uint32_t frontbuffer_ptr = reader_.ReadAndSwap<uint32_t>();
  uint32_t frontbuffer_width = reader_.ReadAndSwap<uint32_t>();
  uint32_t frontbuffer_height = reader_.ReadAndSwap<uint32_t>();
  reader_.AdvanceRead((count - 4) * sizeof(uint32_t));

  COMMAND_PROCESSOR::IssueSwap(frontbuffer_ptr, frontbuffer_width,
                               frontbuffer_height);

  ++counter_;
  return true;
}

bool COMMAND_PROCESSOR::ExecutePacketType3_INDIRECT_BUFFER(
    uint32_t packet, uint32_t count) XE_RESTRICT {
  // indirect buffer dispatch
  uint32_t list_ptr = CpuToGpu(reader_.ReadAndSwap<uint32_t>());
  uint32_t list_length = reader_.ReadAndSwap<uint32_t>();
  assert_zero(list_length & ~0xFFFFF);
  list_length &= 0xFFFFF;
  COMMAND_PROCESSOR::ExecuteIndirectBuffer(GpuToCpu(list_ptr), list_length);
  return true;
}

/*
        chrispy: this is fine to inline, as a noinline function it compiled down
   to 54 bytes
*/
static bool MatchValueAndRef(uint32_t value, uint32_t ref, uint32_t wait_info) {
  // smaller code is generated than the #else path, although whether it is
  // faster i do not know. i don't think games do an enormous number of
  // cond_write though, so we have picked the path with the smaller codegen. we
  // do technically have more instructions executed vs the switch case method,
  // but we have no mispredicts and most of our instructions are 0.25/0.3
  // throughput
  return ((((value < ref) << 1) | ((value <= ref) << 2) |
           ((value == ref) << 3) | ((value != ref) << 4) |
           ((value >= ref) << 5) | ((value > ref) << 6) | (1 << 7)) >>
          (wait_info & 7)) &
         1;
}
XE_NOINLINE
bool COMMAND_PROCESSOR::ExecutePacketType3_WAIT_REG_MEM(
    uint32_t packet, uint32_t count) XE_RESTRICT {
  SCOPE_profile_cpu_f("gpu");

  // wait until a register or memory location is a specific value
  uint32_t wait_info = reader_.ReadAndSwap<uint32_t>();
  uint32_t poll_reg_addr = reader_.ReadAndSwap<uint32_t>();
  uint32_t ref = reader_.ReadAndSwap<uint32_t>();
  uint32_t mask = reader_.ReadAndSwap<uint32_t>();
  uint32_t wait = reader_.ReadAndSwap<uint32_t>();

  bool is_memory = (wait_info & 0x10) != 0;
  assert_true(is_memory || poll_reg_addr < RegisterFile::kRegisterCount);
  const volatile uint32_t& value_ref =
      is_memory ? *reinterpret_cast<uint32_t*>(memory_->TranslatePhysical(
                      poll_reg_addr & ~uint32_t(0x3)))
                : register_file_->values[poll_reg_addr];

  bool matched = false;

  do {
    uint32_t value = value_ref;
    if (is_memory) {
      trace_writer_.WriteMemoryRead(CpuToGpu(poll_reg_addr & ~uint32_t(0x3)),
                                    sizeof(uint32_t));
      value = xenos::GpuSwap(value,
                             static_cast<xenos::Endian>(poll_reg_addr & 0x3));
    } else {
      if (poll_reg_addr == XE_GPU_REG_COHER_STATUS_HOST) {
        MakeCoherent();
        value = value_ref;
      }
    }
    matched = MatchValueAndRef(value & mask, ref, wait_info);

    if (!matched) {
      // Wait.
      if (wait >= 0x100) {
        PrepareForWait();
        if (!cvars::vsync) {
          // User wants it fast and dangerous.
          // do nothing
        } else {
          xe::threading::Sleep(std::chrono::milliseconds(wait / 0x100));
          ReturnFromWait();
        }

        if (!worker_running_) {
          // Short-circuited exit.
          return false;
        }
      } else {
      }
    }
  } while (!matched);

  return true;
}
XE_NOINLINE
bool COMMAND_PROCESSOR::ExecutePacketType3_REG_RMW(uint32_t packet,
                                                   uint32_t count) XE_RESTRICT {
  // register read/modify/write
  // ? (used during shader upload and edram setup)
  uint32_t rmw_info = reader_.ReadAndSwap<uint32_t>();
  uint32_t and_mask = reader_.ReadAndSwap<uint32_t>();
  uint32_t or_mask = reader_.ReadAndSwap<uint32_t>();
  uint32_t value = register_file_->values[rmw_info & 0x1FFF];
  if ((rmw_info >> 31) & 0x1) {
    // & reg
    value &= register_file_->values[and_mask & 0x1FFF];
  } else {
    // & imm
    value &= and_mask;
  }
  if ((rmw_info >> 30) & 0x1) {
    // | reg
    value |= register_file_->values[or_mask & 0x1FFF];
  } else {
    // | imm
    value |= or_mask;
  }
  COMMAND_PROCESSOR::WriteRegister(rmw_info & 0x1FFF, value);
  return true;
}

bool COMMAND_PROCESSOR::ExecutePacketType3_REG_TO_MEM(
    uint32_t packet, uint32_t count) XE_RESTRICT {
  // Copy Register to Memory (?)
  // Count is 2, assuming a Register Addr and a Memory Addr.

  uint32_t reg_addr = reader_.ReadAndSwap<uint32_t>();
  uint32_t mem_addr = reader_.ReadAndSwap<uint32_t>();

  uint32_t reg_val;

  assert_true(reg_addr < RegisterFile::kRegisterCount);
  reg_val = register_file_->values[reg_addr];

  auto endianness = static_cast<xenos::Endian>(mem_addr & 0x3);
  mem_addr &= ~0x3;
  reg_val = GpuSwap(reg_val, endianness);
  xe::store(memory_->TranslatePhysical(mem_addr), reg_val);
  trace_writer_.WriteMemoryWrite(CpuToGpu(mem_addr), 4);

  return true;
}
XE_NOINLINE
bool COMMAND_PROCESSOR::ExecutePacketType3_MEM_WRITE(
    uint32_t packet, uint32_t count) XE_RESTRICT {
  uint32_t write_addr = reader_.ReadAndSwap<uint32_t>();
  for (uint32_t i = 0; i < count - 1; i++) {
    uint32_t write_data = reader_.ReadAndSwap<uint32_t>();

    auto endianness = static_cast<xenos::Endian>(write_addr & 0x3);
    auto addr = write_addr & ~0x3;
    write_data = GpuSwap(write_data, endianness);
    xe::store(memory_->TranslatePhysical(addr), write_data);
    trace_writer_.WriteMemoryWrite(CpuToGpu(addr), 4);
    write_addr += 4;
  }

  return true;
}
XE_NOINLINE
bool COMMAND_PROCESSOR::ExecutePacketType3_COND_WRITE(
    uint32_t packet, uint32_t count) XE_RESTRICT {
  // conditional write to memory or register
  uint32_t wait_info = reader_.ReadAndSwap<uint32_t>();
  uint32_t poll_reg_addr = reader_.ReadAndSwap<uint32_t>();
  uint32_t ref = reader_.ReadAndSwap<uint32_t>();
  uint32_t mask = reader_.ReadAndSwap<uint32_t>();
  uint32_t write_reg_addr = reader_.ReadAndSwap<uint32_t>();
  uint32_t write_data = reader_.ReadAndSwap<uint32_t>();
  uint32_t value;
  if (wait_info & 0x10) {
    // Memory.
    auto endianness = static_cast<xenos::Endian>(poll_reg_addr & 0x3);
    poll_reg_addr &= ~0x3;
    trace_writer_.WriteMemoryRead(CpuToGpu(poll_reg_addr), 4);
    value = xe::load<uint32_t>(memory_->TranslatePhysical(poll_reg_addr));
    value = GpuSwap(value, endianness);
  } else {
    // Register.
    assert_true(poll_reg_addr < RegisterFile::kRegisterCount);
    value = register_file_->values[poll_reg_addr];
  }
  bool matched = MatchValueAndRef(value & mask, ref, wait_info);

  if (matched) {
    // Write.
    if (wait_info & 0x100) {
      // Memory.
      auto endianness = static_cast<xenos::Endian>(write_reg_addr & 0x3);
      write_reg_addr &= ~0x3;
      write_data = GpuSwap(write_data, endianness);
      xe::store(memory_->TranslatePhysical(write_reg_addr), write_data);
      trace_writer_.WriteMemoryWrite(CpuToGpu(write_reg_addr), 4);
    } else {
      // Register.
      COMMAND_PROCESSOR::WriteRegister(write_reg_addr, write_data);
    }
  }
  return true;
}
XE_FORCEINLINE
void COMMAND_PROCESSOR::WriteEventInitiator(uint32_t value) XE_RESTRICT {
  register_file_->values[XE_GPU_REG_VGT_EVENT_INITIATOR] = value;
}
bool COMMAND_PROCESSOR::ExecutePacketType3_EVENT_WRITE(
    uint32_t packet, uint32_t count) XE_RESTRICT {
  // generate an event that creates a write to memory when completed
  uint32_t initiator = reader_.ReadAndSwap<uint32_t>();
  // Writeback initiator.

  COMMAND_PROCESSOR::WriteEventInitiator(initiator & 0x3f);
  if (count == 1) {
    // Just an event flag? Where does this write?
  } else {
    // Write to an address.
    assert_always();
    reader_.AdvanceRead((count - 1) * sizeof(uint32_t));
  }
  return true;
}
XE_NOINLINE
bool COMMAND_PROCESSOR::ExecutePacketType3_EVENT_WRITE_SHD(
    uint32_t packet, uint32_t count) XE_RESTRICT {
  // generate a VS|PS_done event
  uint32_t initiator = reader_.ReadAndSwap<uint32_t>();
  uint32_t address = reader_.ReadAndSwap<uint32_t>();
  uint32_t value = reader_.ReadAndSwap<uint32_t>();
  // Writeback initiator.
  COMMAND_PROCESSOR::WriteEventInitiator(initiator & 0x3F);
  uint32_t data_value;
  if ((initiator >> 31) & 0x1) {
    // Write counter (GPU vblank counter?).
    data_value = counter_;
  } else {
    // Write value.
    data_value = value;
  }
  auto endianness = static_cast<xenos::Endian>(address & 0x3);
  address &= ~0x3;
  data_value = GpuSwap(data_value, endianness);
  uint8_t* write_destination = memory_->TranslatePhysical(address);
  if (address > 0x1FFFFFFF) {
    uint32_t writeback_base = register_file_->values[XE_GPU_REG_WRITEBACK_BASE];
    uint32_t writeback_size = register_file_->values[XE_GPU_REG_WRITEBACK_SIZE];
    uint32_t writeback_offset = address - writeback_base;
    // check whether the guest has written writeback base. if they haven't, skip
    // the offset check
    if (writeback_base != 0 && writeback_offset < writeback_size) {
      write_destination =
          memory_->TranslateVirtual(0x7F000000 + writeback_offset);
    }
  }
  xe::store(write_destination, data_value);
  trace_writer_.WriteMemoryWrite(CpuToGpu(address), 4);
  return true;
}

bool COMMAND_PROCESSOR::ExecutePacketType3_EVENT_WRITE_EXT(
    uint32_t packet, uint32_t count) XE_RESTRICT {
  // generate a screen extent event
  uint32_t initiator = reader_.ReadAndSwap<uint32_t>();
  uint32_t address = reader_.ReadAndSwap<uint32_t>();
  // Writeback initiator.
  COMMAND_PROCESSOR::WriteEventInitiator(initiator & 0x3F);
  auto endianness = static_cast<xenos::Endian>(address & 0x3);
  address &= ~0x3;

  // Let us hope we can fake this.
  // This callback tells the driver the xy coordinates affected by a previous
  // drawcall.
  // https://www.google.com/patents/US20060055701
  uint16_t extents[] = {
      byte_swap<unsigned short>(0 >> 3),  // min x
      byte_swap<unsigned short>(xenos::kTexture2DCubeMaxWidthHeight >>
                                3),       // max x
      byte_swap<unsigned short>(0 >> 3),  // min y
      byte_swap<unsigned short>(xenos::kTexture2DCubeMaxWidthHeight >>
                                3),  // max y
      byte_swap<unsigned short>(0),  // min z
      byte_swap<unsigned short>(1),  // max z
  };
  assert_true(endianness == xenos::Endian::k8in16);

  uint16_t* destination = (uint16_t*)memory_->TranslatePhysical(address);

  for (unsigned i = 0; i < 6; ++i) {
    destination[i] = extents[i];
  }

  trace_writer_.WriteMemoryWrite(CpuToGpu(address), sizeof(extents));
  return true;
}
XE_NOINLINE
bool COMMAND_PROCESSOR::ExecutePacketType3_EVENT_WRITE_ZPD(
    uint32_t packet, uint32_t count) XE_RESTRICT {
  // Set by D3D as BE but struct ABI is LE
  const uint32_t kQueryFinished = xe::byte_swap(0xFFFFFEED);
  assert_true(count == 1);
  uint32_t initiator = reader_.ReadAndSwap<uint32_t>();
  // Writeback initiator.
  COMMAND_PROCESSOR::WriteEventInitiator(initiator & 0x3F);

  // Occlusion queries:
  // This command is send on query begin and end.
  // As a workaround report some fixed amount of passed samples.
  auto fake_sample_count = cvars::query_occlusion_fake_sample_count;
  if (fake_sample_count >= 0) {
    auto* pSampleCounts =
        memory_->TranslatePhysical<xe_gpu_depth_sample_counts*>(
            register_file_->values[XE_GPU_REG_RB_SAMPLE_COUNT_ADDR]);
    // 0xFFFFFEED is written to this two locations by D3D only on D3DISSUE_END
    // and used to detect a finished query.
    bool is_end_via_z_pass = pSampleCounts->ZPass_A == kQueryFinished &&
                             pSampleCounts->ZPass_B == kQueryFinished;
    // Older versions of D3D also checks for ZFail (4D5307D5).
    bool is_end_via_z_fail = pSampleCounts->ZFail_A == kQueryFinished &&
                             pSampleCounts->ZFail_B == kQueryFinished;
    std::memset(pSampleCounts, 0, sizeof(xe_gpu_depth_sample_counts));
    if (is_end_via_z_pass || is_end_via_z_fail) {
      pSampleCounts->ZPass_A = fake_sample_count;
      pSampleCounts->Total_A = fake_sample_count;
    }
  }

  return true;
}

bool COMMAND_PROCESSOR::ExecutePacketType3Draw(
    uint32_t packet, const char* opcode_name, uint32_t viz_query_condition,
    uint32_t count_remaining) XE_RESTRICT {
  // if viz_query_condition != 0, this is a conditional draw based on viz query.
  // This ID matches the one issued in PM4_VIZ_QUERY
  // uint32_t viz_id = viz_query_condition & 0x3F;
  // when true, render conditionally based on query result
  // uint32_t viz_use = viz_query_condition & 0x100;

  assert_not_zero(count_remaining);
  if (!count_remaining) {
    XELOGE("{}: Packet too small, can't read VGT_DRAW_INITIATOR", opcode_name);
    return false;
  }
  reg::VGT_DRAW_INITIATOR vgt_draw_initiator;
  vgt_draw_initiator.value = reader_.ReadAndSwap<uint32_t>();
  --count_remaining;

  register_file_->values[XE_GPU_REG_VGT_DRAW_INITIATOR] =
      vgt_draw_initiator.value;
  bool draw_succeeded = true;
  // TODO(Triang3l): Remove IndexBufferInfo and replace handling of all this
  // with PrimitiveProcessor when the old Vulkan renderer is removed.
  bool is_indexed = false;
  IndexBufferInfo index_buffer_info;
  switch (vgt_draw_initiator.source_select) {
    case xenos::SourceSelect::kDMA: {
      // Indexed draw.
      is_indexed = true;

      // Two separate bounds checks so if there's only one missing register
      // value out of two, one uint32_t will be skipped in the command buffer,
      // not two.
      assert_not_zero(count_remaining);
      if (!count_remaining) {
        XELOGE("{}: Packet too small, can't read VGT_DMA_BASE", opcode_name);
        return false;
      }
      uint32_t vgt_dma_base = reader_.ReadAndSwap<uint32_t>();
      --count_remaining;
      register_file_->values[XE_GPU_REG_VGT_DMA_BASE] = vgt_dma_base;
      reg::VGT_DMA_SIZE vgt_dma_size;
      assert_not_zero(count_remaining);
      if (!count_remaining) {
        XELOGE("{}: Packet too small, can't read VGT_DMA_SIZE", opcode_name);
        return false;
      }
      vgt_dma_size.value = reader_.ReadAndSwap<uint32_t>();
      --count_remaining;
      register_file_->values[XE_GPU_REG_VGT_DMA_SIZE] = vgt_dma_size.value;

      uint32_t index_size_bytes =
          vgt_draw_initiator.index_size == xenos::IndexFormat::kInt16
              ? sizeof(uint16_t)
              : sizeof(uint32_t);
      // The base address must already be word-aligned according to the R6xx
      // documentation, but for safety.
      index_buffer_info.guest_base = vgt_dma_base & ~(index_size_bytes - 1);
      index_buffer_info.endianness = vgt_dma_size.swap_mode;
      index_buffer_info.format = vgt_draw_initiator.index_size;
      index_buffer_info.length = vgt_dma_size.num_words * index_size_bytes;
      index_buffer_info.count = vgt_draw_initiator.num_indices;
    } break;
    case xenos::SourceSelect::kImmediate: {
      // TODO(Triang3l): VGT_IMMED_DATA.
      XELOGE(
          "{}: Using immediate vertex indices, which are not supported yet. "
          "Report the game to Xenia developers!",
          opcode_name, uint32_t(vgt_draw_initiator.source_select));
      draw_succeeded = false;
      assert_always();
    } break;
    case xenos::SourceSelect::kAutoIndex: {
      // Auto draw.
      index_buffer_info.guest_base = 0;
      index_buffer_info.length = 0;
    } break;
    default: {
      // Invalid source selection.
      draw_succeeded = false;
      assert_unhandled_case(vgt_draw_initiator.source_select);
    } break;
  }

  // Skip to the next command, for example, if there are immediate indexes that
  // we don't support yet.
  reader_.AdvanceRead(count_remaining * sizeof(uint32_t));

  if (draw_succeeded) {
    auto viz_query = register_file_->Get<reg::PA_SC_VIZ_QUERY>();
    if (!(viz_query.viz_query_ena && viz_query.kill_pix_post_hi_z)) {
      // TODO(Triang3l): Don't drop the draw call completely if the vertex
      // shader has memexport.
      // TODO(Triang3l || JoelLinn): Handle this properly in the render
      // backends.
      draw_succeeded = COMMAND_PROCESSOR::IssueDraw(
          vgt_draw_initiator.prim_type, vgt_draw_initiator.num_indices,
          is_indexed ? &index_buffer_info : nullptr,
          xenos::IsMajorModeExplicit(vgt_draw_initiator.major_mode,
                                     vgt_draw_initiator.prim_type));
      if (!draw_succeeded) {
        XELOGE("{}({}, {}, {}): Failed in backend", opcode_name,
               vgt_draw_initiator.num_indices,
               uint32_t(vgt_draw_initiator.prim_type),
               uint32_t(vgt_draw_initiator.source_select));
      }
    }
  }

  // If read the packed correctly, but merely couldn't execute it (because of,
  // for instance, features not supported by the host), don't terminate command
  // buffer processing as that would leave rendering in a way more inconsistent
  // state than just a single dropped draw command.
  return true;
}

bool COMMAND_PROCESSOR::ExecutePacketType3_DRAW_INDX(
    uint32_t packet, uint32_t count) XE_RESTRICT {
  // "initiate fetch of index buffer and draw"
  // Generally used by Xbox 360 Direct3D 9 for kDMA and kAutoIndex sources.
  // With a viz query token as the first one.
  uint32_t count_remaining = count;
  assert_not_zero(count_remaining);
  if (!count_remaining) {
    XELOGE("PM4_DRAW_INDX: Packet too small, can't read the viz query token");
    return false;
  }
  uint32_t viz_query_condition = reader_.ReadAndSwap<uint32_t>();
  --count_remaining;
  return COMMAND_PROCESSOR::ExecutePacketType3Draw(
      packet, "PM4_DRAW_INDX", viz_query_condition, count_remaining);
}

bool COMMAND_PROCESSOR::ExecutePacketType3_DRAW_INDX_2(
    uint32_t packet, uint32_t count) XE_RESTRICT {
  // "draw using supplied indices in packet"
  // Generally used by Xbox 360 Direct3D 9 for kAutoIndex source.
  // No viz query token.
  return COMMAND_PROCESSOR::ExecutePacketType3Draw(packet, "PM4_DRAW_INDX_2", 0,
                                                   count);
}
XE_FORCEINLINE
bool COMMAND_PROCESSOR::ExecutePacketType3_SET_CONSTANT(
    uint32_t packet, uint32_t count) XE_RESTRICT {
  // load constant into chip and to memory
  // PM4_REG(reg) ((0x4 << 16) | (GSL_HAL_SUBBLOCK_OFFSET(reg)))
  //                                     reg - 0x2000
  uint32_t offset_type = reader_.ReadAndSwap<uint32_t>();
  uint32_t index = offset_type & 0x7FF;
  uint32_t type = (offset_type >> 16) & 0xFF;
  uint32_t countm1 = count - 1;
  switch (type) {
    case 0:  // ALU
      // index += 0x4000;
      // COMMAND_PROCESSOR::WriteRegisterRangeFromRing( index, countm1);
      COMMAND_PROCESSOR::WriteALURangeFromRing(&reader_, index, countm1);
      break;
    case 1:  // FETCH

      COMMAND_PROCESSOR::WriteFetchRangeFromRing(&reader_, index, countm1);

      break;
    case 2:  // BOOL
      COMMAND_PROCESSOR::WriteBoolRangeFromRing(&reader_, index, countm1);

      break;
    case 3:  // LOOP

      COMMAND_PROCESSOR::WriteLoopRangeFromRing(&reader_, index, countm1);

      break;
    case 4:  // REGISTERS

      COMMAND_PROCESSOR::WriteREGISTERSRangeFromRing(&reader_, index, countm1);

      break;
    default:
      assert_always();
      reader_.AdvanceRead((count - 1) * sizeof(uint32_t));
      return true;
  }

  return true;
}
XE_NOINLINE
bool COMMAND_PROCESSOR::ExecutePacketType3_SET_CONSTANT2(
    uint32_t packet, uint32_t count) XE_RESTRICT {
  uint32_t offset_type = reader_.ReadAndSwap<uint32_t>();
  uint32_t index = offset_type & 0xFFFF;
  uint32_t countm1 = count - 1;

  COMMAND_PROCESSOR::WriteRegisterRangeFromRing(&reader_, index, countm1);

  return true;
}
XE_FORCEINLINE
bool COMMAND_PROCESSOR::ExecutePacketType3_LOAD_ALU_CONSTANT(
    uint32_t packet, uint32_t count) XE_RESTRICT {
  // load constants from memory
  uint32_t address = reader_.ReadAndSwap<uint32_t>();
  address &= 0x3FFFFFFF;
  uint32_t offset_type = reader_.ReadAndSwap<uint32_t>();
  uint32_t index = offset_type & 0x7FF;
  uint32_t size_dwords = reader_.ReadAndSwap<uint32_t>();
  size_dwords &= 0xFFF;
  uint32_t type = (offset_type >> 16) & 0xFF;

  auto xlat_address = (uint32_t*)memory_->TranslatePhysical(address);

  switch (type) {
    case 0:  // ALU
      trace_writer_.WriteMemoryRead(CpuToGpu(address), size_dwords * 4);
      COMMAND_PROCESSOR::WriteALURangeFromMem(index, xlat_address, size_dwords);

      break;
    case 1:  // FETCH
      trace_writer_.WriteMemoryRead(CpuToGpu(address), size_dwords * 4);
      COMMAND_PROCESSOR::WriteFetchRangeFromMem(index, xlat_address,
                                                size_dwords);
      break;
    case 2:  // BOOL
      trace_writer_.WriteMemoryRead(CpuToGpu(address), size_dwords * 4);

      COMMAND_PROCESSOR::WriteBoolRangeFromMem(index, xlat_address,
                                               size_dwords);
      break;
    case 3:  // LOOP
      trace_writer_.WriteMemoryRead(CpuToGpu(address), size_dwords * 4);

      COMMAND_PROCESSOR::WriteLoopRangeFromMem(index, xlat_address,
                                               size_dwords);

      break;
    case 4:  // REGISTERS
      // chrispy: todo, REGISTERS cannot write any special regs, so optimize for
      // that
      trace_writer_.WriteMemoryRead(CpuToGpu(address), size_dwords * 4);

      COMMAND_PROCESSOR::WriteREGISTERSRangeFromMem(index, xlat_address,
                                                    size_dwords);
      break;
    default:
      assert_always();
      return true;
  }

  return true;
}

bool COMMAND_PROCESSOR::ExecutePacketType3_SET_SHADER_CONSTANTS(
    uint32_t packet, uint32_t count) XE_RESTRICT {
  uint32_t offset_type = reader_.ReadAndSwap<uint32_t>();
  uint32_t index = offset_type & 0xFFFF;
  uint32_t countm1 = count - 1;
  COMMAND_PROCESSOR::WriteRegisterRangeFromRing(&reader_, index, countm1);

  return true;
}

bool COMMAND_PROCESSOR::ExecutePacketType3_IM_LOAD(uint32_t packet,
                                                   uint32_t count) XE_RESTRICT {
  SCOPE_profile_cpu_f("gpu");

  // load sequencer instruction memory (pointer-based)
  uint32_t addr_type = reader_.ReadAndSwap<uint32_t>();
  auto shader_type = static_cast<xenos::ShaderType>(addr_type & 0x3);
  uint32_t addr = addr_type & ~0x3;
  uint32_t start_size = reader_.ReadAndSwap<uint32_t>();
  uint32_t start = start_size >> 16;
  uint32_t size_dwords = start_size & 0xFFFF;  // dwords
  assert_true(start == 0);
  trace_writer_.WriteMemoryRead(CpuToGpu(addr), size_dwords * 4);
  auto shader = COMMAND_PROCESSOR::LoadShader(
      shader_type, addr, memory_->TranslatePhysical<uint32_t*>(addr),
      size_dwords);
  switch (shader_type) {
    case xenos::ShaderType::kVertex:
      active_vertex_shader_ = shader;
      break;
    case xenos::ShaderType::kPixel:
      active_pixel_shader_ = shader;
      break;
    default:
      assert_unhandled_case(shader_type);
      return false;
  }
  return true;
}

bool COMMAND_PROCESSOR::ExecutePacketType3_IM_LOAD_IMMEDIATE(
    uint32_t packet, uint32_t count) XE_RESTRICT {
  SCOPE_profile_cpu_f("gpu");

  // load sequencer instruction memory (code embedded in packet)
  uint32_t dword0 = reader_.ReadAndSwap<uint32_t>();
  uint32_t dword1 = reader_.ReadAndSwap<uint32_t>();
  auto shader_type = static_cast<xenos::ShaderType>(dword0);
  uint32_t start_size = dword1;
  uint32_t start = start_size >> 16;
  uint32_t size_dwords = start_size & 0xFFFF;  // dwords
  assert_true(start == 0);
  assert_true(reader_.read_count() >= size_dwords * 4);
  assert_true(count - 2 >= size_dwords);
  auto shader = COMMAND_PROCESSOR::LoadShader(
      shader_type, uint32_t(reader_.read_ptr()),
      reinterpret_cast<uint32_t*>(reader_.read_ptr()), size_dwords);
  switch (shader_type) {
    case xenos::ShaderType::kVertex:
      active_vertex_shader_ = shader;
      break;
    case xenos::ShaderType::kPixel:
      active_pixel_shader_ = shader;
      break;
    default:
      assert_unhandled_case(shader_type);
      return false;
  }
  reader_.AdvanceRead(size_dwords * sizeof(uint32_t));
  return true;
}

/*
        todo: shouldn't this do something?
*/

bool COMMAND_PROCESSOR::ExecutePacketType3_INVALIDATE_STATE(
    uint32_t packet, uint32_t count) XE_RESTRICT {
  // selective invalidation of state pointers
  /*uint32_t mask =*/reader_.ReadAndSwap<uint32_t>();
  // driver_->InvalidateState(mask);
  return true;
}

bool COMMAND_PROCESSOR::ExecutePacketType3_VIZ_QUERY(
    uint32_t packet, uint32_t count) XE_RESTRICT {
  // begin/end initiator for viz query extent processing
  // https://www.google.com/patents/US20050195186
  assert_true(count == 1);

  uint32_t dword0 = reader_.ReadAndSwap<uint32_t>();

  uint32_t id = dword0 & 0x3F;
  uint32_t end = dword0 & 0x100;
  if (!end) {
    // begin a new viz query @ id
    // On hardware this clears the internal state of the scan converter (which
    // is different to the register)
    COMMAND_PROCESSOR::WriteEventInitiator(VIZQUERY_START);
    // XELOGGPU("Begin viz query ID {:02X}", id);
  } else {
    // end the viz query
    COMMAND_PROCESSOR::WriteEventInitiator(VIZQUERY_END);
    // XELOGGPU("End viz query ID {:02X}", id);
    // The scan converter writes the internal result back to the register here.
    // We just fake it and say it was visible in case it is read back.
    if (id < 32) {
      register_file_->values[XE_GPU_REG_PA_SC_VIZ_QUERY_STATUS_0] |= uint32_t(1)
                                                                     << id;
    } else {
      register_file_->values[XE_GPU_REG_PA_SC_VIZ_QUERY_STATUS_1] |=
          uint32_t(1) << (id - 32);
    }
  }

  return true;
}

uint32_t COMMAND_PROCESSOR::ExecutePrimaryBuffer(uint32_t read_index,
                                                 uint32_t write_index) {
  SCOPE_profile_cpu_f("gpu");
#if XE_ENABLE_TRACE_WRITER_INSTRUMENTATION == 1
  // If we have a pending trace stream open it now. That way we ensure we get
  // all commands.
  if (!trace_writer_.is_open() && trace_state_ == TraceState::kStreaming) {
    uint32_t title_id = kernel_state_->GetExecutableModule()
                            ? kernel_state_->GetExecutableModule()->title_id()
                            : 0;
    auto file_name = fmt::format("{:08X}_stream.xtr", title_id);
    auto path = trace_stream_path_ / file_name;
    trace_writer_.Open(path, title_id);
    InitializeTrace();
  }
#endif
  // Adjust pointer base.
  uint32_t start_ptr = primary_buffer_ptr_ + read_index * sizeof(uint32_t);
  start_ptr = (primary_buffer_ptr_ & ~0x1FFFFFFF) | (start_ptr & 0x1FFFFFFF);
  uint32_t end_ptr = primary_buffer_ptr_ + write_index * sizeof(uint32_t);
  end_ptr = (primary_buffer_ptr_ & ~0x1FFFFFFF) | (end_ptr & 0x1FFFFFFF);

  trace_writer_.WritePrimaryBufferStart(start_ptr, write_index - read_index);

  // Execute commands!

  RingBuffer old_reader = reader_;
  new (&reader_) RingBuffer(memory_->TranslatePhysical(primary_buffer_ptr_),
                            primary_buffer_size_);

  reader_.set_read_offset(read_index * sizeof(uint32_t));
  reader_.set_write_offset(write_index * sizeof(uint32_t));
  // prefetch the wraparound range
  // it likely is already in L3 cache, but in a zen system it may be another
  // chiplets l3
  reader_.BeginPrefetchedRead<swcache::PrefetchTag::Level2>(
      GetCurrentRingReadCount());
  do {
    if (!COMMAND_PROCESSOR::ExecutePacket()) {
      // This probably should be fatal - but we're going to continue anyways.
      XELOGE("**** PRIMARY RINGBUFFER: Failed to execute packet.");
      assert_always();
      break;
    }
  } while (reader_.read_count());

  COMMAND_PROCESSOR::OnPrimaryBufferEnd();

  trace_writer_.WritePrimaryBufferEnd();

  reader_ = old_reader;
  return write_index;
}

void COMMAND_PROCESSOR::ExecutePacket(uint32_t ptr, uint32_t count) {
  // Execute commands!
  RingBuffer old_reader = reader_;

  new (&reader_)
      RingBuffer{memory_->TranslatePhysical(ptr), count * sizeof(uint32_t)};

  reader_.set_write_offset(count * sizeof(uint32_t));

  do {
    if (!COMMAND_PROCESSOR::ExecutePacket()) {
      XELOGE("**** ExecutePacket: Failed to execute packet.");
      assert_always();
      break;
    }
  } while (reader_.read_count());
  reader_ = old_reader;
}