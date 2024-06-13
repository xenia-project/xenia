/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_UTIL_XUSERDATA_H_
#define XENIA_KERNEL_UTIL_XUSERDATA_H_

#include "xenia/base/byte_stream.h"
#include "xenia/xbox.h"

namespace xe {
namespace kernel {

enum class X_USER_DATA_TYPE : uint8_t {
  CONTENT = 0,
  INT32 = 1,
  INT64 = 2,
  DOUBLE = 3,
  WSTRING = 4,
  FLOAT = 5,
  BINARY = 6,
  DATETIME = 7,
  UNSET = 0xFF,
};

struct X_USER_DATA {
  X_USER_DATA_TYPE type;

  union {
    be<int32_t> s32;
    be<int64_t> s64;
    be<uint32_t> u32;
    be<double> f64;
    struct {
      be<uint32_t> size;
      be<uint32_t> ptr;
    } unicode;
    be<float> f32;
    struct {
      be<uint32_t> size;
      be<uint32_t> ptr;
    } binary;
    be<uint64_t> filetime;
  };
};
static_assert_size(X_USER_DATA, 16);

class DataByteStream : public ByteStream {
 public:
  DataByteStream(uint32_t ptr, uint8_t* data, size_t data_length,
                 size_t offset = 0)
      : ByteStream(data, data_length, offset), ptr_(ptr) {}

  uint32_t ptr() const { return static_cast<uint32_t>(ptr_ + offset()); }

 private:
  uint32_t ptr_;
};

class UserData {
 public:
  union Key {
    uint32_t value;
    struct {
      uint32_t id : 14;
      uint32_t unk : 2;
      uint32_t size : 12;
      uint32_t type : 4;
    };
  };

  UserData() {};
  UserData(X_USER_DATA_TYPE type) { data_.type = type; }

  virtual void Append(X_USER_DATA* data, DataByteStream* stream) {
    data->type = data_.type;
  }

  virtual std::vector<uint8_t> Serialize() const {
    return std::vector<uint8_t>();
  }
  virtual void Deserialize(std::vector<uint8_t>) {}

 private:
  X_USER_DATA data_ = {};
};

class Int32UserData : public UserData {
 public:
  Int32UserData(int32_t value)
      : UserData(X_USER_DATA_TYPE::INT32), value_(value) {}
  void Append(X_USER_DATA* data, DataByteStream* stream) override {
    UserData::Append(data, stream);
    data->s32 = value_;
  }

 private:
  int32_t value_;
};

class Uint32UserData : public UserData {
 public:
  Uint32UserData(uint32_t value)
      : UserData(X_USER_DATA_TYPE::INT32), value_(value) {}
  void Append(X_USER_DATA* data, DataByteStream* stream) override {
    UserData::Append(data, stream);
    data->u32 = value_;
  }

 private:
  uint32_t value_;
};

class Int64UserData : public UserData {
 public:
  Int64UserData(int64_t value)
      : UserData(X_USER_DATA_TYPE::INT64), value_(value) {}
  void Append(X_USER_DATA* data, DataByteStream* stream) override {
    UserData::Append(data, stream);
    data->s64 = value_;
  }

 private:
  int64_t value_;
};

class FloatUserData : public UserData {
 public:
  FloatUserData(float value)
      : UserData(X_USER_DATA_TYPE::FLOAT), value_(value) {}

  void Append(X_USER_DATA* data, DataByteStream* stream) override {
    UserData::Append(data, stream);
    data->f32 = value_;
  }

 private:
  float value_;
};

class DoubleUserData : public UserData {
 public:
  DoubleUserData(double value)
      : UserData(X_USER_DATA_TYPE::DOUBLE), value_(value) {}
  void Append(X_USER_DATA* data, DataByteStream* stream) override {
    UserData::Append(data, stream);
    data->f64 = value_;
  }

 private:
  double value_;
};

class UnicodeUserData : public UserData {
 public:
  UnicodeUserData(const std::u16string& value)
      : UserData(X_USER_DATA_TYPE::WSTRING), value_(value) {}
  void Append(X_USER_DATA* data, DataByteStream* stream) override {
    UserData::Append(data, stream);

    if (value_.empty()) {
      data->unicode.size = 0;
      data->unicode.ptr = 0;
      return;
    }

    size_t count = value_.size() + 1;
    size_t size = 2 * count;
    assert_true(size <= std::numeric_limits<uint32_t>::max());
    data->unicode.size = static_cast<uint32_t>(size);
    data->unicode.ptr = stream->ptr();
    auto buffer =
        reinterpret_cast<uint16_t*>(&stream->data()[stream->offset()]);
    stream->Advance(size);
    copy_and_swap(buffer, (uint16_t*)value_.data(), count);
  }

 private:
  std::u16string value_;
};

class BinaryUserData : public UserData {
 public:
  BinaryUserData(const std::vector<uint8_t>& value)
      : UserData(X_USER_DATA_TYPE::BINARY), value_(value) {}
  void Append(X_USER_DATA* data, DataByteStream* stream) override {
    UserData::Append(data, stream);

    if (value_.empty()) {
      data->binary.size = 0;
      data->binary.ptr = 0;
      return;
    }

    size_t size = value_.size();
    assert_true(size <= std::numeric_limits<uint32_t>::max());
    data->binary.size = static_cast<uint32_t>(size);
    data->binary.ptr = stream->ptr();
    stream->Write(value_.data(), size);
  }

  std::vector<uint8_t> Serialize() const override {
    return std::vector<uint8_t>(value_.data(), value_.data() + value_.size());
  }

  void Deserialize(std::vector<uint8_t> data) override { value_ = data; }

 private:
  std::vector<uint8_t> value_;
};

class DateTimeUserData : public UserData {
 public:
  DateTimeUserData(int64_t value)
      : UserData(X_USER_DATA_TYPE::DATETIME), value_(value) {}

  void Append(X_USER_DATA* data, DataByteStream* stream) override {
    UserData::Append(data, stream);
    data->filetime = value_;
  }

 private:
  int64_t value_;
};

}  // namespace kernel
}  // namespace xe

#endif
