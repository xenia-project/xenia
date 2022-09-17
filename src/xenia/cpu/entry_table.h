/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_ENTRY_TABLE_H_
#define XENIA_CPU_ENTRY_TABLE_H_

#include <unordered_map>
#include <vector>

#include "xenia/base/mutex.h"
#include "xenia/base/split_map.h"
namespace xe {
namespace cpu {

class Function;

typedef struct Entry_t {
  typedef enum {
    STATUS_NEW = 0,
    STATUS_COMPILING,
    STATUS_READY,
    STATUS_FAILED,
  } Status;

  uint32_t address;
  uint32_t end_address;
  Status status;
  Function* function;
} Entry;

class EntryTable {
 public:
  EntryTable();
  ~EntryTable();

  Entry* Get(uint32_t address);
  Entry::Status GetOrCreate(uint32_t address, Entry** out_entry);
  void Delete(uint32_t address);

  std::vector<Function*> FindWithAddress(uint32_t address);

 private:
  xe::global_critical_region global_critical_region_;
  // TODO(benvanik): replace with a better data structure.
  xe::split_map<uint32_t, Entry*> map_;
  //std::unordered_map<uint32_t, Entry*> map_;
};

}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_ENTRY_TABLE_H_
