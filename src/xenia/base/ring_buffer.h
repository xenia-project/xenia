/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_BASE_RING_BUFFER_H_
#define XENIA_BASE_RING_BUFFER_H_

#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

#include "xenia/base/assert.h"
#include "xenia/base/byte_order.h"
#include "xenia/base/math.h"
#include "xenia/base/memory.h"

namespace xe {
/*
        todo: this class is CRITICAL to the performance of the entire emulator
        currently, about 0.74% cpu time is still taken up by ReadAndSwap, 0.23
   is used by read_count I believe that part of the issue is that smaller
   ringbuffers are kicking off an automatic prefetcher stream, that ends up
   reading ahead of the end of the ring because it can only go in a straight
   line it then gets a cache miss when it eventually wraps around to the start
   of the ring? really hard to tell whats going on there honestly, maybe we can
   occasionally prefetch the first line of the ring to L1? For the automatic
   prefetching i don't think there are any good options. I don't know if we have
   any control over where these buffers will be (they seem to be in guest memory
   :/), but if we did we could right-justify the buffer so that the final byte
   of the ring ends at the end of a page. i think most automatic prefetchers
   cannot cross page boundaries it does feel like something isnt right here
   though

        todo: microoptimization, we can change our size members to be uint32 so
   that the registers no longer need the rex prefix, shrinking the generated
   code a bit.. like i said, every bit helps in this class
*/
using ring_size_t = uint32_t;
class RingBuffer {
 public:
  RingBuffer(uint8_t* buffer, size_t capacity);

  uint8_t* buffer() const { return buffer_; }
  ring_size_t capacity() const { return capacity_; }
  bool empty() const { return read_offset_ == write_offset_; }

  ring_size_t read_offset() const { return read_offset_; }
  uintptr_t read_ptr() const {
    return uintptr_t(buffer_) + static_cast<uintptr_t>(read_offset_);
  }

  // todo: offset/ capacity_ is probably always 1 when its over, just check and
  // subtract instead
  void set_read_offset(size_t offset) { read_offset_ = offset % capacity_; }
  ring_size_t read_count() const {
    // chrispy: these branches are unpredictable

    ring_size_t read_offs = read_offset_;
    ring_size_t write_offs = write_offset_;
    ring_size_t cap = capacity_;

    ring_size_t offset_delta = write_offs - read_offs;
    ring_size_t wrap_read_count = (cap - read_offs) + write_offs;


    if (XE_LIKELY(read_offs <= write_offs)) {
      return offset_delta;  // will be 0 if they are equal, semantically
                            // identical to old code (i checked the asm, msvc
                            // does not automatically do this)
    } else {
      return wrap_read_count;
    }
  }

  ring_size_t write_offset() const { return write_offset_; }
  uintptr_t write_ptr() const { return uintptr_t(buffer_) + write_offset_; }
  void set_write_offset(size_t offset) {
    write_offset_ = static_cast<ring_size_t>(offset) % capacity_;
  }
  ring_size_t write_count() const {
    if (read_offset_ == write_offset_) {
      return capacity_;
    } else if (write_offset_ < read_offset_) {
      return read_offset_ - write_offset_;
    } else {
      return (capacity_ - write_offset_) + read_offset_;
    }
  }

  void AdvanceRead(size_t count);
  void AdvanceWrite(size_t count);

  struct ReadRange {
    const uint8_t* XE_RESTRICT first;

    const uint8_t* XE_RESTRICT second;
    ring_size_t first_length;
    ring_size_t second_length;
  };
  ReadRange BeginRead(size_t count);
  void EndRead(ReadRange read_range);

  /*
        BeginRead, but if there is a second Range it will prefetch all lines of
     it

                this does not prefetch the first range, because software
     prefetching can do that faster than we can
  */
  template <swcache::PrefetchTag tag>
  XE_FORCEINLINE ReadRange BeginPrefetchedRead(size_t count) {
    ReadRange range = BeginRead(count);

    if (range.second) {
      ring_size_t numlines =
          xe::align<ring_size_t>(range.second_length, XE_HOST_CACHE_LINE_SIZE) /
          XE_HOST_CACHE_LINE_SIZE;
      // chrispy: maybe unroll?
      for (ring_size_t i = 0; i < numlines; ++i) {
        swcache::Prefetch<tag>(range.second + (i * XE_HOST_CACHE_LINE_SIZE));
      }
    }
    return range;
  }

  size_t Read(uint8_t* buffer, size_t count);
  template <typename T>
  size_t Read(T* buffer, size_t count) {
    return Read(reinterpret_cast<uint8_t*>(buffer), count);
  }

  template <typename T>
  T Read() {
    static_assert(std::is_fundamental<T>::value,
                  "Immediate read only supports basic types!");

    T imm;
    size_t read = Read(reinterpret_cast<uint8_t*>(&imm), sizeof(T));
    assert_true(read == sizeof(T));
    return imm;
  }

  template <typename T>
  T ReadAndSwap() {
    static_assert(std::is_fundamental<T>::value,
                  "Immediate read only supports basic types!");

    T imm;
    size_t read = Read(reinterpret_cast<uint8_t*>(&imm), sizeof(T));
    assert_true(read == sizeof(T));
    imm = xe::byte_swap(imm);
    return imm;
  }

  size_t Write(const uint8_t* buffer, size_t count);
  template <typename T>
  size_t Write(const T* buffer, size_t count) {
    return Write(reinterpret_cast<const uint8_t*>(buffer), count);
  }

  template <typename T>
  size_t Write(T& data) {
    return Write(reinterpret_cast<const uint8_t*>(&data), sizeof(T));
  }

 private:
  uint8_t* XE_RESTRICT buffer_ = nullptr;
  ring_size_t capacity_ = 0;
  ring_size_t read_offset_ = 0;
  ring_size_t write_offset_ = 0;
};

template <>
inline uint32_t RingBuffer::ReadAndSwap<uint32_t>() {
  ring_size_t read_offset = this->read_offset_;
  xenia_assert(this->capacity_ >= 4);

  ring_size_t next_read_offset = read_offset + 4;

  if (XE_UNLIKELY(next_read_offset == this->capacity_)) {
    next_read_offset = 0;
    // todo: maybe prefetch next? or should that happen much earlier?
  }
  this->read_offset_ = next_read_offset;
  uint32_t ring_value = *(uint32_t*)&this->buffer_[read_offset];
  return xe::byte_swap(ring_value);
}
}  // namespace xe

#endif  // XENIA_BASE_RING_BUFFER_H_
