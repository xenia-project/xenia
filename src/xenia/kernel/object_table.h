/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XBOXKRNL_OBJECT_TABLE_H_
#define XENIA_KERNEL_XBOXKRNL_OBJECT_TABLE_H_

#include <mutex>
#include <string>
#include <unordered_map>

#include "xenia/base/mutex.h"
#include "xenia/kernel/xobject.h"
#include "xenia/xbox.h"

namespace xe {
namespace kernel {

class ObjectTable {
 public:
  ObjectTable();
  ~ObjectTable();

  X_STATUS AddHandle(XObject* object, X_HANDLE* out_handle);
  X_STATUS RemoveHandle(X_HANDLE handle);
  X_STATUS GetObject(X_HANDLE handle, XObject** out_object,
                     bool already_locked = false);
  template <typename T>
  object_ref<T> LookupObject(X_HANDLE handle) {
    auto object = LookupObject(handle, false);
    auto result = object_ref<T>(reinterpret_cast<T*>(object));
    return result;
  }

  X_STATUS AddNameMapping(const std::string& name, X_HANDLE handle);
  void RemoveNameMapping(const std::string& name);
  X_STATUS GetObjectByName(const std::string& name, X_HANDLE* out_handle);

 private:
  XObject* LookupObject(X_HANDLE handle, bool already_locked);

  X_HANDLE TranslateHandle(X_HANDLE handle);
  X_STATUS FindFreeSlot(uint32_t* out_slot);

  typedef struct { XObject* object; } ObjectTableEntry;

  xe::mutex table_mutex_;
  uint32_t table_capacity_;
  ObjectTableEntry* table_;
  uint32_t last_free_entry_;
  std::unordered_map<std::string, X_HANDLE> name_table_;
};

}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XBOXKRNL_OBJECT_TABLE_H_
