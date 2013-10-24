/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/kernel/xboxkrnl/object_table.h>

#include <xenia/kernel/xboxkrnl/xobject.h>
#include <xenia/kernel/xboxkrnl/objects/xthread.h>


using namespace xe;
using namespace xe::kernel;
using namespace xe::kernel::xboxkrnl;


ObjectTable::ObjectTable() :
    table_capacity_(0),
    table_(NULL),
    last_free_entry_(0) {
  table_mutex_ = xe_mutex_alloc(0);
  XEASSERTNOTNULL(table_mutex_);
}

ObjectTable::~ObjectTable() {
  xe_mutex_lock(table_mutex_);

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
  xe_free(table_);
  table_ = NULL;

  xe_mutex_unlock(table_mutex_);

  xe_mutex_free(table_mutex_);
  table_mutex_ = NULL;
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
  uint32_t new_table_capacity = MAX(16 * 1024, table_capacity_ * 2);
  ObjectTableEntry* new_table = (ObjectTableEntry*)xe_recalloc(
      table_,
      table_capacity_ * sizeof(ObjectTableEntry),
      new_table_capacity * sizeof(ObjectTableEntry));
  if (!new_table) {
    return X_STATUS_NO_MEMORY;
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
  XEASSERTNOTNULL(out_handle);

  X_STATUS result = X_STATUS_SUCCESS;

  xe_mutex_lock(table_mutex_);

  // Find a free slot.
  uint32_t slot = 0;
  result = FindFreeSlot(&slot);

  // Stash.
  if (XSUCCEEDED(result)) {
    ObjectTableEntry& entry = table_[slot];
    entry.object = object;

    // Retain so long as the object is in the table.
    object->RetainHandle();
    object->Retain();
  }

  xe_mutex_unlock(table_mutex_);

  if (XSUCCEEDED(result)) {
    *out_handle = slot << 2;
  }

  return result;
}

X_STATUS ObjectTable::RemoveHandle(X_HANDLE handle) {
  X_STATUS result = X_STATUS_SUCCESS;

  xe_mutex_lock(table_mutex_);

  // Lower 2 bits are ignored.
  uint32_t slot = handle >> 2;

  // Verify slot.
  XObject* object = NULL;
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
  }

  xe_mutex_unlock(table_mutex_);

  if (object) {
    // Release the object handle now that it is out of the table.
    object->ReleaseHandle();
    object->Release();
  }

  return result;
}

X_STATUS ObjectTable::GetObject(X_HANDLE handle, XObject** out_object) {
  XEASSERTNOTNULL(out_object);

  X_STATUS result = X_STATUS_SUCCESS;

  if (handle == 0xFFFFFFFF) {
    // CurrentProcess
    XEASSERTALWAYS();
  } else if (handle == 0xFFFFFFFE) {
    // CurrentThread
    handle = XThread::GetCurrentThreadHandle();
  }

  xe_mutex_lock(table_mutex_);

  // Lower 2 bits are ignored.
  uint32_t slot = handle >> 2;

  // Verify slot.
  XObject* object = NULL;
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

  xe_mutex_unlock(table_mutex_);

  *out_object = object;
  return result;
}
