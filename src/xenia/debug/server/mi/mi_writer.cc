/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/debug/server/mi/mi_writer.h"

#include "xenia/base/assert.h"

namespace xe {
namespace debug {
namespace server {
namespace mi {

void MIWriter::AppendRaw(const char* value, size_t length) {
  assert_true(buffer_offset_ + length < buffer_capacity_);
  std::memcpy(buffer_ + buffer_offset_, value, length);
  buffer_offset_ += length;
}

void MIWriter::BeginRecord(RecordToken token, const char* record_class) {
  assert_false(in_record_);
  in_record_ = true;
  size_t record_class_length = std::strlen(record_class);
  assert_true(buffer_offset_ + 1 + record_class_length < buffer_capacity_);
  buffer_[buffer_offset_++] = char(token);
  std::memcpy(buffer_ + buffer_offset_, record_class, record_class_length);
  buffer_offset_ += record_class_length;
}

void MIWriter::BeginResultRecord(ResultClass result_class) {
  const char* record_class = "error";
  switch (result_class) {
    case ResultClass::kDone:
      record_class = "done";
      break;
    case ResultClass::kRunning:
      record_class = "running";
      break;
    case ResultClass::kConnected:
      record_class = "connected";
      break;
    case ResultClass::kError:
      record_class = "error";
      break;
    case ResultClass::kExit:
      record_class = "exit";
      break;
  }
  BeginRecord(RecordToken::kResult, record_class);
}

void MIWriter::BeginStreamRecord(StreamToken token) {
  assert_false(in_record_);
  in_record_ = true;
  assert_true(buffer_offset_ + 1 < buffer_capacity_);
  buffer_[buffer_offset_++] = char(token);
}

void MIWriter::EndRecord() {
  assert_true(in_record_);
  in_record_ = false;
  assert_true(buffer_offset_ + 1 < buffer_capacity_);
  buffer_[buffer_offset_++] = '\n';
}

void MIWriter::AppendArraySeparator() {
  if (array_depth_) {
    if (array_count_[array_depth_]) {
      assert_true(buffer_offset_ + 1 < buffer_capacity_);
      buffer_[buffer_offset_++] = ',';
    }
    ++array_count_[array_depth_];
  }
}

void MIWriter::PushTuple() {
  assert_true(in_record_);
  AppendArraySeparator();
  ++array_depth_;
  assert_true(array_depth_ < kMaxArrayDepth);
  array_count_[array_depth_] = 0;
  assert_true(buffer_offset_ + 1 < buffer_capacity_);
  buffer_[buffer_offset_++] = '{';
}

void MIWriter::PopTuple() {
  --array_depth_;
  assert_true(buffer_offset_ + 1 < buffer_capacity_);
  buffer_[buffer_offset_++] = '}';
}

void MIWriter::PushList() {
  assert_true(in_record_);
  AppendArraySeparator();
  ++array_depth_;
  assert_true(array_depth_ < kMaxArrayDepth);
  array_count_[array_depth_] = 0;
  assert_true(buffer_offset_ + 1 < buffer_capacity_);
  buffer_[buffer_offset_++] = '[';
}

void MIWriter::PopList() {
  --array_depth_;
  assert_true(buffer_offset_ + 1 < buffer_capacity_);
  buffer_[buffer_offset_++] = ']';
}

void MIWriter::AppendString(const char* value, size_t length) {
  assert_true(in_record_);
  AppendArraySeparator();
  assert_true(buffer_offset_ + length < buffer_capacity_);
  std::memcpy(buffer_ + buffer_offset_, value, length);
  buffer_offset_ += length;
}

void MIWriter::AppendResult(const char* variable, const char* value,
                            size_t length) {
  assert_true(in_record_);
  AppendArraySeparator();
  size_t variable_length = std::strlen(variable);
  assert_true(buffer_offset_ + 1 + variable_length + length < buffer_capacity_);
  std::memcpy(buffer_ + buffer_offset_, variable, variable_length);
  buffer_offset_ += variable_length;
  buffer_[buffer_offset_++] = '=';
  std::memcpy(buffer_ + buffer_offset_, value, length);
  buffer_offset_ += length;
}

}  // namespace mi
}  // namespace server
}  // namespace debug
}  // namespace xe
