/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/util/object_table.h"

#include <algorithm>
#include <cstring>

#include "xenia/base/byte_stream.h"
#include "xenia/base/logging.h"
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
  for (uint32_t n = 0; n < host_table_capacity_; n++) {
    ObjectTableEntry& entry = host_table_[n];
    if (entry.object) {
      entry.object->Release();
    }
  }

  table_capacity_ = 0;
  host_table_capacity_ = 0;
  last_free_entry_ = 0;
  last_free_host_entry_ = 0;
  free(table_);
  table_ = nullptr;
  free(host_table_);
  host_table_ = nullptr;
}

X_STATUS ObjectTable::FindFreeSlot(uint32_t* out_slot, bool host) {
  // Find a free slot.
  uint32_t slot = host ? last_free_host_entry_ : last_free_entry_;
  uint32_t capacity = host ? host_table_capacity_ : table_capacity_;
  uint32_t scan_count = 0;
  while (scan_count < capacity) {
    ObjectTableEntry& entry = host ? host_table_[slot] : table_[slot];
    if (!entry.object) {
      *out_slot = slot;
      return X_STATUS_SUCCESS;
    }
    scan_count++;
    slot = (slot + 1) % capacity;
    if (slot == 0 && host) {
      // Never allow 0 handles.
      scan_count++;
      slot++;
    }
  }

  // Table out of slots, expand.
  uint32_t new_table_capacity = std::max(16 * 1024u, capacity * 2);
  if (!Resize(new_table_capacity, host)) {
    return X_STATUS_NO_MEMORY;
  }

  // Never allow 0 handles on host.
  slot = host ? ++last_free_host_entry_ : last_free_entry_++;
  *out_slot = slot;

  return X_STATUS_SUCCESS;
}

bool ObjectTable::Resize(uint32_t new_capacity, bool host) {
  uint32_t capacity = host ? host_table_capacity_ : table_capacity_;
  uint32_t new_size = new_capacity * sizeof(ObjectTableEntry);
  uint32_t old_size = capacity * sizeof(ObjectTableEntry);
  auto new_table = reinterpret_cast<ObjectTableEntry*>(
      realloc(host ? host_table_ : table_, new_size));
  if (!new_table) {
    return false;
  }

  // Zero out new entries.
  if (new_size > old_size) {
    std::memset(reinterpret_cast<uint8_t*>(new_table) + old_size, 0,
                new_size - old_size);
  }

  if (host) {
    last_free_host_entry_ = capacity;
    host_table_capacity_ = new_capacity;
    host_table_ = new_table;
  } else {
    last_free_entry_ = capacity;
    table_capacity_ = new_capacity;
    table_ = new_table;
  }

  return true;
}

X_STATUS ObjectTable::AddHandle(XObject* object, X_HANDLE* out_handle) {
  X_STATUS result = X_STATUS_SUCCESS;

  uint32_t handle = 0;
  {
    auto global_lock = global_critical_region_.Acquire();

    // Find a free slot.
    uint32_t slot = 0;
    bool host_object = object->is_host_object();
    result = FindFreeSlot(&slot, host_object);

    // Stash.
    if (XSUCCEEDED(result)) {
      ObjectTableEntry& entry = host_object ? host_table_[slot] : table_[slot];
      entry.object = object;
      entry.handle_ref_count = 1;
      handle = slot << 2;
      if (!host_object) {
        if (object->type() != XObject::Type::Socket) {
          handle += XObject::kHandleBase;
        }
      } else {
        handle += XObject::kHandleHostBase;
      }
      object->handles().push_back(handle);

      // Retain so long as the object is in the table.
      object->Retain();

      XELOGI("Added handle:{:08X} for {}", handle, typeid(*object).name());
    }
  }

  if (XSUCCEEDED(result)) {
    if (out_handle) {
      *out_handle = handle;
    }
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

  ObjectTableEntry* entry = LookupTableInLock(handle);
  if (!entry) {
    return X_STATUS_INVALID_HANDLE;
  }

  entry->handle_ref_count++;
  return X_STATUS_SUCCESS;
}

X_STATUS ObjectTable::ReleaseHandle(X_HANDLE handle) {
  auto global_lock = global_critical_region_.Acquire();

  return ReleaseHandleInLock(handle);
}
X_STATUS ObjectTable::ReleaseHandleInLock(X_HANDLE handle) {
  ObjectTableEntry* entry = LookupTableInLock(handle);
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
  auto global_lock = global_critical_region_.Acquire();

  ObjectTableEntry* entry = LookupTableInLock(handle);
  if (!entry) {
    return X_STATUS_INVALID_HANDLE;
  }

  if (entry->object) {
    auto object = entry->object;
    entry->object = nullptr;
    assert_zero(entry->handle_ref_count);
    entry->handle_ref_count = 0;

    // Walk the object's handles and remove this one.
    auto handle_entry =
        std::find(object->handles().begin(), object->handles().end(), handle);
    if (handle_entry != object->handles().end()) {
      object->handles().erase(handle_entry);
    }

    XELOGI("Removed handle:{:08X} for {}", handle, typeid(*object).name());

    // Remove object name from mapping to prevent naming collision.
    if (!object->name().empty()) {
      RemoveNameMapping(object->name());
    }
    // Release now that the object has been removed from the table.
    object->Release();
  }

  return X_STATUS_SUCCESS;
}

std::vector<object_ref<XObject>> ObjectTable::GetAllObjects() {
  auto lock = global_critical_region_.Acquire();
  std::vector<object_ref<XObject>> results;

  for (uint32_t slot = 0; slot < host_table_capacity_; slot++) {
    auto& entry = host_table_[slot];
    if (entry.object && std::find(results.begin(), results.end(),
                                  entry.object) == results.end()) {
      entry.object->Retain();
      results.push_back(object_ref<XObject>(entry.object));
    }
  }
  for (uint32_t slot = 0; slot < table_capacity_; slot++) {
    auto& entry = table_[slot];
    if (entry.object && std::find(results.begin(), results.end(),
                                  entry.object) == results.end()) {
      entry.object->Retain();
      results.push_back(object_ref<XObject>(entry.object));
    }
  }

  return results;
}

void ObjectTable::PurgeAllObjects() {
  auto lock = global_critical_region_.Acquire();
  for (uint32_t slot = 0; slot < table_capacity_; slot++) {
    auto& entry = table_[slot];
    if (entry.object) {
      entry.handle_ref_count = 0;
      entry.object->Release();

      entry.object = nullptr;
    }
  }
}

ObjectTable::ObjectTableEntry* ObjectTable::LookupTable(X_HANDLE handle) {
  auto global_lock = global_critical_region_.Acquire();
  return LookupTableInLock(handle);
}

ObjectTable::ObjectTableEntry* ObjectTable::LookupTableInLock(X_HANDLE handle) {
  handle = TranslateHandle(handle);
  if (!handle) {
    return nullptr;
  }

  const bool is_host_object = XObject::is_handle_host_object(handle);
  uint32_t slot = GetHandleSlot(handle, is_host_object);
  if (is_host_object) {
    if (slot <= host_table_capacity_) {
      return &host_table_[slot];
    }
  } else if (slot <= table_capacity_) {
    return &table_[slot];
  }

  return nullptr;
}

// Generic lookup
template <>
object_ref<XObject> ObjectTable::LookupObject<XObject>(X_HANDLE handle,
                                                       bool already_locked) {
  auto object = ObjectTable::LookupObject(handle, already_locked);
  auto result = object_ref<XObject>(reinterpret_cast<XObject*>(object));
  return result;
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

  const bool is_host_object = XObject::is_handle_host_object(handle);
  uint32_t slot = GetHandleSlot(handle, is_host_object);

  // Verify slot.
  if (is_host_object) {
    if (slot < host_table_capacity_) {
      ObjectTableEntry& entry = host_table_[slot];
      if (entry.object) {
        object = entry.object;
      }
    }
  } else if (slot < table_capacity_) {
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
  for (uint32_t slot = 0; slot < host_table_capacity_; ++slot) {
    auto& entry = host_table_[slot];
    if (entry.object) {
      if (entry.object->type() == type) {
        entry.object->Retain();
        results->push_back(object_ref<XObject>(entry.object));
      }
    }
  }
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
  // chrispy: reordered these by likelihood, most likely case is that handle is
  // not a special handle
  XE_LIKELY_IF(handle < 0xFFFFFFFE) { return handle; }
  else if (handle == 0xFFFFFFFF) {
    return 0;
  }
  else {
    return XThread::GetCurrentThreadHandle();
  }
}

X_STATUS ObjectTable::AddNameMapping(const std::string_view name,
                                     X_HANDLE handle) {
  auto global_lock = global_critical_region_.Acquire();
  if (name_table_.count(string_key_case(name))) {
    return X_STATUS_OBJECT_NAME_COLLISION;
  }
  name_table_.insert({string_key_case::create(name), handle});
  return X_STATUS_SUCCESS;
}

void ObjectTable::RemoveNameMapping(const std::string_view name) {
  // Names are case-insensitive.
  auto global_lock = global_critical_region_.Acquire();
  auto it = name_table_.find(string_key_case(name));
  if (it != name_table_.end()) {
    name_table_.erase(it);
  }
}

X_STATUS ObjectTable::GetObjectByName(const std::string_view name,
                                      X_HANDLE* out_handle) {
  // Names are case-insensitive.
  auto global_lock = global_critical_region_.Acquire();
  auto it = name_table_.find(string_key_case(name));
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

bool ObjectTable::Save(ByteStream* stream) {
  stream->Write<uint32_t>(host_table_capacity_);
  for (uint32_t i = 0; i < host_table_capacity_; i++) {
    auto& entry = host_table_[i];
    stream->Write<int32_t>(entry.handle_ref_count);
  }

  stream->Write<uint32_t>(table_capacity_);
  for (uint32_t i = 0; i < table_capacity_; i++) {
    auto& entry = table_[i];
    stream->Write<int32_t>(entry.handle_ref_count);
  }

  return true;
}

bool ObjectTable::Restore(ByteStream* stream) {
  Resize(stream->Read<uint32_t>(), true);
  for (uint32_t i = 0; i < host_table_capacity_; i++) {
    auto& entry = host_table_[i];
    // entry.object = nullptr;
    entry.handle_ref_count = stream->Read<int32_t>();
  }

  Resize(stream->Read<uint32_t>(), false);
  for (uint32_t i = 0; i < table_capacity_; i++) {
    auto& entry = table_[i];
    // entry.object = nullptr;
    entry.handle_ref_count = stream->Read<int32_t>();
  }

  return true;
}

X_STATUS ObjectTable::RestoreHandle(X_HANDLE handle, XObject* object) {
  const bool is_host_object = XObject::is_handle_host_object(handle);
  uint32_t slot = GetHandleSlot(handle, is_host_object);
  uint32_t capacity = is_host_object ? host_table_capacity_ : table_capacity_;
  assert_true(capacity >= slot);

  if (capacity >= slot) {
    auto& entry = is_host_object ? host_table_[slot] : table_[slot];
    entry.object = object;
    object->Retain();
  }

  return X_STATUS_SUCCESS;
}

}  // namespace util
}  // namespace kernel
}  // namespace xe
