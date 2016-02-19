/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2016 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_VULKAN_FENCED_POOLS_H_
#define XENIA_UI_VULKAN_FENCED_POOLS_H_

#include <memory>

#include "xenia/base/assert.h"
#include "xenia/ui/vulkan/vulkan.h"

namespace xe {
namespace ui {
namespace vulkan {

// Simple pool for Vulkan homogenous objects that cannot be reused while
// in-flight.
// It batches pooled objects into groups and uses a vkQueueSubmit fence to
// indicate their availability. If no objects are free when one is requested
// the caller is expected to create them.
template <typename T, typename HANDLE>
class BaseFencedPool {
 public:
  BaseFencedPool(VkDevice device) : device_(device) {}

  virtual ~BaseFencedPool() {
    // TODO(benvanik): wait on fence until done.
    assert_null(pending_batch_list_head_);

    // Run down free lists.
    while (free_batch_list_head_) {
      auto batch = free_batch_list_head_;
      free_batch_list_head_ = batch->next;
      delete batch;
    }
    while (free_entry_list_head_) {
      auto entry = free_entry_list_head_;
      free_entry_list_head_ = entry->next;
      static_cast<T*>(this)->FreeEntry(entry->handle);
      delete entry;
    }
  }

  // Checks all pending batches for completion and scavenges their entries.
  // This should be called as frequently as reasonable.
  void Scavenge() {
    while (pending_batch_list_head_) {
      auto batch = pending_batch_list_head_;
      if (vkGetFenceStatus(device_, batch->fence) == VK_SUCCESS) {
        // Batch has completed. Reclaim.
        pending_batch_list_head_ = batch->next;
        if (batch == pending_batch_list_tail_) {
          pending_batch_list_tail_ = nullptr;
        }
        batch->next = free_batch_list_head_;
        free_batch_list_head_ = batch;
        batch->entry_list_tail->next = free_entry_list_head_;
        free_entry_list_head_ = batch->entry_list_head;
        batch->entry_list_head = nullptr;
        batch->entry_list_tail = nullptr;
      } else {
        // Batch is still in-flight. Since batches are executed in order we know
        // no others after it could have completed, so early-exit.
        return;
      }
    }
  }

  // Begins a new batch.
  // All entries acquired within this batch will be marked as in-use until
  // the fence specified in EndBatch is signalled.
  void BeginBatch() {
    assert_null(open_batch_);
    Batch* batch = nullptr;
    if (free_batch_list_head_) {
      // Reuse a batch.
      batch = free_batch_list_head_;
      free_batch_list_head_ = batch->next;
      batch->next = nullptr;
    } else {
      // Allocate new batch.
      batch = new Batch();
      batch->next = nullptr;
    }
    batch->entry_list_head = nullptr;
    batch->entry_list_tail = nullptr;
    batch->fence = nullptr;
    open_batch_ = batch;
  }

  // Attempts to acquire an entry from the pool in the current batch.
  // If none are available a new one will be allocated.
  HANDLE AcquireEntry() {
    Entry* entry = nullptr;
    if (free_entry_list_head_) {
      // Slice off an entry from the free list.
      entry = free_entry_list_head_;
      free_entry_list_head_ = entry->next;
    } else {
      // No entry available; allocate new.
      entry = new Entry();
      entry->handle = static_cast<T*>(this)->AllocateEntry();
    }
    entry->next = nullptr;
    if (!open_batch_->entry_list_head) {
      open_batch_->entry_list_head = entry;
    }
    if (open_batch_->entry_list_tail) {
      open_batch_->entry_list_tail->next = entry;
    }
    open_batch_->entry_list_tail = entry;
    return entry->handle;
  }

  // Ends the current batch using the given fence to indicate when the batch
  // has completed execution on the GPU.
  void EndBatch(VkFence fence) {
    assert_not_null(open_batch_);

    // Close and see if we have anything.
    auto batch = open_batch_;
    open_batch_ = nullptr;
    if (!batch->entry_list_head) {
      // Nothing to do.
      batch->next = free_batch_list_head_;
      free_batch_list_head_ = batch;
      return;
    }

    // Track the fence.
    batch->fence = fence;

    // Append to the end of the batch list.
    batch->next = nullptr;
    if (!pending_batch_list_head_) {
      pending_batch_list_head_ = batch;
    }
    if (pending_batch_list_tail_) {
      pending_batch_list_tail_->next = batch;
    } else {
      pending_batch_list_tail_ = batch;
    }
  }

 protected:
  void PushEntry(HANDLE handle) {
    auto entry = new Entry();
    entry->next = free_entry_list_head_;
    entry->handle = handle;
    free_entry_list_head_ = entry;
  }

  VkDevice device_ = nullptr;

 private:
  struct Entry {
    Entry* next;
    HANDLE handle;
  };
  struct Batch {
    Batch* next;
    Entry* entry_list_head;
    Entry* entry_list_tail;
    VkFence fence;
  };

  Batch* free_batch_list_head_ = nullptr;
  Entry* free_entry_list_head_ = nullptr;
  Batch* pending_batch_list_head_ = nullptr;
  Batch* pending_batch_list_tail_ = nullptr;
  Batch* open_batch_ = nullptr;
};

class CommandBufferPool
    : public BaseFencedPool<CommandBufferPool, VkCommandBuffer> {
 public:
  CommandBufferPool(VkDevice device, uint32_t queue_family_index,
                    VkCommandBufferLevel level);
  ~CommandBufferPool() override;

 protected:
  friend class BaseFencedPool<CommandBufferPool, VkCommandBuffer>;
  VkCommandBuffer AllocateEntry();
  void FreeEntry(VkCommandBuffer handle);

  VkCommandPool command_pool_ = nullptr;
  VkCommandBufferLevel level_ = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
};

}  // namespace vulkan
}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_VULKAN_FENCED_POOLS_H_
