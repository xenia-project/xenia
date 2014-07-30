/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef ALLOY_RUNTIME_ENTRY_TABLE_H_
#define ALLOY_RUNTIME_ENTRY_TABLE_H_

#include <mutex>
#include <unordered_map>
#include <vector>

#include <alloy/core.h>

namespace alloy {
namespace runtime {

class Function;

typedef struct Entry_t {
  typedef enum {
    STATUS_NEW = 0,
    STATUS_COMPILING,
    STATUS_READY,
    STATUS_FAILED,
  } Status;

  uint64_t address;
  uint64_t end_address;
  Status status;
  Function* function;
} Entry;

class EntryTable {
 public:
  EntryTable();
  ~EntryTable();

  Entry* Get(uint64_t address);
  Entry::Status GetOrCreate(uint64_t address, Entry** out_entry);

  std::vector<Function*> FindWithAddress(uint64_t address);

 private:
  // TODO(benvanik): replace with a better data structure.
  std::mutex lock_;
  std::unordered_map<uint64_t, Entry*> map_;
};

}  // namespace runtime
}  // namespace alloy

#endif  // ALLOY_RUNTIME_ENTRY_TABLE_H_
