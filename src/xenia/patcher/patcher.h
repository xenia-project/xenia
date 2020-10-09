/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_PATCHER_H_
#define XENIA_PATCHER_H_

#include "xenia/memory.h"
#include "xenia/patcher/patch_db.h"

namespace xe {
namespace patcher {

class Patcher {
 public:
  Patcher(const std::filesystem::path patches_root);

  void ApplyPatch(Memory* memory, const PatchInfoEntry* patch);
  void ApplyPatchesForTitle(Memory* memory, const uint32_t title_id,
                            const std::optional<uint64_t> hash);

  bool IsAnyPatchApplied() { return is_any_patch_applied_; }

 private:
  PatchDB* patch_db_;
  bool is_any_patch_applied_;
};

}  // namespace patcher
}  // namespace xe
#endif
