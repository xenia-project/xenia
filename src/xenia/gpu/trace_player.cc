/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/trace_player.h"

#include <memory>

#include "xenia/gpu/command_processor.h"
#include "xenia/gpu/graphics_system.h"
#include "xenia/gpu/registers.h"
#include "xenia/gpu/xenos.h"
#include "xenia/memory.h"

namespace xe {
namespace gpu {

TracePlayer::TracePlayer(GraphicsSystem* graphics_system)
    : graphics_system_(graphics_system),
      current_frame_index_(0),
      current_command_index_(-1) {
  // Need to allocate all of physical memory so that we can write to it during
  // playback. The 64 KB page heap is larger, covers the entire physical memory,
  // so it is used instead of the 4 KB page one.
  auto heap = graphics_system_->memory()->LookupHeapByType(true, 64 * 1024);
  heap->AllocFixed(heap->heap_base(), heap->heap_size(), heap->page_size(),
                   kMemoryAllocationReserve | kMemoryAllocationCommit,
                   kMemoryProtectRead | kMemoryProtectWrite);

  playback_event_ = xe::threading::Event::CreateAutoResetEvent(false);
  assert_not_null(playback_event_);
}

const TraceReader::Frame* TracePlayer::current_frame() const {
  if (current_frame_index_ >= frame_count()) {
    return nullptr;
  }
  return frame(current_frame_index_);
}

void TracePlayer::SeekFrame(int target_frame) {
  if (current_frame_index_ == target_frame) {
    return;
  }
  current_frame_index_ = target_frame;
  auto frame = current_frame();
  current_command_index_ = int(frame->commands.size()) - 1;

  assert_true(frame->start_ptr <= frame->end_ptr);
  PlayTrace(frame->start_ptr, frame->end_ptr - frame->start_ptr,
            TracePlaybackMode::kBreakOnSwap, false);
}

void TracePlayer::SeekCommand(int target_command) {
  if (current_command_index_ == target_command) {
    return;
  }
  int previous_command_index = current_command_index_;
  current_command_index_ = target_command;
  if (current_command_index_ == -1) {
    return;
  }
  auto frame = current_frame();
  const auto& command = frame->commands[target_command];
  assert_true(frame->start_ptr <= command.end_ptr);
  if (previous_command_index != -1 && target_command > previous_command_index) {
    // Seek forward.
    const auto& previous_command = frame->commands[previous_command_index];
    PlayTrace(previous_command.end_ptr,
              command.end_ptr - previous_command.end_ptr,
              TracePlaybackMode::kBreakOnSwap, false);
  } else {
    // Full playback from frame start.
    PlayTrace(frame->start_ptr, command.end_ptr - frame->start_ptr,
              TracePlaybackMode::kBreakOnSwap, true);
  }
}

void TracePlayer::WaitOnPlayback() {
  xe::threading::Wait(playback_event_.get(), true);
}

void TracePlayer::PlayTrace(const uint8_t* trace_data, size_t trace_size,
                            TracePlaybackMode playback_mode,
                            bool clear_caches) {
  playing_trace_ = true;
  graphics_system_->command_processor()->CallInThread([=, this]() {
    PlayTraceOnThread(trace_data, trace_size, playback_mode, clear_caches);
  });
}

void TracePlayer::PlayTraceOnThread(const uint8_t* trace_data,
                                    size_t trace_size,
                                    TracePlaybackMode playback_mode,
                                    bool clear_caches) {
  auto memory = graphics_system_->memory();
  auto command_processor = graphics_system_->command_processor();

  if (clear_caches) {
    command_processor->ClearCaches();
  }

  playback_percent_ = 0;
  auto trace_end = trace_data + trace_size;

  playing_trace_ = true;
  auto trace_ptr = trace_data;
  bool pending_break = false;
  const PacketStartCommand* pending_packet = nullptr;
  while (trace_ptr < trace_data + trace_size) {
    playback_percent_ = uint32_t(
        (float(trace_ptr - trace_data) / float(trace_end - trace_data)) *
        10000);

    auto type = static_cast<TraceCommandType>(xe::load<uint32_t>(trace_ptr));
    switch (type) {
      case TraceCommandType::kPrimaryBufferStart: {
        auto cmd =
            reinterpret_cast<const PrimaryBufferStartCommand*>(trace_ptr);
        //
        trace_ptr += sizeof(*cmd) + cmd->count * 4;
        break;
      }
      case TraceCommandType::kPrimaryBufferEnd: {
        auto cmd = reinterpret_cast<const PrimaryBufferEndCommand*>(trace_ptr);
        //
        trace_ptr += sizeof(*cmd);
        break;
      }
      case TraceCommandType::kIndirectBufferStart: {
        auto cmd =
            reinterpret_cast<const IndirectBufferStartCommand*>(trace_ptr);
        //
        trace_ptr += sizeof(*cmd) + cmd->count * 4;
        break;
      }
      case TraceCommandType::kIndirectBufferEnd: {
        auto cmd = reinterpret_cast<const IndirectBufferEndCommand*>(trace_ptr);
        //
        trace_ptr += sizeof(*cmd);
        break;
      }
      case TraceCommandType::kPacketStart: {
        auto cmd = reinterpret_cast<const PacketStartCommand*>(trace_ptr);
        trace_ptr += sizeof(*cmd);
        std::memcpy(memory->TranslatePhysical(cmd->base_ptr), trace_ptr,
                    cmd->count * 4);
        trace_ptr += cmd->count * 4;
        pending_packet = cmd;
        break;
      }
      case TraceCommandType::kPacketEnd: {
        auto cmd = reinterpret_cast<const PacketEndCommand*>(trace_ptr);
        trace_ptr += sizeof(*cmd);
        if (pending_packet) {
          command_processor->ExecutePacket(pending_packet->base_ptr,
                                           pending_packet->count);
          pending_packet = nullptr;
        }
        if (pending_break) {
          playing_trace_ = false;
          return;
        }
        break;
      }
      case TraceCommandType::kMemoryRead: {
        auto cmd = reinterpret_cast<const MemoryCommand*>(trace_ptr);
        trace_ptr += sizeof(*cmd);
        DecompressMemory(cmd->encoding_format, trace_ptr, cmd->encoded_length,
                         memory->TranslatePhysical(cmd->base_ptr),
                         cmd->decoded_length);
        trace_ptr += cmd->encoded_length;
        command_processor->TracePlaybackWroteMemory(cmd->base_ptr,
                                                    cmd->decoded_length);
        break;
      }
      case TraceCommandType::kMemoryWrite: {
        auto cmd = reinterpret_cast<const MemoryCommand*>(trace_ptr);
        trace_ptr += sizeof(*cmd);
        // ?
        // Assuming the command processor will do the same write.
        trace_ptr += cmd->encoded_length;
        break;
      }
      case TraceCommandType::kEdramSnapshot: {
        auto cmd = reinterpret_cast<const EdramSnapshotCommand*>(trace_ptr);
        trace_ptr += sizeof(*cmd);
        std::unique_ptr<uint8_t[]> edram_snapshot(
            new uint8_t[xenos::kEdramSizeBytes]);
        DecompressMemory(cmd->encoding_format, trace_ptr, cmd->encoded_length,
                         edram_snapshot.get(), xenos::kEdramSizeBytes);
        trace_ptr += cmd->encoded_length;
        command_processor->RestoreEdramSnapshot(edram_snapshot.get());
        break;
      }
      case TraceCommandType::kEvent: {
        auto cmd = reinterpret_cast<const EventCommand*>(trace_ptr);
        trace_ptr += sizeof(*cmd);
        switch (cmd->event_type) {
          case EventCommand::Type::kSwap: {
            if (playback_mode == TracePlaybackMode::kBreakOnSwap) {
              pending_break = true;
            }
            break;
          }
        }
        break;
      }
      case TraceCommandType::kRegisters: {
        auto cmd = reinterpret_cast<const RegistersCommand*>(trace_ptr);
        trace_ptr += sizeof(*cmd);
        std::unique_ptr<uint32_t[]> register_values(
            new uint32_t[cmd->register_count]);
        DecompressMemory(cmd->encoding_format, trace_ptr, cmd->encoded_length,
                         register_values.get(),
                         sizeof(uint32_t) * cmd->register_count);
        trace_ptr += cmd->encoded_length;
        command_processor->RestoreRegisters(
            cmd->first_register, register_values.get(), cmd->register_count,
            cmd->execute_callbacks);
        break;
      }
      case TraceCommandType::kGammaRamp: {
        auto cmd = reinterpret_cast<const GammaRampCommand*>(trace_ptr);
        trace_ptr += sizeof(*cmd);
        std::unique_ptr<uint32_t[]> gamma_ramps(new uint32_t[256 + 3 * 128]);
        DecompressMemory(cmd->encoding_format, trace_ptr, cmd->encoded_length,
                         gamma_ramps.get(), sizeof(uint32_t) * (256 + 3 * 128));
        trace_ptr += cmd->encoded_length;
        command_processor->RestoreGammaRamp(
            reinterpret_cast<const reg::DC_LUT_30_COLOR*>(gamma_ramps.get()),
            reinterpret_cast<const reg::DC_LUT_PWL_DATA*>(gamma_ramps.get() +
                                                          256),
            cmd->rw_component);
        break;
      }
    }
  }

  playing_trace_ = false;

  playback_event_->Set();
}

}  // namespace gpu
}  // namespace xe
