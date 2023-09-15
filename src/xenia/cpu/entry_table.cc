/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/entry_table.h"

#include "xenia/base/profiling.h"
#include "xenia/base/threading.h"

namespace xe {
namespace cpu {

EntryTable::EntryTable() = default;

EntryTable::~EntryTable() {
  auto global_lock = global_critical_region_.Acquire();
  for (auto it : map_) {
    Entry* entry = it.second;
    delete entry;
  }
}

Entry* EntryTable::Get(uint32_t address) {
  auto global_lock = global_critical_region_.Acquire();
  const auto& it = map_.find(address);
  Entry* entry = it != map_.end() ? it->second : nullptr;
  if (entry) {
    // TODO(benvanik): wait if needed?
    if (entry->status != Entry::STATUS_READY) {
      entry = nullptr;
    }
  }
  return entry;
}

Entry::Status EntryTable::GetOrCreate(uint32_t address, Entry** out_entry) {
  // TODO(benvanik): replace with a map with wait-free for find.
  // https://github.com/facebook/folly/blob/master/folly/AtomicHashMap.h

  auto global_lock = global_critical_region_.Acquire();
  const auto& it = map_.find(address);
  Entry* entry = it != map_.end() ? it->second : nullptr;
  Entry::Status status;
  if (entry) {
    // If we aren't ready yet spin and wait.
    if (entry->status == Entry::STATUS_COMPILING) {
      // Still compiling, so spin.
      do {
        global_lock.unlock();
        // TODO(benvanik): sleep for less time?
        xe::threading::Sleep(std::chrono::microseconds(10));
        global_lock.lock();
      } while (entry->status == Entry::STATUS_COMPILING);
    }
    status = entry->status;
  } else {
    // Create and return for initialization.
    entry = new Entry();
    entry->address = address;
    entry->end_address = 0;
    entry->status = Entry::STATUS_COMPILING;
    entry->function = 0;
    map_[address] = entry;
    status = Entry::STATUS_NEW;
  }
  global_lock.unlock();
  *out_entry = entry;
  return status;
}

void EntryTable::Delete(uint32_t address) {
  auto global_lock = global_critical_region_.Acquire();
  const auto itr = map_.find(address);

  if (itr != map_.cend()) {
    map_.erase(itr);
  }
}

std::vector<Function*> EntryTable::FindWithAddress(uint32_t address) {
  auto global_lock = global_critical_region_.Acquire();
  std::vector<Function*> fns;
  for (auto& it : map_) {
    Entry* entry = it.second;
    if (address >= entry->address && address <= entry->end_address) {
      if (entry->status == Entry::STATUS_READY) {
        fns.push_back(entry->function);
      }
    }
  }
  return fns;
}

}  // namespace cpu
}  // namespace xe
