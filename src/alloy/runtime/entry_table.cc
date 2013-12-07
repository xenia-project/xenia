/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <alloy/runtime/entry_table.h>

using namespace alloy;
using namespace alloy::runtime;


EntryTable::EntryTable() {
  lock_ = AllocMutex(10000);
}

EntryTable::~EntryTable() {
  LockMutex(lock_);
  EntryMap::iterator it = map_.begin();
  for (; it != map_.end(); ++it) {
    Entry* entry = it->second;
    delete entry;
  }
  UnlockMutex(lock_);
  FreeMutex(lock_);
}

Entry* EntryTable::Get(uint64_t address) {
  LockMutex(lock_);
  EntryMap::const_iterator it = map_.find(address);
  Entry* entry = it != map_.end() ? it->second : NULL;
  if (entry) {
    // TODO(benvanik): wait if needed?
    if (entry->status != Entry::STATUS_READY) {
      entry = NULL;
    }
  }
  UnlockMutex(lock_);
  return entry;
}

Entry::Status EntryTable::GetOrCreate(uint64_t address, Entry** out_entry) {
  LockMutex(lock_);
  EntryMap::const_iterator it = map_.find(address);
  Entry* entry = it != map_.end() ? it->second : NULL;
  Entry::Status status;
  if (entry) {
    // If we aren't ready yet spin and wait.
    if (entry->status == Entry::STATUS_COMPILING) {
      // Still compiling, so spin.
      do {
        UnlockMutex(lock_);
        // TODO(benvanik): sleep for less time?
        Sleep(0);
        LockMutex(lock_);
      } while (entry->status == Entry::STATUS_COMPILING);
    }
    status = entry->status;
  } else {
    // Create and return for initialization.
    entry = new Entry();
    entry->address = address;
    entry->status = Entry::STATUS_COMPILING;
    entry->function = 0;
    map_[address] = entry;
    status = Entry::STATUS_NEW;
  }
  UnlockMutex(lock_);
  *out_entry = entry;
  return status;
}
