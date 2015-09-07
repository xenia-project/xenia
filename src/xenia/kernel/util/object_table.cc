/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/util/object_table.h"

#include <algorithm>
#include <cstring>

#include "xenia/kernel/xobject.h"
#include "xenia/kernel/xthread.h"

namespace xe {
namespace kernel {
namespace util {

ObjectTable::ObjectTable() {}

ObjectTable::~ObjectTable() { Reset(); }

void ObjectTable::Reset() {
  auto global_lock = global_critical_region_.Acquire();

  // Release all objects.
  for (uint32_t n = 0; n < table_capacity_; n++) {
    ObjectTableEntry& entry = table_[n];
    if (entry.object) {
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
  auto new_table =
      reinterpret_cast<ObjectTableEntry*>(realloc(table_, new_table_size));
  if (!new_table) {
    return X_STATUS_NO_MEMORY;
  }
  // Zero out new memory.
  if (new_table_size > old_table_size) {
    std::memset(reinterpret_cast<uint8_t*>(new_table) + old_table_size, 0,
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
    auto global_lock = global_critical_region_.Acquire();

    // Find a free slot.
    result = FindFreeSlot(&slot);

    // Stash.
    if (XSUCCEEDED(result)) {
      ObjectTableEntry& entry = table_[slot];
      entry.object = object;
      entry.handle_ref_count = 1;

      // Retain so long as the object is in the table.
      object->Retain();
    }
  }

  if (XSUCCEEDED(result)) {
    *out_handle = slot << 2;
  }

  return result;
}

X_STATUS ObjectTable::DuplicateHandle(X_HANDLE handle, X_HANDLE* out_handle) {
  X_STATUS result = X_STATUS_SUCCESS;
  handle = TranslateHandle(handle);

  XObject* object = LookupObject(handle, false);
  if (object) {
    result = AddHandle(object, out_handle);
    object->Release();  // Release the ref that LookupObject took
  } else {
    result = X_STATUS_INVALID_HANDLE;
  }

  return result;
}

X_STATUS ObjectTable::RetainHandle(X_HANDLE handle) {
  auto global_lock = global_critical_region_.Acquire();

  ObjectTableEntry* entry = LookupTable(handle);
  if (!entry) {
    return X_STATUS_INVALID_HANDLE;
  }

  entry->handle_ref_count++;
  return X_STATUS_SUCCESS;
}

X_STATUS ObjectTable::ReleaseHandle(X_HANDLE handle) {
  auto global_lock = global_critical_region_.Acquire();

  ObjectTableEntry* entry = LookupTable(handle);
  if (!entry) {
    return X_STATUS_INVALID_HANDLE;
  }

  if (--entry->handle_ref_count == 0) {
    // No more references. Remove it from the table.
    return RemoveHandle(handle);
  }

  // FIXME: Return a status code telling the caller it wasn't released
  // (but not a failure code)
  return X_STATUS_SUCCESS;
}

X_STATUS ObjectTable::RemoveHandle(X_HANDLE handle) {
  X_STATUS result = X_STATUS_SUCCESS;

  handle = TranslateHandle(handle);
  if (!handle) {
    return X_STATUS_INVALID_HANDLE;
  }

  ObjectTableEntry* entry = LookupTable(handle);
  if (!entry) {
    return X_STATUS_INVALID_HANDLE;
  }

  auto global_lock = global_critical_region_.Acquire();
  if (entry->object) {
    auto object = entry->object;
    entry->object = nullptr;
    entry->handle_ref_count = 0;

    // Release now that the object has been removed from the table.
    object->Release();
  }

  return X_STATUS_SUCCESS;
}

ObjectTable::ObjectTableEntry* ObjectTable::LookupTable(X_HANDLE handle) {
  handle = TranslateHandle(handle);
  if (!handle) {
    return nullptr;
  }

  auto global_lock = global_critical_region_.Acquire();

  // Lower 2 bits are ignored.
  uint32_t slot = handle >> 2;
  if (slot <= table_capacity_) {
    return &table_[slot];
  }

  return nullptr;
}

XObject* ObjectTable::LookupObject(X_HANDLE handle, bool already_locked) {
  handle = TranslateHandle(handle);
  if (!handle) {
    return nullptr;
  }

  XObject* object = nullptr;
  if (!already_locked) {
    global_critical_region_.mutex().lock();
  }

  // Lower 2 bits are ignored.
  uint32_t slot = handle >> 2;

  // Verify slot.
  if (slot < table_capacity_) {
    ObjectTableEntry& entry = table_[slot];
    if (entry.object) {
      object = entry.object;
    }
  }

  // Retain the object pointer.
  if (object) {
    object->Retain();
  }

  if (!already_locked) {
    global_critical_region_.mutex().unlock();
  }

  return object;
}

void ObjectTable::GetObjectsByType(XObject::Type type,
                                   std::vector<object_ref<XObject>>* results) {
  auto global_lock = global_critical_region_.Acquire();
  for (uint32_t slot = 0; slot < table_capacity_; ++slot) {
    auto& entry = table_[slot];
    if (entry.object) {
      if (entry.object->type() == type) {
        entry.object->Retain();
        results->push_back(object_ref<XObject>(entry.object));
      }
    }
  }
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
  // Names are case-insensitive.
  std::string lower_name = name;
  std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(),
                 tolower);

  auto global_lock = global_critical_region_.Acquire();
  if (name_table_.count(lower_name)) {
    return X_STATUS_OBJECT_NAME_COLLISION;
  }
  name_table_.insert({lower_name, handle});
  return X_STATUS_SUCCESS;
}

void ObjectTable::RemoveNameMapping(const std::string& name) {
  // Names are case-insensitive.
  std::string lower_name = name;
  std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(),
                 tolower);

  auto global_lock = global_critical_region_.Acquire();
  auto it = name_table_.find(lower_name);
  if (it != name_table_.end()) {
    name_table_.erase(it);
  }
}

X_STATUS ObjectTable::GetObjectByName(const std::string& name,
                                      X_HANDLE* out_handle) {
  // Names are case-insensitive.
  std::string lower_name = name;
  std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(),
                 tolower);

  auto global_lock = global_critical_region_.Acquire();
  auto it = name_table_.find(lower_name);
  if (it == name_table_.end()) {
    *out_handle = X_INVALID_HANDLE_VALUE;
    return X_STATUS_OBJECT_NAME_NOT_FOUND;
  }
  *out_handle = it->second;

  // We need to ref the handle. I think.
  auto obj = LookupObject(it->second, true);
  if (obj) {
    obj->RetainHandle();
    obj->Release();
  }

  return X_STATUS_SUCCESS;
}

}  // namespace util
}  // namespace kernel
}  // namespace xe
