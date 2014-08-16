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

#include <xenia/common.h>
#include <xenia/core.h>

#include <xenia/xbox.h>


namespace xe {
namespace kernel {


class XObject;


class ObjectTable {
public:
  ObjectTable();
  ~ObjectTable();

  X_STATUS AddHandle(XObject* object, X_HANDLE* out_handle);
  X_STATUS RemoveHandle(X_HANDLE handle);
  X_STATUS GetObject(X_HANDLE handle, XObject** out_object);

private:
  X_HANDLE TranslateHandle(X_HANDLE handle);
  X_STATUS FindFreeSlot(uint32_t* out_slot);

  typedef struct {
    XObject* object;
  } ObjectTableEntry;

  std::mutex        table_mutex_;
  uint32_t          table_capacity_;
  ObjectTableEntry* table_;
  uint32_t          last_free_entry_;
};


}  // namespace kernel
}  // namespace xe


#endif  // XENIA_KERNEL_XBOXKRNL_OBJECT_TABLE_H_
