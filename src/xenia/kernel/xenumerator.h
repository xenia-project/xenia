/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XENUMERATOR_H_
#define XENIA_KERNEL_XENUMERATOR_H_

#include <algorithm>
#include <cstring>
#include <vector>

#include "xenia/kernel/xam/achievement_manager.h"
#include "xenia/kernel/xobject.h"
#include "xenia/xbox.h"

namespace xe {
namespace kernel {

struct X_KENUMERATOR {
  be<uint32_t> app_id;
  be<uint32_t> open_message;
  be<uint32_t> close_message;
  be<uint32_t> user_index;
  be<uint32_t> items_per_enumerate;
  be<uint32_t> flags;
};
static_assert_size(X_KENUMERATOR, 0x18);

struct X_KENUMERATOR_CONTENT_AGGREGATE {
  be<uint32_t> magic;
  be<uint32_t> handle;
};

class XEnumerator : public XObject {
 public:
  static const XObject::Type kObjectType = XObject::Type::Enumerator;

  XEnumerator(KernelState* kernel_state, size_t items_per_enumerate,
              size_t item_size);
  virtual ~XEnumerator();

  X_STATUS Initialize(uint32_t user_index, uint32_t app_id,
                      uint32_t open_message, uint32_t close_message,
                      uint32_t flags, uint32_t extra_size, void** extra_buffer);

  X_STATUS Initialize(uint32_t user_index, uint32_t app_id,
                      uint32_t open_message, uint32_t close_message,
                      uint32_t flags);

  template <typename T>
  X_STATUS Initialize(uint32_t user_index, uint32_t app_id,
                      uint32_t open_message, uint32_t close_message,
                      uint32_t flags, T** extra) {
    void* dummy;
    auto result = Initialize(user_index, app_id, open_message, close_message,
                             flags, static_cast<uint32_t>(sizeof(T)), &dummy);
    if (extra) {
      *extra = XFAILED(result) ? nullptr : static_cast<T*>(dummy);
    }
    return result;
  }

  virtual uint32_t WriteItems(uint32_t buffer_ptr, uint8_t* buffer_data,
                              uint32_t* written_count) = 0;

  size_t item_size() const { return item_size_; }
  size_t items_per_enumerate() const { return items_per_enumerate_; }

 private:
  size_t items_per_enumerate_;
  size_t item_size_;
};

class XStaticUntypedEnumerator : public XEnumerator {
 public:
  XStaticUntypedEnumerator(KernelState* kernel_state,
                           size_t items_per_enumerate, size_t item_size)
      : XEnumerator(kernel_state, items_per_enumerate, item_size),
        item_count_(0),
        current_item_(0) {}

  size_t item_count() const { return item_count_; }

  uint8_t* AppendItem();

  uint32_t WriteItems(uint32_t buffer_ptr, uint8_t* buffer_data,
                      uint32_t* written_count) override;

 private:
  size_t item_count_;
  size_t current_item_;
  std::vector<uint8_t> buffer_;
};

template <typename T>
class XStaticEnumerator : public XStaticUntypedEnumerator {
 public:
  XStaticEnumerator(KernelState* kernel_state, size_t items_per_enumerate)
      : XStaticUntypedEnumerator(kernel_state, items_per_enumerate, sizeof(T)) {
  }

  T* AppendItem() {
    return reinterpret_cast<T*>(XStaticUntypedEnumerator::AppendItem());
  }

  void AppendItem(const T& item) {
    auto ptr = AppendItem();
    item.Write(ptr);
  }
};

class XAchievementEnumerator : public XEnumerator {
 public:
  struct AchievementDetails {
    uint32_t id;
    std::u16string label;
    std::u16string description;
    std::u16string unachieved;
    uint32_t image_id;
    uint32_t gamerscore;
    struct {
      uint32_t high_part;
      uint32_t low_part;
    } unlock_time;
    uint32_t flags;
  };

  XAchievementEnumerator(KernelState* kernel_state, size_t items_per_enumerate,
                         uint32_t flags)
      : XEnumerator(
            kernel_state, items_per_enumerate,
            sizeof(xam::X_ACHIEVEMENT_DETAILS) +
                (!!(flags & 7) ? xam::X_ACHIEVEMENT_DETAILS::kStringBufferSize
                               : 0)),
        flags_(flags) {}

  void AppendItem(AchievementDetails item) {
    items_.push_back(std::move(item));
  }

  uint32_t WriteItems(uint32_t buffer_ptr, uint8_t* buffer_data,
                      uint32_t* written_count) override;

 private:
  struct StringBuffer {
    uint32_t ptr;
    uint8_t* data;
    size_t remaining_bytes;
  };

  uint32_t AppendString(StringBuffer& sb, const std::u16string_view string) {
    size_t count = string.length() + 1;
    size_t size = count * sizeof(char16_t);
    if (size > sb.remaining_bytes) {
      assert_always();
      return 0;
    }
    auto ptr = sb.ptr;
    string_util::copy_and_swap_truncating(reinterpret_cast<char16_t*>(sb.data),
                                          string, count);
    sb.ptr += static_cast<uint32_t>(size);
    sb.data += size;
    sb.remaining_bytes -= size;
    return ptr;
  }

 private:
  uint32_t flags_;
  std::vector<AchievementDetails> items_;
  size_t current_item_ = 0;
};

class XUserStatsEnumerator : public XEnumerator {
 public:
  struct XUSER_STATS_SPEC {
    xe::be<uint32_t> ViewId;
    xe::be<uint32_t> NumColumnIds;
    xe::be<uint16_t> rgwColumnIds[0x40];
  };

  XUserStatsEnumerator(KernelState* kernel_state, size_t items_per_enumerate)
      : XEnumerator(kernel_state, items_per_enumerate, 0) {}

  uint32_t WriteItems(uint32_t buffer_ptr, uint8_t* buffer_data,
                      uint32_t* written_count) override;

 private:
  std::vector<XUSER_STATS_SPEC> items_;
  size_t current_item_ = 0;
};

}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XENUMERATOR_H_
