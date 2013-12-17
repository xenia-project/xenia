/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/debug/protocols/gdb/message.h>


using namespace xe;
using namespace xe::debug;
using namespace xe::debug::protocols::gdb;


MessageReader::MessageReader() {
  Reset();
}

MessageReader::~MessageReader() {
}

void MessageReader::Reset() {
  buffer_.Reset();
}

void MessageReader::Append(const uint8_t* buffer, size_t length) {
  buffer_.AppendBytes(buffer, length);
}

bool MessageReader::CheckComplete() {
  // TODO(benvanik): verify checksum.
  return true;
}

const char* MessageReader::GetString() {
  return buffer_.GetString();
}


MessageWriter::MessageWriter() :
    offset_(0) {
  Reset();
}

MessageWriter::~MessageWriter() {
}

const uint8_t* MessageWriter::buffer() const {
  return (const uint8_t*)buffer_.GetString();
}

size_t MessageWriter::length() const {
  return buffer_.length() + 1;
}

void MessageWriter::Reset() {
  buffer_.Reset();
  buffer_.Append("$");
  offset_ = 0;
}

void MessageWriter::Append(const char* format, ...) {
  va_list args;
  va_start(args, format);
  buffer_.AppendVarargs(format, args);
  va_end(args);
}

void MessageWriter::AppendVarargs(const char* format, va_list args) {
  buffer_.AppendVarargs(format, args);
}

void MessageWriter::Finalize() {
  uint8_t checksum = 0;
  const uint8_t* data = (const uint8_t*)buffer_.GetString();
  data++; // skip $
  while (true) {
    uint8_t c = *data;
    if (!c) {
      break;
    }
    checksum += c;
    data++;
  }

  buffer_.Append("#%.2X", checksum);
}
