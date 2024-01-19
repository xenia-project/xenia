/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_UTIL_OBJECT_TABLE_H_
#define XENIA_KERNEL_UTIL_OBJECT_TABLE_H_

#include <string>
#include <unordered_map>
#include <vector>

#include "xenia/base/mutex.h"
#include "xenia/base/string_key.h"
#include "xenia/kernel/xobject.h"
#include "xenia/xbox.h"

namespace xe {
class ByteStream;
}  // namespace xe

namespace xe {
namespace kernel {
namespace util {

class ObjectTable {
 public:
  ObjectTable();
  ~ObjectTable();

  void Reset();

  X_STATUS AddHandle(XObject* object, X_HANDLE* out_handle);
  X_STATUS DuplicateHandle(X_HANDLE orig, X_HANDLE* out_handle);
  X_STATUS RetainHandle(X_HANDLE handle);
  X_STATUS ReleaseHandle(X_HANDLE handle);
  X_STATUS ReleaseHandleInLock(X_HANDLE handle);
  X_STATUS RemoveHandle(X_HANDLE handle);

  bool Save(ByteStream* stream);
  bool Restore(ByteStream* stream);

  // Restores a XObject reference with a handle. Mainly for internal use - do
  // not use.
  X_STATUS RestoreHandle(X_HANDLE handle, XObject* object);
  template <typename T>
  object_ref<T> LookupObject(X_HANDLE handle, bool already_locked = false) {
    auto object = LookupObject(handle, already_locked);
    if (object) {
      assert_true(object->type() == T::kObjectType);
    }
    auto result = object_ref<T>(reinterpret_cast<T*>(object));
    return result;
  }

  X_STATUS AddNameMapping(const std::string_view name, X_HANDLE handle);
  void RemoveNameMapping(const std::string_view name);
  X_STATUS GetObjectByName(const std::string_view name, X_HANDLE* out_handle);
  template <typename T>
  std::vector<object_ref<T>> GetObjectsByType(XObject::Type type) {
    std::vector<object_ref<T>> results;
    GetObjectsByType(
        type, reinterpret_cast<std::vector<object_ref<XObject>>*>(&results));
    return results;
  }

  template <typename T>
  std::vector<object_ref<T>> GetObjectsByType() {
    std::vector<object_ref<T>> results;
    GetObjectsByType(
        T::kObjectType,
        reinterpret_cast<std::vector<object_ref<XObject>>*>(&results));
    return results;
  }

  std::vector<object_ref<XObject>> GetAllObjects();
  void PurgeAllObjects();  // Purges the object table of all guest objects

 private:
  struct ObjectTableEntry {
    int handle_ref_count = 0;
    XObject* object = nullptr;
  };
  ObjectTableEntry* LookupTableInLock(X_HANDLE handle);
  ObjectTableEntry* LookupTable(X_HANDLE handle);
  XObject* LookupObject(X_HANDLE handle, bool already_locked);
  void GetObjectsByType(XObject::Type type,
                        std::vector<object_ref<XObject>>* results);

  X_HANDLE TranslateHandle(X_HANDLE handle);
  static constexpr uint32_t GetHandleSlot(X_HANDLE handle, bool host) {
    handle &= host ? ~XObject::kHandleHostBase : ~XObject::kHandleBase;
    return handle >> 2;
  }
  X_STATUS FindFreeSlot(uint32_t* out_slot, bool host);
  bool Resize(uint32_t new_capacity, bool host);

  xe::global_critical_region global_critical_region_;
  uint32_t table_capacity_ = 0;
  uint32_t host_table_capacity_ = 0;
  ObjectTableEntry* table_ = nullptr;
  ObjectTableEntry* host_table_ = nullptr;
  uint32_t last_free_entry_ = 0;
  uint32_t last_free_host_entry_ = 0;
  std::unordered_map<string_key_case, X_HANDLE> name_table_;
};

// Generic lookup
template <>
object_ref<XObject> ObjectTable::LookupObject<XObject>(
    X_HANDLE handle, bool already_locked);

}  // namespace util
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_UTIL_OBJECT_TABLE_H_
