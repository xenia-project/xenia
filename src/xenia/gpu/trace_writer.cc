/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/trace_writer.h"

#include "xenia/base/assert.h"
#include "xenia/base/logging.h"
#include "xenia/base/string.h"

#include "third_party/zlib/zlib.h"

namespace xe {
namespace gpu {

TraceWriter::TraceWriter(uint8_t* membase)
    : membase_(membase), file_(nullptr) {}

TraceWriter::~TraceWriter() = default;

bool TraceWriter::Open(const std::wstring& path) {
  Close();

  // Reserve 2 MB of space.
  tmp_buff_.reserve(1024 * 2048);

  auto canonical_path = xe::to_absolute_path(path);
  auto base_path = xe::find_base_path(canonical_path);
  xe::filesystem::CreateFolder(base_path);

  file_ = xe::filesystem::OpenFile(canonical_path, "wb");
  return file_ != nullptr;
}

void TraceWriter::Flush() {
  if (file_) {
    fflush(file_);
  }
}

void TraceWriter::Close() {
  if (file_) {
    fflush(file_);
    fclose(file_);
    file_ = nullptr;
  }
}

void TraceWriter::WritePrimaryBufferStart(uint32_t base_ptr, uint32_t count) {
  if (!file_) {
    return;
  }
  auto cmd = PrimaryBufferStartCommand({
      TraceCommandType::kPrimaryBufferStart, base_ptr, 0,
  });
  fwrite(&cmd, 1, sizeof(cmd), file_);
}

void TraceWriter::WritePrimaryBufferEnd() {
  if (!file_) {
    return;
  }
  auto cmd = PrimaryBufferEndCommand({
      TraceCommandType::kPrimaryBufferEnd,
  });
  fwrite(&cmd, 1, sizeof(cmd), file_);
}

void TraceWriter::WriteIndirectBufferStart(uint32_t base_ptr, uint32_t count) {
  if (!file_) {
    return;
  }
  auto cmd = IndirectBufferStartCommand({
      TraceCommandType::kIndirectBufferStart, base_ptr, 0,
  });
  fwrite(&cmd, 1, sizeof(cmd), file_);
}

void TraceWriter::WriteIndirectBufferEnd() {
  if (!file_) {
    return;
  }
  auto cmd = IndirectBufferEndCommand({
      TraceCommandType::kIndirectBufferEnd,
  });
  fwrite(&cmd, 1, sizeof(cmd), file_);
}

void TraceWriter::WritePacketStart(uint32_t base_ptr, uint32_t count) {
  if (!file_) {
    return;
  }
  auto cmd = PacketStartCommand({
      TraceCommandType::kPacketStart, base_ptr, count,
  });
  fwrite(&cmd, 1, sizeof(cmd), file_);
  fwrite(membase_ + base_ptr, 4, count, file_);
}

void TraceWriter::WritePacketEnd() {
  if (!file_) {
    return;
  }
  auto cmd = PacketEndCommand({
      TraceCommandType::kPacketEnd,
  });
  fwrite(&cmd, 1, sizeof(cmd), file_);
}

void TraceWriter::WriteMemoryRead(uint32_t base_ptr, size_t length) {
  if (!file_) {
    return;
  }

  bool compress = compress_output_ && length > compression_threshold_;

  auto cmd = MemoryReadCommand({
      TraceCommandType::kMemoryRead, base_ptr, uint32_t(length), 0,
  });

  if (compress) {
    size_t written = WriteCompressed(membase_ + base_ptr, length);
    cmd.length = uint32_t(written);
    cmd.full_length = uint32_t(length);

    fwrite(&cmd, 1, sizeof(cmd), file_);
    fwrite(tmp_buff_.data(), 1, written, file_);
  } else {
    fwrite(&cmd, 1, sizeof(cmd), file_);
    fwrite(membase_ + base_ptr, 1, length, file_);
  }
}

void TraceWriter::WriteMemoryWrite(uint32_t base_ptr, size_t length) {
  if (!file_) {
    return;
  }

  bool compress = compress_output_ && length > compression_threshold_;

  auto cmd = MemoryWriteCommand({
      TraceCommandType::kMemoryWrite, base_ptr, uint32_t(length), 0,
  });

  if (compress) {
    size_t written = WriteCompressed(membase_ + base_ptr, length);
    cmd.length = uint32_t(written);
    cmd.full_length = uint32_t(length);

    fwrite(&cmd, 1, sizeof(cmd), file_);
    fwrite(tmp_buff_.data(), 1, written, file_);
  } else {
    fwrite(&cmd, 1, sizeof(cmd), file_);
    fwrite(membase_ + base_ptr, 1, length, file_);
  }
}

void TraceWriter::WriteEvent(EventType event_type) {
  if (!file_) {
    return;
  }
  auto cmd = EventCommand({
      TraceCommandType::kEvent, event_type,
  });
  fwrite(&cmd, 1, sizeof(cmd), file_);
}

size_t TraceWriter::WriteCompressed(void* buf, size_t length) {
  tmp_buff_.resize(compressBound(uint32_t(length)));

  uLongf dest_len = (uint32_t)tmp_buff_.size();
  int ret =
      compress(tmp_buff_.data(), &dest_len, (uint8_t*)buf, uint32_t(length));
  assert_true(ret >= 0);
  return dest_len;
}

}  //  namespace gpu
}  //  namespace xe
