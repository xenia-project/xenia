/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */
#include <cstring>

#include "xenia/base/logging.h"
#include "xenia/patcher/patcher.h"

namespace xe {
namespace patcher {

Patcher::Patcher(const std::filesystem::path patches_root) {
  is_any_patch_applied_ = false;
  patch_db_ = new PatchDB(patches_root);
}

void Patcher::ApplyPatchesForTitle(Memory* memory, const uint32_t title_id,
                                   const std::optional<uint64_t> hash) {
  const auto title_patches = patch_db_->GetTitlePatches(title_id, hash);

  for (const PatchFileEntry& patchFile : title_patches) {
    for (const PatchInfoEntry& patchEntry : patchFile.patch_info) {
      if (!patchEntry.is_enabled) {
        continue;
      }
      XELOGE("Patcher: Applying patch for: {}({:08X}) - {}",
             patchFile.title_name, patchFile.title_id, patchEntry.patch_name);
      ApplyPatch(memory, &patchEntry);
    }
  }
}

void Patcher::ApplyPatch(Memory* memory, const PatchInfoEntry* patch) {
  for (const PatchDataEntry& patch_data_entry : patch->patch_data) {
    uint32_t old_address_protect = 0;
    uint8_t* address = memory->TranslateVirtual(patch_data_entry.address);
    xe::BaseHeap* heap = memory->LookupHeap(patch_data_entry.address);
    if (!heap) {
      continue;
    }

    heap->QueryProtect(patch_data_entry.address, &old_address_protect);

    heap->Protect(patch_data_entry.address,
                  (uint32_t)patch_data_entry.data.alloc_size,
                  kMemoryProtectRead | kMemoryProtectWrite);

    std::memcpy(address, patch_data_entry.data.patch_data.data(),
                patch_data_entry.data.alloc_size);

    // Restore previous protection
    heap->Protect(patch_data_entry.address,
                  (uint32_t)patch_data_entry.data.alloc_size,
                  old_address_protect);

    is_any_patch_applied_ = true;
  }
}

}  // namespace patcher
}  // namespace xe
