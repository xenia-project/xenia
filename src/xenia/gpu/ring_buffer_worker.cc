/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/gpu/ring_buffer_worker.h>

#include <xenia/gpu/gpu-private.h>
#include <xenia/gpu/graphics_driver.h>
#include <xenia/gpu/graphics_system.h>
#include <xenia/gpu/xenos/packets.h>
#include <xenia/gpu/xenos/registers.h>


using namespace xe;
using namespace xe::gpu;
using namespace xe::gpu::xenos;


#define XETRACERB(fmt, ...) if (FLAGS_trace_ring_buffer) XELOGGPU(fmt, ##__VA_ARGS__)


RingBufferWorker::RingBufferWorker(
    GraphicsSystem* graphics_system, Memory* memory) :
    graphics_system_(graphics_system), memory_(memory), driver_(0) {
  write_ptr_index_event_ = CreateEvent(
      NULL, FALSE, FALSE, NULL);

  primary_buffer_ptr_     = 0;
  primary_buffer_size_    = 0;
  read_ptr_index_         = 0;
  read_ptr_update_freq_   = 0;
  read_ptr_writeback_ptr_ = 0;
  write_ptr_index_        = 0;
  write_ptr_max_index_    = 0;

  LARGE_INTEGER perf_counter;
  QueryPerformanceCounter(&perf_counter);
  time_base_ = perf_counter.QuadPart;
  counter_ = 0;
}

RingBufferWorker::~RingBufferWorker() {
  SetEvent(write_ptr_index_event_);
  CloseHandle(write_ptr_index_event_);
}

uint64_t RingBufferWorker::QueryTime() {
  LARGE_INTEGER perf_counter;
  QueryPerformanceCounter(&perf_counter);
  return perf_counter.QuadPart - time_base_;
}

void RingBufferWorker::Initialize(GraphicsDriver* driver,
                                  uint32_t ptr, uint32_t page_count) {
  driver_               = driver;
  primary_buffer_ptr_   = ptr;
  // Not sure this is correct, but it's a way to take the page_count back to
  // the number of bytes allocated by the physical alloc.
  uint32_t original_size = 1 << (0x1C - page_count - 1);
  primary_buffer_size_  = original_size;
  read_ptr_index_       = 0;

  // Tell the driver what to use for translation.
  driver_->set_address_translation(primary_buffer_ptr_ & ~0x1FFFFFFF);
}

void RingBufferWorker::EnableReadPointerWriteBack(uint32_t ptr,
                                                  uint32_t block_size) {
  // CP_RB_RPTR_ADDR Ring Buffer Read Pointer Address 0x70C
  // ptr = RB_RPTR_ADDR, pointer to write back the address to.
  read_ptr_writeback_ptr_ = (primary_buffer_ptr_ & ~0x1FFFFFFF) + ptr;
  // CP_RB_CNTL Ring Buffer Control 0x704
  // block_size = RB_BLKSZ, number of quadwords read between updates of the
  //              read pointer.
  read_ptr_update_freq_ = (uint32_t)pow(2.0, (double)block_size) / 4;
}

void RingBufferWorker::UpdateWritePointer(uint32_t value) {
  write_ptr_max_index_  = MAX(write_ptr_max_index_, value);
  write_ptr_index_      = value;
  SetEvent(write_ptr_index_event_);
}

void RingBufferWorker::Pump() {
  uint8_t* p = memory_->membase();

  if (write_ptr_index_ == 0xBAADF00D ||
      read_ptr_index_ == write_ptr_index_) {
    // Check if the pointer has moved.
    // We wait a short bit here to yield time. Since we are also running the
    // main window display we don't want to pause too long, though.
    const int wait_time_ms = 1;
    if (WaitForSingleObject(write_ptr_index_event_,
                            wait_time_ms) == WAIT_TIMEOUT) {
      return;
    }
  }

  // Bring local so we don't have to worry about them changing out from under
  // us.
  uint32_t write_ptr_index = write_ptr_index_;
  uint32_t write_ptr_max_index = write_ptr_max_index_;
  if (read_ptr_index_ == write_ptr_index) {
    return;
  }

  // Process the new commands.
  XETRACERB("Ring buffer thread work");

  // Execute. Note that we handle wraparound transparently.
  ExecutePrimaryBuffer(read_ptr_index_, write_ptr_index);
  read_ptr_index_ = write_ptr_index;

  // TODO(benvanik): use read_ptr_update_freq_ and only issue after moving
  //     that many indices.
  if (read_ptr_writeback_ptr_) {
    XESETUINT32BE(p + read_ptr_writeback_ptr_, read_ptr_index_);
  }
}

void RingBufferWorker::ExecutePrimaryBuffer(
    uint32_t start_index, uint32_t end_index) {
  // Adjust pointer base.
  uint32_t ptr = primary_buffer_ptr_ + start_index * 4;
  ptr = (primary_buffer_ptr_ & ~0x1FFFFFFF) | (ptr & 0x1FFFFFFF);
  uint32_t end_ptr = primary_buffer_ptr_ + end_index * 4;
  end_ptr = (primary_buffer_ptr_ & ~0x1FFFFFFF) | (end_ptr & 0x1FFFFFFF);

  XETRACERB("[%.8X] ExecutePrimaryBuffer(%dw -> %dw)",
            ptr, start_index, end_index);

  // Execute commands!
  PacketArgs args;
  args.ptr          = ptr;
  args.base_ptr     = primary_buffer_ptr_;
  args.max_address  = primary_buffer_ptr_ + primary_buffer_size_ * 4;
  args.ptr_mask     = (primary_buffer_size_ / 4) - 1;
  uint32_t n = 0;
  while (args.ptr != end_ptr) {
    n += ExecutePacket(args);
  }
  if (end_index > start_index) {
    XEASSERT(n == (end_index - start_index));
  }

  XETRACERB("           ExecutePrimaryBuffer End");
}

void RingBufferWorker::ExecuteIndirectBuffer(uint32_t ptr, uint32_t length) {
  XETRACERB("[%.8X] ExecuteIndirectBuffer(%dw)", ptr, length);

  // Execute commands!
  PacketArgs args;
  args.ptr          = ptr;
  args.base_ptr     = ptr;
  args.max_address  = ptr + length * 4;
  args.ptr_mask     = 0;
  for (uint32_t n = 0; n < length;) {
    n += ExecutePacket(args);
    XEASSERT(n <= length);
  }

  XETRACERB("           ExecuteIndirectBuffer End");
}

#define LOG_DATA(count) \
  for (uint32_t __m = 0; __m < count; __m++) { \
    XETRACERB("[%.8X]   %.8X", \
              packet_ptr + (1 + __m) * 4, \
              XEGETUINT32BE(packet_base + 1 * 4 + __m * 4)); \
  }

void RingBufferWorker::AdvancePtr(PacketArgs& args, uint32_t n) {
  args.ptr = args.ptr + n * 4;
  if (args.ptr_mask) {
    args.ptr =
        args.base_ptr + (((args.ptr - args.base_ptr) / 4) & args.ptr_mask) * 4;
  }
}
#define ADVANCE_PTR(n) AdvancePtr(args, n)
#define PEEK_PTR() \
    XEGETUINT32BE(p + args.ptr)
#define READ_PTR() \
    XEGETUINT32BE(p + args.ptr); ADVANCE_PTR(1);

uint32_t RingBufferWorker::ExecutePacket(PacketArgs& args) {
  uint8_t* p = memory_->membase();
  RegisterFile* regs = driver_->register_file();

  uint32_t packet_ptr = args.ptr;
  const uint8_t* packet_base = p + packet_ptr;
  const uint32_t packet = PEEK_PTR();
  ADVANCE_PTR(1);
  const uint32_t packet_type = packet >> 30;
  if (packet == 0) {
    XETRACERB("[%.8X] Packet(%.8X): 0?",
              packet_ptr, packet);
    return 1;
  }

  switch (packet_type) {
  case 0x00:
    {
      // Type-0 packet.
      // Write count registers in sequence to the registers starting at
      // (base_index << 2).
      XETRACERB("[%.8X] Packet(%.8X): set registers:",
                packet_ptr, packet);
      uint32_t count = ((packet >> 16) & 0x3FFF) + 1;
      uint32_t base_index = (packet & 0x7FFF);
      uint32_t write_one_reg = (packet >> 15) & 0x1;
      for (uint32_t m = 0; m < count; m++) {
        uint32_t reg_data = PEEK_PTR();
        uint32_t target_index = write_one_reg ? base_index : base_index + m;
        const char* reg_name = xenos::GetRegisterName(target_index);
        XETRACERB("[%.8X]   %.8X -> %.4X %s",
                  args.ptr,
                  reg_data, target_index, reg_name ? reg_name : "");
        ADVANCE_PTR(1);
        WriteRegister(packet_ptr, target_index, reg_data);
      }
      return 1 + count;
    }
    break;
  case 0x01:
    {
      // Type-1 packet.
      // Contains two registers of data. Type-0 should be more common.
      XETRACERB("[%.8X] Packet(%.8X): set registers:",
                packet_ptr, packet);
      uint32_t reg_index_1 = packet & 0x7FF;
      uint32_t reg_index_2 = (packet >> 11) & 0x7FF;
      uint32_t reg_ptr_1 = args.ptr;
      uint32_t reg_data_1 = READ_PTR();
      uint32_t reg_ptr_2 = args.ptr;
      uint32_t reg_data_2 = READ_PTR();
      const char* reg_name_1 = xenos::GetRegisterName(reg_index_1);
      const char* reg_name_2 = xenos::GetRegisterName(reg_index_2);
      XETRACERB("[%.8X]   %.8X -> %.4X %s",
                reg_ptr_1,
                reg_data_1, reg_index_1, reg_name_1 ? reg_name_1 : "");
      XETRACERB("[%.8X]   %.8X -> %.4X %s",
                reg_ptr_2,
                reg_data_2, reg_index_2, reg_name_2 ? reg_name_2 : "");
      WriteRegister(packet_ptr, reg_index_1, reg_data_1);
      WriteRegister(packet_ptr, reg_index_2, reg_data_2);
      return 1 + 2;
    }
    break;
  case 0x02:
    // Type-2 packet.
    // No-op. Do nothing.
    XETRACERB("[%.8X] Packet(%.8X): padding",
              packet_ptr, packet);
    return 1;
  case 0x03:
    {
      // Type-3 packet.
      uint32_t count = ((packet >> 16) & 0x3FFF) + 1;
      uint32_t opcode = (packet >> 8) & 0x7F;
      // & 1 == predicate, maybe?

      switch (opcode) {
      case PM4_ME_INIT:
        // initialize CP's micro-engine
        XETRACERB("[%.8X] Packet(%.8X): PM4_ME_INIT",
                  packet_ptr, packet);
        LOG_DATA(count);
        ADVANCE_PTR(count);
        break;

      case PM4_NOP:
        // skip N 32-bit words to get to the next packet
        // No-op, ignore some data.
        XETRACERB("[%.8X] Packet(%.8X): PM4_NOP",
                  packet_ptr, packet);
        LOG_DATA(count);
        ADVANCE_PTR(count);
        break;

      case PM4_INTERRUPT:
        // generate interrupt from the command stream
        {
          XETRACERB("[%.8X] Packet(%.8X): PM4_INTERRUPT",
                    packet_ptr, packet);
          LOG_DATA(count);
          uint32_t cpu_mask = READ_PTR();
          for (int n = 0; n < 6; n++) {
            if (cpu_mask & (1 << n)) {
              graphics_system_->DispatchInterruptCallback(1, n);
            }
          }
        }
        break;

      case PM4_INDIRECT_BUFFER:
        // indirect buffer dispatch
        {
          uint32_t list_ptr = READ_PTR();
          uint32_t list_length = READ_PTR();
          XETRACERB("[%.8X] Packet(%.8X): PM4_INDIRECT_BUFFER %.8X (%dw)",
                    packet_ptr, packet, list_ptr, list_length);
          ExecuteIndirectBuffer(GpuToCpu(list_ptr), list_length);
        }
        break;

      case PM4_WAIT_REG_MEM:
        // wait until a register or memory location is a specific value
        {
          XETRACERB("[%.8X] Packet(%.8X): PM4_WAIT_REG_MEM",
                    packet_ptr, packet);
          LOG_DATA(count);
          uint32_t wait_info = READ_PTR();
          uint32_t poll_reg_addr = READ_PTR();
          uint32_t ref = READ_PTR();
          uint32_t mask = READ_PTR();
          uint32_t wait = READ_PTR();
          bool matched = false;
          do {
            uint32_t value;
            if (wait_info & 0x10) {
              // Memory.
              XE_GPU_ENDIAN endianness = (XE_GPU_ENDIAN)(poll_reg_addr & 0x3);
              poll_reg_addr &= ~0x3;
              value = XEGETUINT32LE(p + GpuToCpu(packet_ptr, poll_reg_addr));
              value = GpuSwap(value, endianness);
            } else {
              // Register.
              XEASSERT(poll_reg_addr < kXEGpuRegisterCount);
              value = regs->values[poll_reg_addr].u32;
            }
            switch (wait_info & 0x7) {
            case 0x0: // Never.
              matched = false;
              break;
            case 0x1: // Less than reference.
              matched = (value & mask) < ref;
              break;
            case 0x2: // Less than or equal to reference.
              matched = (value & mask) <= ref;
              break;
            case 0x3: // Equal to reference.
              matched = (value & mask) == ref;
              break;
            case 0x4: // Not equal to reference.
              matched = (value & mask) != ref;
              break;
            case 0x5: // Greater than or equal to reference.
              matched = (value & mask) >= ref;
              break;
            case 0x6: // Greater than reference.
              matched = (value & mask) > ref;
              break;
            case 0x7: // Always
              matched = true;
              break;
            }
            if (!matched) {
              // Wait.
              if (wait >= 0x100) {
                Sleep(wait / 0x100);
              } else {
                SwitchToThread();
              }
            }
          } while (!matched);
        }
        break;

      case PM4_REG_RMW:
        // register read/modify/write
        // ? (used during shader upload and edram setup)
        {
          XETRACERB("[%.8X] Packet(%.8X): PM4_REG_RMW",
                    packet_ptr, packet);
          LOG_DATA(count);
          uint32_t rmw_info = READ_PTR();
          uint32_t and_mask = READ_PTR();
          uint32_t or_mask = READ_PTR();
          uint32_t value = regs->values[rmw_info & 0x1FFF].u32;
          if ((rmw_info >> 30) & 0x1) {
            // | reg
            value |= regs->values[or_mask & 0x1FFF].u32;
          } else {
            // | imm
            value |= or_mask;
          }
          if ((rmw_info >> 31) & 0x1) {
            // & reg
            value &= regs->values[and_mask & 0x1FFF].u32;
          } else {
            // & imm
            value &= and_mask;
          }
          WriteRegister(packet_ptr, rmw_info & 0x1FFF, value);
        }
        break;

      case PM4_COND_WRITE:
        // conditional write to memory or register
        {
          XETRACERB("[%.8X] Packet(%.8X): PM4_COND_WRITE",
                    packet_ptr, packet);
          LOG_DATA(count);
          uint32_t wait_info = READ_PTR();
          uint32_t poll_reg_addr = READ_PTR();
          uint32_t ref = READ_PTR();
          uint32_t mask = READ_PTR();
          uint32_t write_reg_addr = READ_PTR();
          uint32_t write_data = READ_PTR();
          uint32_t value;
          if (wait_info & 0x10) {
            // Memory.
            XE_GPU_ENDIAN endianness = (XE_GPU_ENDIAN)(poll_reg_addr & 0x3);
            poll_reg_addr &= ~0x3;
            value = XEGETUINT32LE(p + GpuToCpu(packet_ptr, poll_reg_addr));
            value = GpuSwap(value, endianness);
          } else {
            // Register.
            XEASSERT(poll_reg_addr < kXEGpuRegisterCount);
            value = regs->values[poll_reg_addr].u32;
          }
          bool matched = false;
          switch (wait_info & 0x7) {
          case 0x0: // Never.
            matched = false;
            break;
          case 0x1: // Less than reference.
            matched = (value & mask) < ref;
            break;
          case 0x2: // Less than or equal to reference.
            matched = (value & mask) <= ref;
            break;
          case 0x3: // Equal to reference.
            matched = (value & mask) == ref;
            break;
          case 0x4: // Not equal to reference.
            matched = (value & mask) != ref;
            break;
          case 0x5: // Greater than or equal to reference.
            matched = (value & mask) >= ref;
            break;
          case 0x6: // Greater than reference.
            matched = (value & mask) > ref;
            break;
          case 0x7: // Always
            matched = true;
            break;
          }
          if (matched) {
            // Write.
            if (wait_info & 0x100) {
              // Memory.
              XE_GPU_ENDIAN endianness = (XE_GPU_ENDIAN)(write_reg_addr & 0x3);
              write_reg_addr &= ~0x3;
              write_data = GpuSwap(write_data, endianness);
              XESETUINT32LE(p + GpuToCpu(packet_ptr, write_reg_addr),
                            write_data);
            } else {
              // Register.
              WriteRegister(packet_ptr, write_reg_addr, write_data);
            }
          }
        }
        break;

      case PM4_EVENT_WRITE:
        // generate an event that creates a write to memory when completed
        {
          XETRACERB("[%.8X] Packet(%.8X): PM4_EVENT_WRITE (unimplemented!)",
                    packet_ptr, packet);
          LOG_DATA(count);
          uint32_t initiator = READ_PTR();
          if (count == 1) {
            // Just an event flag? Where does this write?
          } else {
            // Write to an address.
            XEASSERTALWAYS();
            ADVANCE_PTR(count - 1);
          }
        }
        break;
      case PM4_EVENT_WRITE_SHD:
        // generate a VS|PS_done event
        {
          XETRACERB("[%.8X] Packet(%.8X): PM4_EVENT_WRITE_SHD",
                    packet_ptr, packet);
          LOG_DATA(count);
          uint32_t initiator = READ_PTR();
          uint32_t address = READ_PTR();
          uint32_t value = READ_PTR();
          // Writeback initiator.
          WriteRegister(packet_ptr, XE_GPU_REG_VGT_EVENT_INITIATOR,
                        initiator & 0x1F);
          uint32_t data_value;
          if ((initiator >> 31) & 0x1) {
            // Write counter (GPU vblank counter?).
            data_value = counter_;
          } else {
            // Write value.
            data_value = value;
          }
          XE_GPU_ENDIAN endianness = (XE_GPU_ENDIAN)(address & 0x3);
          address &= ~0x3;
          data_value = GpuSwap(data_value, endianness);
          XESETUINT32LE(p + GpuToCpu(packet_ptr, address), data_value);
        }
        break;

      case PM4_DRAW_INDX:
        // initiate fetch of index buffer and draw
        {
          XETRACERB("[%.8X] Packet(%.8X): PM4_DRAW_INDX",
                    packet_ptr, packet);
          LOG_DATA(count);
          // d0 = viz query info
          uint32_t d0 = READ_PTR();
          uint32_t d1 = READ_PTR();
          uint32_t index_count = d1 >> 16;
          uint32_t prim_type = d1 & 0x3F;
          uint32_t src_sel = (d1 >> 6) & 0x3;
          if (src_sel == 0x0) {
            uint32_t index_base = READ_PTR();
            uint32_t index_size = READ_PTR();
            uint32_t endianness = index_size >> 29;
            index_size &= 0x00FFFFFF;
            bool index_32bit = (d1 >> 11) & 0x1;
            index_size *= index_32bit ? 4 : 2;
            driver_->DrawIndexBuffer(
                (XE_GPU_PRIMITIVE_TYPE)prim_type,
                index_32bit, index_count, index_base, index_size, endianness);
          } else if (src_sel == 0x2) {
            driver_->DrawIndexAuto(
                (XE_GPU_PRIMITIVE_TYPE)prim_type,
                index_count);
          } else {
            // Unknown source select.
            XEASSERTALWAYS();
          }
        }
        break;
      case PM4_DRAW_INDX_2:
        // draw using supplied indices in packet
        {
          XETRACERB("[%.8X] Packet(%.8X): PM4_DRAW_INDX_2",
                    packet_ptr, packet);
          LOG_DATA(count);
          uint32_t d0 = READ_PTR();
          uint32_t index_count = d0 >> 16;
          uint32_t prim_type = d0 & 0x3F;
          uint32_t src_sel = (d0 >> 6) & 0x3;
          XEASSERT(src_sel == 0x2); // 'SrcSel=AutoIndex'
          driver_->DrawIndexAuto(
              (XE_GPU_PRIMITIVE_TYPE)prim_type,
              index_count);
        }
        break;

      case PM4_SET_CONSTANT:
        // load constant into chip and to memory
        {
          XETRACERB("[%.8X] Packet(%.8X): PM4_SET_CONSTANT",
                    packet_ptr, packet);
          // PM4_REG(reg) ((0x4 << 16) | (GSL_HAL_SUBBLOCK_OFFSET(reg)))
          //                                     reg - 0x2000
          uint32_t offset_type = READ_PTR();
          uint32_t index = offset_type & 0x7FF;
          uint32_t type = (offset_type >> 16) & 0xFF;
          switch (type) {
          case 0x4: // REGISTER
            index += 0x2000; // registers
            for (uint32_t n = 0; n < count - 1; n++, index++) {
              uint32_t data = READ_PTR();
              const char* reg_name = xenos::GetRegisterName(index);
              XETRACERB("[%.8X]   %.8X -> %.4X %s",
                        packet_ptr + (1 + n) * 4,
                        data, index, reg_name ? reg_name : "");
              WriteRegister(packet_ptr, index, data);
            }
            break;
          default:
            XEASSERTALWAYS();
            break;
          }
        }
        break;
      case PM4_LOAD_ALU_CONSTANT:
        // load constants from memory
        {
          XETRACERB("[%.8X] Packet(%.8X): PM4_LOAD_ALU_CONSTANT",
                    packet_ptr, packet);
          uint32_t address = READ_PTR();
          address &= 0x3FFFFFFF;
          uint32_t offset_type = READ_PTR();
          uint32_t index = offset_type & 0x7FF;
          uint32_t size = READ_PTR();
          size &= 0xFFF;
          index += 0x4000; // alu constants
          for (uint32_t n = 0; n < size; n++, index++) {
            uint32_t data = XEGETUINT32BE(
                p + GpuToCpu(packet_ptr, address + n * 4));
            const char* reg_name = xenos::GetRegisterName(index);
            XETRACERB("[%.8X]   %.8X -> %.4X %s",
                      packet_ptr,
                      data, index, reg_name ? reg_name : "");
            WriteRegister(packet_ptr, index, data);
          }
        }
        break;

      case PM4_IM_LOAD:
        // load sequencer instruction memory (pointer-based)
        {
          XETRACERB("[%.8X] Packet(%.8X): PM4_IM_LOAD",
                    packet_ptr, packet);
          LOG_DATA(count);
          uint32_t addr_type = READ_PTR();
          uint32_t type = addr_type & 0x3;
          uint32_t addr = addr_type & ~0x3;
          uint32_t start_size = READ_PTR();
          uint32_t start = start_size >> 16;
          uint32_t size = start_size & 0xFFFF; // dwords
          XEASSERT(start == 0);
          driver_->SetShader(
              (XE_GPU_SHADER_TYPE)type,
              GpuToCpu(packet_ptr, addr),
              start,
              size * 4);
        }
        break;
      case PM4_IM_LOAD_IMMEDIATE:
        // load sequencer instruction memory (code embedded in packet)
        {
          XETRACERB("[%.8X] Packet(%.8X): PM4_IM_LOAD_IMMEDIATE",
                    packet_ptr, packet);
          LOG_DATA(count);
          uint32_t type = READ_PTR();
          uint32_t start_size = READ_PTR();
          uint32_t start = start_size >> 16;
          uint32_t size = start_size & 0xFFFF; // dwords
          XEASSERT(start == 0);
          // TODO(benvanik): figure out if this could wrap.
          XEASSERT(args.ptr + size * 4 < args.max_address);
          driver_->SetShader(
              (XE_GPU_SHADER_TYPE)type,
              args.ptr,
              start,
              size * 4);
          ADVANCE_PTR(size);
        }
        break;

      case PM4_INVALIDATE_STATE:
        // selective invalidation of state pointers
        {
          XETRACERB("[%.8X] Packet(%.8X): PM4_INVALIDATE_STATE",
                    packet_ptr, packet);
          LOG_DATA(count);
          uint32_t mask = READ_PTR();
          driver_->InvalidateState(mask);
        }
        break;

      case PM4_SET_BIN_MASK_LO:
        {
          uint32_t value = READ_PTR();
          XETRACERB("[%.8X] Packet(%.8X): PM4_SET_BIN_MASK_LO = %.8X",
                    packet_ptr, packet, value);
        }
        break;
      case PM4_SET_BIN_MASK_HI:
        {
          uint32_t value = READ_PTR();
          XETRACERB("[%.8X] Packet(%.8X): PM4_SET_BIN_MASK_HI = %.8X",
                    packet_ptr, packet, value);
        }
        break;
      case PM4_SET_BIN_SELECT_LO:
        {
          uint32_t value = READ_PTR();
          XETRACERB("[%.8X] Packet(%.8X): PM4_SET_BIN_SELECT_LO = %.8X",
                    packet_ptr, packet, value);
        }
        break;
      case PM4_SET_BIN_SELECT_HI:
        {
          uint32_t value = READ_PTR();
          XETRACERB("[%.8X] Packet(%.8X): PM4_SET_BIN_SELECT_HI = %.8X",
                    packet_ptr, packet, value);
        }
        break;

      // Ignored packets - useful if breaking on the default handler below.
      case 0x50: // 0xC0015000 usually 2 words, 0xFFFFFFFF / 0x00000000
        XETRACERB("[%.8X] Packet(%.8X): unknown!",
                  packet_ptr, packet);
        LOG_DATA(count);
        ADVANCE_PTR(count);
        break;

      default:
        XETRACERB("[%.8X] Packet(%.8X): unknown!",
                  packet_ptr, packet);
        LOG_DATA(count);
        ADVANCE_PTR(count);
        break;
      }

      return 1 + count;
    }
    break;
  }

  return 0;
}

void RingBufferWorker::WriteRegister(
    uint32_t packet_ptr, uint32_t index, uint32_t value) {
  RegisterFile* regs = driver_->register_file();
  XEASSERT(index < kXEGpuRegisterCount);
  regs->values[index].u32 = value;

  // Scratch register writeback.
  if (index >= XE_GPU_REG_SCRATCH_REG0 && index <= XE_GPU_REG_SCRATCH_REG7) {
    uint32_t scratch_reg = index - XE_GPU_REG_SCRATCH_REG0;
    if ((1 << scratch_reg) & regs->values[XE_GPU_REG_SCRATCH_UMSK].u32) {
      // Enabled - write to address.
      uint8_t* p = memory_->membase();
      uint32_t scratch_addr = regs->values[XE_GPU_REG_SCRATCH_ADDR].u32;
      uint32_t mem_addr = scratch_addr + (scratch_reg * 4);
      XESETUINT32BE(p + GpuToCpu(primary_buffer_ptr_, mem_addr), value);
    }
  }
}
