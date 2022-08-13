/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/trace_writer.h"

#include <cstring>
#include <memory>

#include "third_party/snappy/snappy-sinksource.h"
#include "third_party/snappy/snappy.h"

#include "build/version.h"
#include "xenia/base/assert.h"
#include "xenia/base/filesystem.h"
#include "xenia/base/logging.h"
#include "xenia/base/string.h"
#include "xenia/gpu/registers.h"
#include "xenia/gpu/xenos.h"

namespace xe {
namespace gpu {
#if XE_ENABLE_TRACE_WRITER_INSTRUMENTATION == 1
TraceWriter::TraceWriter(uint8_t* membase)
    : membase_(membase), file_(nullptr) {}

TraceWriter::~TraceWriter() = default;

bool TraceWriter::Open(const std::filesystem::path& path, uint32_t title_id) {
  Close();

  auto canonical_path = std::filesystem::absolute(path);
  if (canonical_path.has_parent_path()) {
    auto base_path = canonical_path.parent_path();
    std::filesystem::create_directories(base_path);
  }

  file_ = xe::filesystem::OpenFile(canonical_path, "wb");
  if (!file_) {
    return false;
  }

  // Write header first. Must be at the top of the file.
  TraceHeader header;
  header.version = kTraceFormatVersion;
  std::memcpy(header.build_commit_sha, XE_BUILD_COMMIT,
              sizeof(header.build_commit_sha));
  header.title_id = title_id;
  fwrite(&header, sizeof(header), 1, file_);

  cached_memory_reads_.clear();
  return true;
}

void TraceWriter::Flush() {
  if (file_) {
    fflush(file_);
  }
}

void TraceWriter::Close() {
  if (file_) {
    cached_memory_reads_.clear();

    fflush(file_);
    fclose(file_);
    file_ = nullptr;
  }
}

void TraceWriter::WritePrimaryBufferStart(uint32_t base_ptr, uint32_t count) {
  if (!file_) {
    return;
  }
  PrimaryBufferStartCommand cmd = {
      TraceCommandType::kPrimaryBufferStart,
      base_ptr,
      0,
  };
  fwrite(&cmd, 1, sizeof(cmd), file_);
}

void TraceWriter::WritePrimaryBufferEnd() {
  if (!file_) {
    return;
  }
  PrimaryBufferEndCommand cmd = {
      TraceCommandType::kPrimaryBufferEnd,
  };
  fwrite(&cmd, 1, sizeof(cmd), file_);
}

void TraceWriter::WriteIndirectBufferStart(uint32_t base_ptr, uint32_t count) {
  if (!file_) {
    return;
  }
  IndirectBufferStartCommand cmd = {
      TraceCommandType::kIndirectBufferStart,
      base_ptr,
      0,
  };
  fwrite(&cmd, 1, sizeof(cmd), file_);
}

void TraceWriter::WriteIndirectBufferEnd() {
  if (!file_) {
    return;
  }
  IndirectBufferEndCommand cmd = {
      TraceCommandType::kIndirectBufferEnd,
  };
  fwrite(&cmd, 1, sizeof(cmd), file_);
}

void TraceWriter::WritePacketStart(uint32_t base_ptr, uint32_t count) {
  if (!file_) {
    return;
  }
  PacketStartCommand cmd = {
      TraceCommandType::kPacketStart,
      base_ptr,
      count,
  };
  fwrite(&cmd, 1, sizeof(cmd), file_);
  fwrite(membase_ + base_ptr, 4, count, file_);
}

void TraceWriter::WritePacketEnd() {
  if (!file_) {
    return;
  }
  PacketEndCommand cmd = {
      TraceCommandType::kPacketEnd,
  };
  fwrite(&cmd, 1, sizeof(cmd), file_);
}

void TraceWriter::WriteMemoryRead(uint32_t base_ptr, size_t length,
                                  const void* host_ptr) {
  if (!file_) {
    return;
  }
  WriteMemoryCommand(TraceCommandType::kMemoryRead, base_ptr, length, host_ptr);
}

void TraceWriter::WriteMemoryReadCached(uint32_t base_ptr, size_t length) {
  if (!file_) {
    return;
  }

  // HACK: length is guaranteed to be within 32-bits (guest memory)
  uint64_t key = uint64_t(base_ptr) << 32 | uint64_t(length);
  if (cached_memory_reads_.find(key) == cached_memory_reads_.end()) {
    WriteMemoryCommand(TraceCommandType::kMemoryRead, base_ptr, length);
    cached_memory_reads_.insert(key);
  }
}

void TraceWriter::WriteMemoryReadCachedNop(uint32_t base_ptr, size_t length) {
  if (!file_) {
    return;
  }

  // HACK: length is guaranteed to be within 32-bits (guest memory)
  uint64_t key = uint64_t(base_ptr) << 32 | uint64_t(length);
  if (cached_memory_reads_.find(key) == cached_memory_reads_.end()) {
    cached_memory_reads_.insert(key);
  }
}

void TraceWriter::WriteMemoryWrite(uint32_t base_ptr, size_t length,
                                   const void* host_ptr) {
  if (!file_) {
    return;
  }
  WriteMemoryCommand(TraceCommandType::kMemoryWrite, base_ptr, length,
                     host_ptr);
}

class SnappySink : public snappy::Sink {
 public:
  SnappySink(FILE* file) : file_(file) {}

  void Append(const char* bytes, size_t n) override {
    fwrite(bytes, 1, n, file_);
  }

 private:
  FILE* file_ = nullptr;
};

void TraceWriter::WriteMemoryCommand(TraceCommandType type, uint32_t base_ptr,
                                     size_t length, const void* host_ptr) {
  MemoryCommand cmd = {};
  cmd.type = type;
  cmd.base_ptr = base_ptr;
  cmd.encoding_format = MemoryEncodingFormat::kNone;
  cmd.encoded_length = cmd.decoded_length = static_cast<uint32_t>(length);

  if (!host_ptr) {
    host_ptr = membase_ + cmd.base_ptr;
  }

  bool compress = compress_output_ && length > compression_threshold_;
  if (compress) {
    // Write the header now so we reserve space in the buffer.
    long header_position = std::ftell(file_);
    cmd.encoding_format = MemoryEncodingFormat::kSnappy;
    fwrite(&cmd, 1, sizeof(cmd), file_);

    // Stream the content right to the buffer.
    snappy::ByteArraySource snappy_source(
        reinterpret_cast<const char*>(host_ptr), cmd.decoded_length);
    SnappySink snappy_sink(file_);
    cmd.encoded_length =
        static_cast<uint32_t>(snappy::Compress(&snappy_source, &snappy_sink));

    // Seek back and overwrite the header with our final size.
    std::fseek(file_, header_position, SEEK_SET);
    fwrite(&cmd, 1, sizeof(cmd), file_);
    std::fseek(file_, header_position + sizeof(cmd) + cmd.encoded_length,
               SEEK_SET);
  } else {
    // Uncompressed - write buffer directly to the file.
    cmd.encoding_format = MemoryEncodingFormat::kNone;
    fwrite(&cmd, 1, sizeof(cmd), file_);
    fwrite(host_ptr, 1, cmd.decoded_length, file_);
  }
}

void TraceWriter::WriteEdramSnapshot(const void* snapshot) {
  EdramSnapshotCommand cmd = {};
  cmd.type = TraceCommandType::kEdramSnapshot;

  if (compress_output_) {
    // Write the header now so we reserve space in the buffer.
    long header_position = std::ftell(file_);
    cmd.encoding_format = MemoryEncodingFormat::kSnappy;
    fwrite(&cmd, 1, sizeof(cmd), file_);

    // Stream the content right to the buffer.
    snappy::ByteArraySource snappy_source(
        reinterpret_cast<const char*>(snapshot), xenos::kEdramSizeBytes);
    SnappySink snappy_sink(file_);
    cmd.encoded_length =
        static_cast<uint32_t>(snappy::Compress(&snappy_source, &snappy_sink));

    // Seek back and overwrite the header with our final size.
    std::fseek(file_, header_position, SEEK_SET);
    fwrite(&cmd, 1, sizeof(cmd), file_);
    std::fseek(file_, header_position + sizeof(cmd) + cmd.encoded_length,
               SEEK_SET);
  } else {
    // Uncompressed - write buffer directly to the file.
    cmd.encoding_format = MemoryEncodingFormat::kNone;
    cmd.encoded_length = xenos::kEdramSizeBytes;
    fwrite(&cmd, 1, sizeof(cmd), file_);
    fwrite(snapshot, 1, xenos::kEdramSizeBytes, file_);
  }
}

void TraceWriter::WriteEvent(EventCommand::Type event_type) {
  if (!file_) {
    return;
  }
  EventCommand cmd = {
      TraceCommandType::kEvent,
      event_type,
  };
  fwrite(&cmd, 1, sizeof(cmd), file_);
}

void TraceWriter::WriteRegisters(uint32_t first_register,
                                 const uint32_t* register_values,
                                 uint32_t register_count,
                                 bool execute_callbacks_on_play) {
  RegistersCommand cmd = {};
  cmd.type = TraceCommandType::kRegisters;
  cmd.first_register = first_register;
  cmd.register_count = register_count;
  cmd.execute_callbacks = execute_callbacks_on_play;

  uint32_t uncompressed_length = uint32_t(sizeof(uint32_t) * register_count);
  if (compress_output_) {
    // Write the header now so we reserve space in the buffer.
    long header_position = std::ftell(file_);
    cmd.encoding_format = MemoryEncodingFormat::kSnappy;
    fwrite(&cmd, 1, sizeof(cmd), file_);

    // Stream the content right to the buffer.
    snappy::ByteArraySource snappy_source(
        reinterpret_cast<const char*>(register_values), uncompressed_length);
    SnappySink snappy_sink(file_);
    cmd.encoded_length =
        static_cast<uint32_t>(snappy::Compress(&snappy_source, &snappy_sink));

    // Seek back and overwrite the header with our final size.
    std::fseek(file_, header_position, SEEK_SET);
    fwrite(&cmd, 1, sizeof(cmd), file_);
    std::fseek(file_, header_position + sizeof(cmd) + cmd.encoded_length,
               SEEK_SET);
  } else {
    // Uncompressed - write the values directly to the file.
    cmd.encoding_format = MemoryEncodingFormat::kNone;
    cmd.encoded_length = uncompressed_length;
    fwrite(&cmd, 1, sizeof(cmd), file_);
    fwrite(register_values, 1, uncompressed_length, file_);
  }
}

void TraceWriter::WriteGammaRamp(
    const reg::DC_LUT_30_COLOR* gamma_ramp_256_entry_table,
    const reg::DC_LUT_PWL_DATA* gamma_ramp_pwl_rgb,
    uint32_t gamma_ramp_rw_component) {
  GammaRampCommand cmd = {};
  cmd.type = TraceCommandType::kGammaRamp;
  cmd.rw_component = uint8_t(gamma_ramp_rw_component);

  constexpr uint32_t k256EntryTableUncompressedLength =
      sizeof(reg::DC_LUT_30_COLOR) * 256;
  constexpr uint32_t kPWLUncompressedLength =
      sizeof(reg::DC_LUT_PWL_DATA) * 3 * 128;
  constexpr uint32_t kUncompressedLength =
      k256EntryTableUncompressedLength + kPWLUncompressedLength;
  if (compress_output_) {
    // Write the header now so we reserve space in the buffer.
    long header_position = std::ftell(file_);
    cmd.encoding_format = MemoryEncodingFormat::kSnappy;
    fwrite(&cmd, 1, sizeof(cmd), file_);

    // Stream the content right to the buffer.
    {
      std::unique_ptr<char[]> gamma_ramps(new char[kUncompressedLength]);
      std::memcpy(gamma_ramps.get(), gamma_ramp_256_entry_table,
                  k256EntryTableUncompressedLength);
      std::memcpy(gamma_ramps.get() + k256EntryTableUncompressedLength,
                  gamma_ramp_pwl_rgb, kPWLUncompressedLength);
      snappy::ByteArraySource snappy_source(gamma_ramps.get(),
                                            kUncompressedLength);
      SnappySink snappy_sink(file_);
      cmd.encoded_length =
          static_cast<uint32_t>(snappy::Compress(&snappy_source, &snappy_sink));
    }

    // Seek back and overwrite the header with our final size.
    std::fseek(file_, header_position, SEEK_SET);
    fwrite(&cmd, 1, sizeof(cmd), file_);
    std::fseek(file_, header_position + sizeof(cmd) + cmd.encoded_length,
               SEEK_SET);
  } else {
    // Uncompressed - write the values directly to the file.
    cmd.encoding_format = MemoryEncodingFormat::kNone;
    cmd.encoded_length = kUncompressedLength;
    fwrite(&cmd, 1, sizeof(cmd), file_);
    fwrite(gamma_ramp_256_entry_table, 1, k256EntryTableUncompressedLength,
           file_);
    fwrite(gamma_ramp_pwl_rgb, 1, kPWLUncompressedLength, file_);
  }
}
#endif
}  //  namespace gpu
}  //  namespace xe
