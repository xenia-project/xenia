/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_DEBUG_SERVER_MI_MI_WRITER_H_
#define XENIA_DEBUG_SERVER_MI_MI_WRITER_H_

#include <cstdint>
#include <string>

#include "xenia/debug/server/mi/mi_protocol.h"

namespace xe {
namespace debug {
namespace server {
namespace mi {

// https://sourceware.org/gdb/onlinedocs/gdb/GDB_002fMI-Output-Syntax.html#GDB_002fMI-Output-Syntax
class MIWriter {
 public:
  MIWriter(uint8_t* buffer, size_t buffer_capacity)
      : buffer_(buffer), buffer_capacity_(buffer_capacity) {}

  uint8_t* buffer() const { return buffer_; }
  size_t buffer_capacity() const { return buffer_capacity_; }
  size_t buffer_offset() const { return buffer_offset_; }

  void AppendRaw(const char* value, size_t length);
  void AppendRaw(const char* value) { AppendRaw(value, std::strlen(value)); }
  void AppendRaw(const std::string& value) {
    AppendRaw(value.c_str(), value.size());
  }

  void BeginResultRecord(ResultClass result_class);
  void BeginAsyncExecRecord(const char* async_class) {
    BeginRecord(RecordToken::kAsyncExec, async_class);
    array_depth_ = 1;
    array_count_[array_depth_] = 1;
  }
  void BeginAsyncStatusRecord(const char* async_class) {
    BeginRecord(RecordToken::kAsyncStatus, async_class);
    array_depth_ = 1;
    array_count_[array_depth_] = 1;
  }
  void BeginAsyncNotifyRecord(const char* async_class) {
    BeginRecord(RecordToken::kAsyncNotify, async_class);
    array_depth_ = 1;
    array_count_[array_depth_] = 1;
  }
  void BeginConsoleStreamOutput() { BeginStreamRecord(StreamToken::kConsole); }
  void BeginTargetStreamOutput() { BeginStreamRecord(StreamToken::kTarget); }
  void BeginLogStreamOutput() { BeginStreamRecord(StreamToken::kLog); }
  void EndRecord();

  void PushTuple();
  void PopTuple();
  void PushList();
  void PopList();

  void AppendString(const char* value, size_t length);
  void AppendString(const char* value) {
    AppendString(value, std::strlen(value));
  }
  void AppendString(const std::string& value) {
    AppendString(value.c_str(), value.size());
  }

  void AppendResult(const char* variable, const char* value, size_t length);
  void AppendResult(const char* variable, const char* value) {
    AppendResult(variable, value, std::strlen(value));
  }
  void AppendResult(const char* variable, const std::string& value) {
    AppendResult(variable, value.c_str(), value.size());
  }

 private:
  void BeginRecord(RecordToken token, const char* record_class);
  void BeginStreamRecord(StreamToken token);
  void AppendArraySeparator();

  uint8_t* buffer_ = nullptr;
  size_t buffer_capacity_ = 0;
  size_t buffer_offset_ = 0;

  static const int kMaxArrayDepth = 32;
  bool in_record_ = false;
  int array_depth_ = 0;
  int array_count_[kMaxArrayDepth];
};

}  // namespace mi
}  // namespace server
}  // namespace debug
}  // namespace xe

#endif  // XENIA_DEBUG_SERVER_MI_MI_WRITER_H_
