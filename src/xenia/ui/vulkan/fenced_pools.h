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
#include "xenia/ui/vulkan/vulkan_util.h"

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

    // Subclasses must call FreeAllEntries() to properly clean up things.
    assert_null(free_batch_list_head_);
    assert_null(free_entry_list_head_);
  }

  // True if one or more batches are still pending on the GPU.
  bool has_pending() const { return pending_batch_list_head_ != nullptr; }
  // True if a batch is open.
  bool has_open_batch() const { return open_batch_ != nullptr; }

  // Checks all pending batches for completion and scavenges their entries.
  // This should be called as frequently as reasonable.
  void Scavenge() {
    while (pending_batch_list_head_) {
      auto batch = pending_batch_list_head_;
      assert_not_null(batch->fence);

      VkResult status = vkGetFenceStatus(device_, batch->fence);
      if (status == VK_SUCCESS || status == VK_ERROR_DEVICE_LOST) {
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
  // the fence returned is signalled.
  // Pass in a fence to use an external fence. This assumes the fence has been
  // reset.
  VkFence BeginBatch(VkFence fence = nullptr) {
    assert_null(open_batch_);
    Batch* batch = nullptr;
    if (free_batch_list_head_) {
      // Reuse a batch.
      batch = free_batch_list_head_;
      free_batch_list_head_ = batch->next;
      batch->next = nullptr;

      if (batch->flags & kBatchOwnsFence && !fence) {
        // Reset owned fence.
        vkResetFences(device_, 1, &batch->fence);
      } else if ((batch->flags & kBatchOwnsFence) && fence) {
        // Transfer owned -> external
        vkDestroyFence(device_, batch->fence, nullptr);
        batch->fence = fence;
      } else if (!(batch->flags & kBatchOwnsFence) && !fence) {
        // external -> owned
        VkFenceCreateInfo info;
        info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        info.pNext = nullptr;
        info.flags = 0;
        VkResult res = vkCreateFence(device_, &info, nullptr, &batch->fence);
        if (res != VK_SUCCESS) {
          assert_always();
        }

        batch->flags |= kBatchOwnsFence;
      } else {
        // external -> external
        batch->fence = fence;
      }
    } else {
      // Allocate new batch.
      batch = new Batch();
      batch->next = nullptr;
      batch->flags = 0;

      if (!fence) {
        VkFenceCreateInfo info;
        info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        info.pNext = nullptr;
        info.flags = 0;
        VkResult res = vkCreateFence(device_, &info, nullptr, &batch->fence);
        if (res != VK_SUCCESS) {
          assert_always();
        }

        batch->flags |= kBatchOwnsFence;
      } else {
        batch->fence = fence;
      }
    }
    batch->entry_list_head = nullptr;
    batch->entry_list_tail = nullptr;
    open_batch_ = batch;

    return batch->fence;
  }

  // Cancels an open batch, and releases all entries acquired within.
  void CancelBatch() {
    assert_not_null(open_batch_);

    auto batch = open_batch_;
    open_batch_ = nullptr;

    // Relink the batch back into the free batch list.
    batch->next = free_batch_list_head_;
    free_batch_list_head_ = batch;

    // Relink entries back into free entries list.
    batch->entry_list_tail->next = free_entry_list_head_;
    free_entry_list_head_ = batch->entry_list_head;
    batch->entry_list_head = nullptr;
    batch->entry_list_tail = nullptr;
  }

  // Ends the current batch.
  void EndBatch() {
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

    // Append to the end of the batch list.
    batch->next = nullptr;
    if (!pending_batch_list_head_) {
      pending_batch_list_head_ = batch;
    }
    if (pending_batch_list_tail_) {
      pending_batch_list_tail_->next = batch;
      pending_batch_list_tail_ = batch;
    } else {
      pending_batch_list_tail_ = batch;
    }
  }

 protected:
  // Attempts to acquire an entry from the pool in the current batch.
  // If none are available a new one will be allocated.
  HANDLE AcquireEntry(void* data) {
    Entry* entry = nullptr;
    if (free_entry_list_head_) {
      // Slice off an entry from the free list.
      Entry* prev = nullptr;
      Entry* cur = free_entry_list_head_;
      while (cur != nullptr) {
        if (cur->data == data) {
          if (prev) {
            prev->next = cur->next;
          } else {
            free_entry_list_head_ = cur->next;
          }

          entry = cur;
          break;
        }

        prev = cur;
        cur = cur->next;
      }
    }

    if (!entry) {
      // No entry available; allocate new.
      entry = new Entry();
      entry->data = data;
      entry->handle = static_cast<T*>(this)->AllocateEntry(data);
      if (!entry->handle) {
        delete entry;
        return nullptr;
      }
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

  void PushEntry(HANDLE handle, void* data) {
    auto entry = new Entry();
    entry->next = free_entry_list_head_;
    entry->data = data;
    entry->handle = handle;
    free_entry_list_head_ = entry;
  }

  void FreeAllEntries() {
    // Run down free lists.
    while (free_batch_list_head_) {
      auto batch = free_batch_list_head_;
      free_batch_list_head_ = batch->next;

      if (batch->flags & kBatchOwnsFence) {
        vkDestroyFence(device_, batch->fence, nullptr);
        batch->fence = nullptr;
      }
      delete batch;
    }
    while (free_entry_list_head_) {
      auto entry = free_entry_list_head_;
      free_entry_list_head_ = entry->next;
      static_cast<T*>(this)->FreeEntry(entry->handle);
      delete entry;
    }
  }

  VkDevice device_ = nullptr;

 private:
  struct Entry {
    Entry* next;
    void* data;
    HANDLE handle;
  };
  struct Batch {
    Batch* next;
    Entry* entry_list_head;
    Entry* entry_list_tail;
    uint32_t flags;
    VkFence fence;
  };

  static const uint32_t kBatchOwnsFence = 1;

  Batch* free_batch_list_head_ = nullptr;
  Entry* free_entry_list_head_ = nullptr;
  Batch* pending_batch_list_head_ = nullptr;
  Batch* pending_batch_list_tail_ = nullptr;
  Batch* open_batch_ = nullptr;
};

class CommandBufferPool
    : public BaseFencedPool<CommandBufferPool, VkCommandBuffer> {
 public:
  typedef BaseFencedPool<CommandBufferPool, VkCommandBuffer> Base;

  CommandBufferPool(VkDevice device, uint32_t queue_family_index);
  ~CommandBufferPool() override;

  VkCommandBuffer AcquireEntry(
      VkCommandBufferLevel level = VK_COMMAND_BUFFER_LEVEL_PRIMARY) {
    return Base::AcquireEntry(reinterpret_cast<void*>(level));
  }

 protected:
  friend class BaseFencedPool<CommandBufferPool, VkCommandBuffer>;
  VkCommandBuffer AllocateEntry(void* data);
  void FreeEntry(VkCommandBuffer handle);

  VkCommandPool command_pool_ = nullptr;
};

class DescriptorPool : public BaseFencedPool<DescriptorPool, VkDescriptorSet> {
 public:
  typedef BaseFencedPool<DescriptorPool, VkDescriptorSet> Base;

  DescriptorPool(VkDevice device, uint32_t max_count,
                 std::vector<VkDescriptorPoolSize> pool_sizes);
  ~DescriptorPool() override;

  VkDescriptorSet AcquireEntry(VkDescriptorSetLayout layout) {
    return Base::AcquireEntry(layout);
  }

  // WARNING: Allocating sets from the vulkan pool will not be tracked!
  VkDescriptorPool descriptor_pool() { return descriptor_pool_; }

 protected:
  friend class BaseFencedPool<DescriptorPool, VkDescriptorSet>;
  VkDescriptorSet AllocateEntry(void* data);
  void FreeEntry(VkDescriptorSet handle);

  VkDescriptorPool descriptor_pool_ = nullptr;
};

}  // namespace vulkan
}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_VULKAN_FENCED_POOLS_H_
