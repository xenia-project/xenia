/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2023 Xenia Canary. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/util/guest_object_table.h"
#include "xenia/base/atomic.h"
#include "xenia/cpu/processor.h"
#include "xenia/kernel/xboxkrnl/xboxkrnl_memory.h"
#include "xenia/kernel/xboxkrnl/xboxkrnl_threading.h"
namespace xe {
namespace kernel {
namespace util {

static constexpr uint32_t NUM_HANDLES_PER_BUCKET = 64;
static constexpr uint32_t SIZE_PER_HANDLE_BUCKET =
    sizeof(guest_handle_t) * NUM_HANDLES_PER_BUCKET;

// every time we need to reallocate the list of buckets, we allocate an
// additional BUCKET_SLOT_GROWTH slots
static constexpr uint32_t BUCKET_SLOT_GROWTH = 8;

// if set, the element is a reference to the next free slot, not an object
static constexpr uint32_t ELEMENT_IS_FREE_FLAG = 1;
static constexpr uint32_t HANDLE_MAX = 0xFFFFFF;

static constexpr uint32_t HandleToBucketOffset(guest_handle_t handle) {
  // yes, this does not divide by SIZE_PER_HANDLE_BUCKET, but the mask has the
  // low 2 bits clear and we shr by 6 so its really handle >> 8
  return (((handle & 0xFFFFFC) >> 6) & 0x3FFFFFC);
}

static constexpr uint32_t HandleToBucketElementOffset(guest_handle_t handle) {
  return handle & 0xFC;
}
void InitializeNewHandleRange(X_HANDLE_TABLE* table, PPCContext* context,
                              uint32_t bucket_base_handle,
                              uint32_t new_bucket) {
  uint32_t bucket_slot_addr =
      HandleToBucketOffset(bucket_base_handle) + table->table_dynamic_buckets;

  // insert the new bucket into its slot
  *context->TranslateVirtualBE<uint32_t>(bucket_slot_addr) = new_bucket;

  table->free_offset = bucket_base_handle;
  table->highest_allocated_offset = bucket_base_handle + SIZE_PER_HANDLE_BUCKET;

  auto bucket = context->TranslateVirtualBE<guest_handle_t>(new_bucket);

  /*
    initialize each bucket slot with a handle to the next free slot
    (bucket_handle_index+1) this is so we can read back the slot, update free
    ptr to that, and then store an object in NewObjectHandle

  */
  for (uint32_t bucket_handle_index = 0;
       bucket_handle_index < NUM_HANDLES_PER_BUCKET; ++bucket_handle_index) {
    bucket[bucket_handle_index] = (bucket_base_handle | ELEMENT_IS_FREE_FLAG) +
                                  ((bucket_handle_index + 1) * 4);
  }
}

bool GrowHandleTable(uint32_t table_ptr, PPCContext* context) {
  X_HANDLE_TABLE* table = context->TranslateVirtual<X_HANDLE_TABLE*>(table_ptr);

  guest_handle_t new_bucket_handle_base = table->highest_allocated_offset;
  if (new_bucket_handle_base >= HANDLE_MAX) {
    return false;
  }

  uint32_t new_bucket = xboxkrnl::xeAllocatePoolTypeWithTag(
      context, SIZE_PER_HANDLE_BUCKET, 'tHbO', table->unk_pool_arg_34);
  if (!new_bucket) {
    return false;
  }
  // this is exactly equal to (SIZE_PER_HANDLE_BUCKET*
  // countof(table_static_buckets)) - 1
  if ((new_bucket_handle_base & 0x7FF) != 0) {
    InitializeNewHandleRange(table, context, new_bucket_handle_base,
                             new_bucket);
    return true;
  }
  if (new_bucket_handle_base) {
    // bucket list realloc logic starts here
    uint32_t new_dynamic_buckets = xboxkrnl::xeAllocatePoolTypeWithTag(
        context,
        sizeof(uint32_t) * ((new_bucket_handle_base / SIZE_PER_HANDLE_BUCKET) +
                            BUCKET_SLOT_GROWTH),
        'rHbO', table->unk_pool_arg_34);
    if (new_dynamic_buckets) {
      /*
        copy old bucket list contents to new, larger bucket list
      */
      memcpy(context->TranslateVirtual(new_dynamic_buckets),
             context->TranslateVirtual(table->table_dynamic_buckets),
             sizeof(uint32_t) * (new_bucket_handle_base / SIZE_PER_HANDLE_BUCKET));

      if (context->TranslateVirtualBE<uint32_t>(table->table_dynamic_buckets) !=
          &table->table_static_buckets[0]) {
        xboxkrnl::xeFreePool(context, table->table_dynamic_buckets);
      }
      table->table_dynamic_buckets = new_dynamic_buckets;
      InitializeNewHandleRange(table, context, new_bucket_handle_base,
                               new_bucket);
      return true;
    }
    xboxkrnl::xeFreePool(context, new_bucket);
    return false;
  }
  table->table_dynamic_buckets =
      table_ptr + offsetof(X_HANDLE_TABLE, table_static_buckets);
  InitializeNewHandleRange(table, context, new_bucket_handle_base, new_bucket);
  return true;
}

uint32_t NewObjectHandle(uint32_t table_guest, uint32_t object_guest,
                         PPCContext* context) {
  X_HANDLE_TABLE* table =
      context->TranslateVirtual<X_HANDLE_TABLE*>(table_guest);

  X_OBJECT_HEADER* object = context->TranslateVirtual<X_OBJECT_HEADER*>(
      object_guest - sizeof(X_OBJECT_HEADER));

  guest_handle_t new_handle;

  xboxkrnl::xeKeKfAcquireSpinLock(context, &table->table_lock, false);
  {
    if (table->unk_36 ||
        table->free_offset == table->highest_allocated_offset &&
            !GrowHandleTable(table_guest, context)) {
      new_handle = 0;
    } else {
      guest_handle_t new_handle_offset = table->free_offset;
      uint32_t bucket = *context->TranslateVirtualBE<uint32_t>(
          HandleToBucketOffset(new_handle_offset) +
          table->table_dynamic_buckets);
      auto object_ptr_dest = context->TranslateVirtualBE<uint32_t>(
          bucket + HandleToBucketElementOffset(new_handle_offset));

      // see end of InitializeNewHandleRange, each slot contains the offset of
      // the next free slot
      uint32_t next_free_slot = *object_ptr_dest;

      table->free_offset = next_free_slot & ~ELEMENT_IS_FREE_FLAG;
      table->num_handles++;

      // this object header field is not atomic, because we're already under the
      // table lock whenever we make changes to it
      ++object->handle_count;

      *object_ptr_dest = object_guest;
      new_handle = (static_cast<uint32_t>(table->handle_high_byte) << 24) |
                   new_handle_offset;
    }
  }
  xboxkrnl::xeKeKfReleaseSpinLock(context, &table->table_lock, 0, false);

  return new_handle;
}

uint32_t DestroyObjectHandle(uint32_t table_guest, uint32_t handle,
                             PPCContext* context) {
  X_HANDLE_TABLE* table =
      context->TranslateVirtual<X_HANDLE_TABLE*>(table_guest);

  xboxkrnl::xeKeKfAcquireSpinLock(context, &table->table_lock, false);
  unsigned int result = 0;
  {
    if ((handle >> 24) != table->handle_high_byte) {
      xenia_assert(false);

    } else {
      uint32_t handle_sans_flags_and_high = handle & 0xFFFFFC;

      if (handle_sans_flags_and_high < table->highest_allocated_offset) {
        uint32_t bucket_for_handle = *context->TranslateVirtualBE<uint32_t>(
            HandleToBucketOffset(handle_sans_flags_and_high) +
            table->table_dynamic_buckets);

        uint32_t bucket_element_guest_ptr =
            bucket_for_handle + HandleToBucketElementOffset(handle);
        if (bucket_element_guest_ptr) {
          auto bucket_element_ptr =
              context->TranslateVirtualBE<uint32_t>(bucket_element_guest_ptr);

          uint32_t bucket_element = *bucket_element_ptr;
          if ((bucket_element & ELEMENT_IS_FREE_FLAG) == 0) {
            result = bucket_element & ~2;
            *bucket_element_ptr = table->free_offset | ELEMENT_IS_FREE_FLAG;
            table->free_offset = handle_sans_flags_and_high;
            table->num_handles--;
          }
        }

      } else {
        xenia_assert(false);
      }
    }
  }

  xboxkrnl::xeKeKfReleaseSpinLock(context, &table->table_lock, 0, false);

  return result;
}

uint32_t LookupHandleUnlocked(X_HANDLE_TABLE* table, guest_handle_t handle,
                              bool reference_object, PPCContext* context) {
  uint32_t result_object = 0;

  if ((handle >> 24) != table->handle_high_byte) {
    return 0U;
  }
  if ((handle & 0xFFFFFC) >= table->highest_allocated_offset) {
    return 0U;
  }
  uint32_t bucket_element_guest_ptr =
      *context->TranslateVirtualBE<uint32_t>(HandleToBucketOffset(handle) +
                                             table->table_dynamic_buckets) +
      HandleToBucketElementOffset(handle);
  if (bucket_element_guest_ptr != 0) {
    uint32_t bucket_element =
        *context->TranslateVirtualBE<uint32_t>(bucket_element_guest_ptr);
    result_object = bucket_element & ~2U;

    if ((bucket_element & ELEMENT_IS_FREE_FLAG) == 0) {
      if (reference_object) {
        X_OBJECT_HEADER* header = context->TranslateVirtual<X_OBJECT_HEADER*>(
            result_object - sizeof(X_OBJECT_HEADER));

        context->processor->GuestAtomicIncrement32(
            context, context->HostToGuestVirtual(&header->pointer_count));
      }
    } else {
      result_object = 0;
    }
  } else {
    result_object = 0;
  }
  return result_object;
}

uint32_t LookupHandle(uint32_t table, uint32_t handle,
                      uint32_t reference_object, PPCContext* context) {
  X_HANDLE_TABLE* table_ptr = context->TranslateVirtual<X_HANDLE_TABLE*>(table);
  uint32_t old_irql =
      xboxkrnl::xeKeKfAcquireSpinLock(context, &table_ptr->table_lock);

  uint32_t result =
      LookupHandleUnlocked(table_ptr, handle, reference_object, context);

  xboxkrnl::xeKeKfReleaseSpinLock(context, &table_ptr->table_lock, old_irql);

  return result;
}

}  // namespace util
}  // namespace kernel
}  // namespace xe
