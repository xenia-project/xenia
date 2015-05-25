/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/object_table.h"

#include <algorithm>

#include "xenia/kernel/xobject.h"
#include "xenia/kernel/objects/xthread.h"

namespace xe {
namespace kernel {

ObjectTable::ObjectTable()
    : table_capacity_(0), table_(nullptr), last_free_entry_(0) {}

ObjectTable::~ObjectTable() {
  std::lock_guard<std::mutex> lock(table_mutex_);

  // Release all objects.
  for (uint32_t n = 0; n < table_capacity_; n++) {
    ObjectTableEntry& entry = table_[n];
    if (entry.object) {
      entry.object->ReleaseHandle();
      entry.object->Release();
    }
  }

  table_capacity_ = 0;
  last_free_entry_ = 0;
  free(table_);
  table_ = nullptr;
}

X_STATUS ObjectTable::FindFreeSlot(uint32_t* out_slot) {
  // Find a free slot.
  uint32_t slot = last_free_entry_;
  uint32_t scan_count = 0;
  while (scan_count < table_capacity_) {
    ObjectTableEntry& entry = table_[slot];
    if (!entry.object) {
      *out_slot = slot;
      return X_STATUS_SUCCESS;
    }
    scan_count++;
    slot = (slot + 1) % table_capacity_;
    if (slot == 0) {
      // Never allow 0 handles.
      scan_count++;
      slot++;
    }
  }

  // Table out of slots, expand.
  uint32_t new_table_capacity = std::max(16 * 1024u, table_capacity_ * 2);
  size_t new_table_size = new_table_capacity * sizeof(ObjectTableEntry);
  size_t old_table_size = table_capacity_ * sizeof(ObjectTableEntry);
  ObjectTableEntry* new_table =
      (ObjectTableEntry*)realloc(table_, new_table_size);
  if (!new_table) {
    return X_STATUS_NO_MEMORY;
  }
  // Zero out new memory.
  if (new_table_size > old_table_size) {
    memset(reinterpret_cast<uint8_t*>(new_table) + old_table_size, 0,
           new_table_size - old_table_size);
  }
  last_free_entry_ = table_capacity_;
  table_capacity_ = new_table_capacity;
  table_ = new_table;

  // Never allow 0 handles.
  slot = ++last_free_entry_;
  *out_slot = slot;

  return X_STATUS_SUCCESS;
}

X_STATUS ObjectTable::AddHandle(XObject* object, X_HANDLE* out_handle) {
  assert_not_null(out_handle);

  X_STATUS result = X_STATUS_SUCCESS;

  uint32_t slot = 0;
  {
    std::lock_guard<std::mutex> lock(table_mutex_);

    // Find a free slot.
    result = FindFreeSlot(&slot);

    // Stash.
    if (XSUCCEEDED(result)) {
      ObjectTableEntry& entry = table_[slot];
      entry.object = object;

      // Retain so long as the object is in the table.
      object->RetainHandle();
      object->Retain();
    }
  }

  if (XSUCCEEDED(result)) {
    *out_handle = slot << 2;
  }

  return result;
}

X_STATUS ObjectTable::RemoveHandle(X_HANDLE handle) {
  X_STATUS result = X_STATUS_SUCCESS;

  handle = TranslateHandle(handle);
  if (!handle) {
    return X_STATUS_INVALID_HANDLE;
  }

  XObject* object = NULL;
  {
    std::lock_guard<std::mutex> lock(table_mutex_);

    // Lower 2 bits are ignored.
    uint32_t slot = handle >> 2;

    // Verify slot.
    if (slot > table_capacity_) {
      result = X_STATUS_INVALID_HANDLE;
    } else {
      ObjectTableEntry& entry = table_[slot];
      if (entry.object) {
        // Release after we lose the lock.
        object = entry.object;
      } else {
        result = X_STATUS_INVALID_HANDLE;
      }
      entry.object = nullptr;
    }
  }

  if (object) {
    // Release the object handle now that it is out of the table.
    object->ReleaseHandle();
    object->Release();
  }

  return result;
}

X_STATUS ObjectTable::GetObject(X_HANDLE handle, XObject** out_object,
                                bool already_locked) {
  assert_not_null(out_object);

  X_STATUS result = X_STATUS_SUCCESS;

  handle = TranslateHandle(handle);
  if (!handle) {
    return X_STATUS_INVALID_HANDLE;
  }

  XObject* object = NULL;
  if (!already_locked) {
    table_mutex_.lock();
  }

  // Lower 2 bits are ignored.
  uint32_t slot = handle >> 2;

  // Verify slot.
  if (slot > table_capacity_) {
    result = X_STATUS_INVALID_HANDLE;
  } else {
    ObjectTableEntry& entry = table_[slot];
    if (entry.object) {
      object = entry.object;
    } else {
      result = X_STATUS_INVALID_HANDLE;
    }
  }

  // Retain the object pointer.
  if (object) {
    object->Retain();
  }

  if (!already_locked) {
    table_mutex_.unlock();
  }

  *out_object = object;
  return result;
}

X_HANDLE ObjectTable::TranslateHandle(X_HANDLE handle) {
  if (handle == 0xFFFFFFFF) {
    // CurrentProcess
    // assert_always();
    return 0;
  } else if (handle == 0xFFFFFFFE) {
    // CurrentThread
    return XThread::GetCurrentThreadHandle();
  } else {
    return handle;
  }
}

X_STATUS ObjectTable::AddNameMapping(const std::string& name, X_HANDLE handle) {
  std::lock_guard<std::mutex> lock(table_mutex_);
  if (name_table_.count(name)) {
    return X_STATUS_OBJECT_NAME_COLLISION;
  }
  name_table_.insert({name, handle});
  return X_STATUS_SUCCESS;
}

void ObjectTable::RemoveNameMapping(const std::string& name) {
  std::lock_guard<std::mutex> lock(table_mutex_);
  auto it = name_table_.find(name);
  if (it != name_table_.end()) {
    name_table_.erase(it);
  }
}

X_STATUS ObjectTable::GetObjectByName(const std::string& name,
                                      X_HANDLE* out_handle) {
  std::lock_guard<std::mutex> lock(table_mutex_);
  auto it = name_table_.find(name);
  if (it == name_table_.end()) {
    *out_handle = X_INVALID_HANDLE_VALUE;
    return X_STATUS_OBJECT_NAME_NOT_FOUND;
  }
  *out_handle = it->second;

  // We need to ref the handle. I think.
  XObject* obj = nullptr;
  if (XSUCCEEDED(GetObject(it->second, &obj, true))) {
    obj->RetainHandle();
    obj->Release();
  }

  return X_STATUS_SUCCESS;
}

}  // namespace kernel
}  // namespace xe
