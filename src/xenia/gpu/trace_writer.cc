/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/trace_writer.h"

#include <cstring>

#include "third_party/snappy/snappy-sinksource.h"
#include "third_party/snappy/snappy.h"

#include "build/version.h"
#include "xenia/base/assert.h"
#include "xenia/base/logging.h"
#include "xenia/base/string.h"

namespace xe {
namespace gpu {

TraceWriter::TraceWriter(uint8_t* membase)
    : membase_(membase), file_(nullptr) {}

TraceWriter::~TraceWriter() = default;

bool TraceWriter::Open(const std::wstring& path, uint32_t title_id) {
  Close();

  auto canonical_path = xe::to_absolute_path(path);
  auto base_path = xe::find_base_path(canonical_path);
  xe::filesystem::CreateFolder(base_path);

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

void TraceWriter::WriteMemoryRead(uint32_t base_ptr, size_t length) {
  if (!file_) {
    return;
  }
  WriteMemoryCommand(TraceCommandType::kMemoryRead, base_ptr, length);
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

void TraceWriter::WriteMemoryWrite(uint32_t base_ptr, size_t length) {
  if (!file_) {
    return;
  }
  WriteMemoryCommand(TraceCommandType::kMemoryWrite, base_ptr, length);
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
                                     size_t length) {
  MemoryCommand cmd;
  cmd.type = type;
  cmd.base_ptr = base_ptr;
  cmd.encoding_format = MemoryEncodingFormat::kNone;
  cmd.encoded_length = cmd.decoded_length = static_cast<uint32_t>(length);

  bool compress = compress_output_ && length > compression_threshold_;
  if (compress) {
    // Write the header now so we reserve space in the buffer.
    long header_position = std::ftell(file_);
    cmd.encoding_format = MemoryEncodingFormat::kSnappy;
    fwrite(&cmd, 1, sizeof(cmd), file_);

    // Stream the content right to the buffer.
    snappy::ByteArraySource snappy_source(
        reinterpret_cast<const char*>(membase_ + cmd.base_ptr),
        cmd.decoded_length);
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
    fwrite(membase_ + cmd.base_ptr, 1, cmd.decoded_length, file_);
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

}  //  namespace gpu
}  //  namespace xe
