/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <alloy/runtime/entry_table.h>

namespace alloy {
namespace runtime {

EntryTable::EntryTable() = default;

EntryTable::~EntryTable() {
  std::lock_guard<std::mutex> guard(lock_);
  EntryMap::iterator it = map_.begin();
  for (; it != map_.end(); ++it) {
    Entry* entry = it->second;
    delete entry;
  }
}

Entry* EntryTable::Get(uint64_t address) {
  std::lock_guard<std::mutex> guard(lock_);
  EntryMap::const_iterator it = map_.find(address);
  Entry* entry = it != map_.end() ? it->second : NULL;
  if (entry) {
    // TODO(benvanik): wait if needed?
    if (entry->status != Entry::STATUS_READY) {
      entry = NULL;
    }
  }
  return entry;
}

Entry::Status EntryTable::GetOrCreate(uint64_t address, Entry** out_entry) {
  lock_.lock();
  EntryMap::const_iterator it = map_.find(address);
  Entry* entry = it != map_.end() ? it->second : NULL;
  Entry::Status status;
  if (entry) {
    // If we aren't ready yet spin and wait.
    if (entry->status == Entry::STATUS_COMPILING) {
      // Still compiling, so spin.
      do {
        lock_.unlock();
        // TODO(benvanik): sleep for less time?
        poly::threading::Sleep(std::chrono::microseconds(100));
        lock_.lock();
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
  lock_.unlock();
  *out_entry = entry;
  return status;
}

std::vector<Function*> EntryTable::FindWithAddress(uint64_t address) {
  SCOPE_profile_cpu_f("alloy");
  std::lock_guard<std::mutex> guard(lock_);
  std::vector<Function*> fns;
  for (auto it = map_.begin(); it != map_.end(); ++it) {
    Entry* entry = it->second;
    if (address >= entry->address && address <= entry->end_address) {
      if (entry->status == Entry::STATUS_READY) {
        fns.push_back(entry->function);
      }
    }
  }
  return fns;
}

}  // namespace runtime
}  // namespace alloy
