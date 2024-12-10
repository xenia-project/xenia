/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/xenumerator.h"

namespace xe {
namespace kernel {

XEnumerator::XEnumerator(KernelState* kernel_state, size_t items_per_enumerate,
                         size_t item_size)
    : XObject(kernel_state, kObjectType),
      items_per_enumerate_(items_per_enumerate),
      item_size_(item_size) {}

XEnumerator::~XEnumerator() = default;

X_STATUS XEnumerator::Initialize(uint32_t user_index, uint32_t app_id,
                                 uint32_t open_message, uint32_t close_message,
                                 uint32_t flags, uint32_t extra_size,
                                 void** extra_buffer) {
  auto native_object = CreateNative(sizeof(X_KENUMERATOR) + extra_size);
  if (!native_object) {
    return X_STATUS_NO_MEMORY;
  }
  auto guest_object = reinterpret_cast<X_KENUMERATOR*>(native_object);
  guest_object->app_id = app_id;
  guest_object->open_message = open_message;
  guest_object->close_message = close_message;
  guest_object->user_index = user_index;
  guest_object->items_per_enumerate =
      static_cast<uint32_t>(items_per_enumerate_);
  guest_object->flags = flags;
  if (extra_buffer) {
    *extra_buffer =
        !extra_buffer ? nullptr : &native_object[sizeof(X_KENUMERATOR)];
  }
  return X_STATUS_SUCCESS;
}

X_STATUS XEnumerator::Initialize(uint32_t user_index, uint32_t app_id,
                                 uint32_t open_message, uint32_t close_message,
                                 uint32_t flags) {
  return Initialize(user_index, app_id, open_message, close_message, flags, 0,
                    nullptr);
}

uint8_t* XStaticUntypedEnumerator::AppendItem() {
  size_t offset = buffer_.size();
  buffer_.resize(offset + item_size());
  item_count_++;
  return const_cast<uint8_t*>(&buffer_.data()[offset]);
}

uint32_t XStaticUntypedEnumerator::WriteItems(uint32_t buffer_ptr,
                                              uint8_t* buffer_data,
                                              uint32_t* written_count) {
  size_t count = std::min(item_count_ - current_item_, items_per_enumerate());
  if (!count) {
    return X_ERROR_NO_MORE_FILES;
  }

  size_t size = count * item_size();
  size_t offset = current_item_ * item_size();
  std::memcpy(buffer_data, buffer_.data() + offset, size);

  current_item_ += count;

  if (written_count) {
    *written_count = static_cast<uint32_t>(count);
  }

  return X_ERROR_SUCCESS;
}

uint32_t XAchievementEnumerator::WriteItems(uint32_t buffer_ptr,
                                            uint8_t* buffer_data,
                                            uint32_t* written_count) {
  size_t count = std::min(items_.size() - current_item_, items_per_enumerate());
  if (!count) {
    return X_ERROR_NO_MORE_FILES;
  }

  size_t size = count * item_size();

  auto details = reinterpret_cast<xam::X_ACHIEVEMENT_DETAILS*>(buffer_data);
  size_t string_offset =
      items_per_enumerate() * sizeof(xam::X_ACHIEVEMENT_DETAILS);
  auto string_buffer =
      StringBuffer{buffer_ptr + static_cast<uint32_t>(string_offset),
                   &buffer_data[string_offset],
                   count * xam::X_ACHIEVEMENT_DETAILS::kStringBufferSize};
  for (size_t i = 0, o = current_item_; i < count; ++i, ++current_item_) {
    const auto& item = items_[current_item_];
    details[i].id = item.id;
    details[i].label_ptr =
        !!(flags_ & 1) ? AppendString(string_buffer, item.label) : 0;
    details[i].description_ptr =
        !!(flags_ & 2) ? AppendString(string_buffer, item.description) : 0;
    details[i].unachieved_ptr =
        !!(flags_ & 4) ? AppendString(string_buffer, item.unachieved) : 0;
    details[i].image_id = item.image_id;
    details[i].gamerscore = item.gamerscore;
    details[i].unlock_time.high_part = item.unlock_time.high_part;
    details[i].unlock_time.low_part = item.unlock_time.low_part;
    details[i].flags = item.flags;
  }

  if (written_count) {
    *written_count = static_cast<uint32_t>(count);
  }

  return X_ERROR_SUCCESS;
}

uint32_t XUserStatsEnumerator::WriteItems(uint32_t buffer_ptr,
                                          uint8_t* buffer_data,
                                          uint32_t* written_count) {
  size_t count = std::min(items_.size() - current_item_, items_per_enumerate());
  if (!count) {
    return X_ERROR_NO_MORE_FILES;
  }

  size_t size = count * item_size();

  return X_ERROR_SUCCESS;
}

}  // namespace kernel
}  // namespace xe
