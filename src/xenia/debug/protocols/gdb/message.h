/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_DEBUG_PROTOCOLS_GDB_MESSAGE_H_
#define XENIA_DEBUG_PROTOCOLS_GDB_MESSAGE_H_

#include <xenia/common.h>
#include <xenia/core.h>

#include <alloy/string_buffer.h>


namespace xe {
namespace debug {
namespace protocols {
namespace gdb {


class MessageReader {
public:
  MessageReader();
  ~MessageReader();

  void Reset();
  void Append(const uint8_t* buffer, size_t length);
  bool CheckComplete();

  const char* GetString();

private:
  alloy::StringBuffer buffer_;
};


class MessageWriter {
public:
  MessageWriter();
  ~MessageWriter();

  const uint8_t* buffer() const;
  size_t length() const;
  size_t offset() const { return offset_; }
  void set_offset(size_t value) { offset_ = value; }

  void Reset();

  void Append(const char* format, ...);
  void AppendVarargs(const char* format, va_list args);

  void Finalize();

private:
  alloy::StringBuffer buffer_;
  size_t offset_;
};


}  // namespace gdb
}  // namespace protocols
}  // namespace debug
}  // namespace xe


#endif  // XENIA_DEBUG_PROTOCOLS_GDB_MESSAGE_H_
