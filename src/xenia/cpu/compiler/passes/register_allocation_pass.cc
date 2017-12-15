/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/compiler/passes/register_allocation_pass.h"

#include <algorithm>
#include <cstring>

#include "xenia/base/assert.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/base/profiling.h"

namespace xe {
namespace cpu {
namespace compiler {
namespace passes {

// TODO(benvanik): remove when enums redefined.
using namespace xe::cpu::hir;

using xe::cpu::backend::MachineInfo;
using xe::cpu::hir::HIRBuilder;
using xe::cpu::hir::Instr;
using xe::cpu::hir::OpcodeSignatureType;
using xe::cpu::hir::RegAssignment;
using xe::cpu::hir::TypeName;
using xe::cpu::hir::Value;

#define ASSERT_NO_CYCLES 0

RegisterAllocationPass::RegisterAllocationPass(const MachineInfo* machine_info)
    : CompilerPass() {
  // Initialize register sets.
  // TODO(benvanik): rewrite in a way that makes sense - this is terrible.
  auto mi_sets = machine_info->register_sets;
  std::memset(&usage_sets_, 0, sizeof(usage_sets_));
  uint32_t n = 0;
  while (mi_sets[n].count) {
    auto& mi_set = mi_sets[n];
    auto usage_set = new RegisterSetUsage();
    usage_sets_.all_sets[n] = usage_set;
    usage_set->count = mi_set.count;
    usage_set->set = &mi_set;
    if (mi_set.types & MachineInfo::RegisterSet::INT_TYPES) {
      usage_sets_.int_set = usage_set;
    }
    if (mi_set.types & MachineInfo::RegisterSet::FLOAT_TYPES) {
      usage_sets_.float_set = usage_set;
    }
    if (mi_set.types & MachineInfo::RegisterSet::VEC_TYPES) {
      usage_sets_.vec_set = usage_set;
    }
    n++;
  }
}

RegisterAllocationPass::~RegisterAllocationPass() {
  for (size_t n = 0; n < xe::countof(usage_sets_.all_sets); n++) {
    if (!usage_sets_.all_sets[n]) {
      break;
    }
    delete usage_sets_.all_sets[n];
  }
}

bool RegisterAllocationPass::Run(HIRBuilder* builder) {
  // Simple per-block allocator that operates on SSA form.
  // Registers do not move across blocks, though this could be
  // optimized with some intra-block analysis (dominators/etc).
  // Really, it'd just be nice to have someone who knew what they
  // were doing lower SSA and do this right.

  uint16_t block_ordinal = 0;
  uint32_t instr_ordinal = 0;
  auto block = builder->first_block();
  while (block) {
    // Sequential block ordinals.
    block->ordinal = block_ordinal++;

    // Reset all state.
    PrepareBlockState();

    // Renumber all instructions in the block. This is required so that
    // we can sort the usage pointers below.
    auto instr = block->instr_head;
    while (instr) {
      // Sequential global instruction ordinals.
      instr->ordinal = instr_ordinal++;
      instr = instr->next;
    }

    instr = block->instr_head;
    while (instr) {
      const auto info = instr->opcode;
      uint32_t signature = info->signature;

      // Update the register use heaps.
      AdvanceUses(instr);

      // Check sources for retirement. If any are unused after this instruction
      // we can eagerly evict them to speed up register allocation.
      // Since X64 (and other platforms) can often take advantage of dest==src1
      // register mappings we track retired src1 so that we can attempt to
      // reuse it.
      // NOTE: these checks require that the usage list be sorted!
      bool has_preferred_reg = false;
      RegAssignment preferred_reg = {0};
      if (GET_OPCODE_SIG_TYPE_SRC1(signature) == OPCODE_SIG_TYPE_V &&
          !instr->src1.value->IsConstant()) {
        if (!instr->src1_use->next) {
          // Pull off preferred register. We will try to reuse this for the
          // dest.
          // NOTE: set may be null if this is a store local.
          if (preferred_reg.set) {
            has_preferred_reg = true;
            preferred_reg = instr->src1.value->reg;
          }
        }
      }

      if (GET_OPCODE_SIG_TYPE_DEST(signature) == OPCODE_SIG_TYPE_V) {
        // Must not have been set already.
        assert_null(instr->dest->reg.set);

        // Sort the usage list. We depend on this in future uses of this
        // variable.
        SortUsageList(instr->dest);

        // If we have a preferred register, use that.
        // This way we can help along the stupid X86 two opcode instructions.
        bool allocated;
        if (has_preferred_reg) {
          // Allocate with the given preferred register. If the register is in
          // the wrong set it will not be reused.
          allocated = TryAllocateRegister(instr->dest, preferred_reg);
        } else {
          // Allocate a register. This will either reserve a free one or
          // spill and reuse an active one.
          allocated = TryAllocateRegister(instr->dest);
        }
        if (!allocated) {
          // Failed to allocate register -- need to spill and try again.
          // We spill only those registers we aren't using.
          if (!SpillOneRegister(builder, block, instr->dest->type)) {
            // Unable to spill anything - this shouldn't happen.
            XELOGE("Unable to spill any registers");
            assert_always();
            return false;
          }

          // Demand allocation.
          if (!TryAllocateRegister(instr->dest)) {
            // Boned.
            XELOGE("Register allocation failed");
            assert_always();
            return false;
          }
        }
      }

      instr = instr->next;
    }
    block = block->next;
  }

  return true;
}

void RegisterAllocationPass::DumpUsage(const char* name) {
#if 0
  fprintf(stdout, "\n%s:\n", name);
  for (size_t i = 0; i < xe::countof(usage_sets_.all_sets); ++i) {
    auto usage_set = usage_sets_.all_sets[i];
    if (usage_set) {
      fprintf(stdout, "set %s:\n", usage_set->set->name);
      fprintf(stdout, "  avail: %s\n",
              usage_set->availability.to_string().c_str());
      fprintf(stdout, "  upcoming uses:\n");
      for (auto it = usage_set->upcoming_uses.begin();
           it != usage_set->upcoming_uses.end(); ++it) {
        fprintf(stdout, "    v%d, used at %d\n",
                it->value->ordinal,
                it->use->instr->ordinal);
      }
    }
  }
  fflush(stdout);
#endif
}

void RegisterAllocationPass::PrepareBlockState() {
  for (size_t i = 0; i < xe::countof(usage_sets_.all_sets); ++i) {
    auto usage_set = usage_sets_.all_sets[i];
    if (usage_set) {
      usage_set->availability.set();
      usage_set->upcoming_uses.clear();
    }
  }
  DumpUsage("PrepareBlockState");
}

void RegisterAllocationPass::AdvanceUses(Instr* instr) {
  for (size_t i = 0; i < xe::countof(usage_sets_.all_sets); ++i) {
    auto usage_set = usage_sets_.all_sets[i];
    if (!usage_set) {
      break;
    }
    auto& upcoming_uses = usage_set->upcoming_uses;
    for (size_t j = 0; j < upcoming_uses.size();) {
      auto& upcoming_use = upcoming_uses.at(j);
      if (!upcoming_use.use) {
        // No uses at all - we can remove right away.
        // This comes up from instructions where the dest is never used,
        // like the ATOMIC ops.
        MarkRegAvailable(upcoming_use.value->reg);
        upcoming_uses.erase(upcoming_uses.begin() + j);
        // i remains the same.
        continue;
      }
      if (upcoming_use.use->instr != instr) {
        // Not yet at this instruction.
        ++j;
        continue;
      }
      // The use is from this instruction.
      if (!upcoming_use.use->next) {
        // Last use of the value. We can retire it now.
        MarkRegAvailable(upcoming_use.value->reg);
        upcoming_uses.erase(upcoming_uses.begin() + j);
        // i remains the same.
        continue;
      } else {
        // Used again. Push back the next use.
        // Note that we may be used multiple times this instruction, so
        // eat those.
        auto next_use = upcoming_use.use->next;
        while (next_use->next && next_use->instr == instr) {
          next_use = next_use->next;
        }
        // Remove the iterator.
        auto value = upcoming_use.value;
        upcoming_uses.erase(upcoming_uses.begin() + j);
        assert_true(next_use->instr->block == instr->block);
        assert_true(value->def->block == instr->block);
        upcoming_uses.emplace_back(value, next_use);
        // i remains the same.
        continue;
      }
    }
  }
  DumpUsage("AdvanceUses");
}

bool RegisterAllocationPass::IsRegInUse(const RegAssignment& reg) {
  RegisterSetUsage* usage_set;
  if (reg.set == usage_sets_.int_set->set) {
    usage_set = usage_sets_.int_set;
  } else if (reg.set == usage_sets_.float_set->set) {
    usage_set = usage_sets_.float_set;
  } else {
    usage_set = usage_sets_.vec_set;
  }
  return !usage_set->availability.test(reg.index);
}

RegisterAllocationPass::RegisterSetUsage* RegisterAllocationPass::MarkRegUsed(
    const RegAssignment& reg, Value* value, Value::Use* use) {
  auto usage_set = RegisterSetForValue(value);
  usage_set->availability.set(reg.index, false);
  usage_set->upcoming_uses.emplace_back(value, use);
  DumpUsage("MarkRegUsed");
  return usage_set;
}

RegisterAllocationPass::RegisterSetUsage*
RegisterAllocationPass::MarkRegAvailable(const hir::RegAssignment& reg) {
  RegisterSetUsage* usage_set;
  if (reg.set == usage_sets_.int_set->set) {
    usage_set = usage_sets_.int_set;
  } else if (reg.set == usage_sets_.float_set->set) {
    usage_set = usage_sets_.float_set;
  } else {
    usage_set = usage_sets_.vec_set;
  }
  usage_set->availability.set(reg.index, true);
  return usage_set;
}

bool RegisterAllocationPass::TryAllocateRegister(
    Value* value, const RegAssignment& preferred_reg) {
  // If the preferred register matches type and is available, use it.
  auto usage_set = RegisterSetForValue(value);
  if (usage_set->set == preferred_reg.set) {
    // Check if available.
    if (!IsRegInUse(preferred_reg)) {
      // Mark as in-use and return. Best case.
      MarkRegUsed(preferred_reg, value, value->use_head);
      value->reg = preferred_reg;
      return true;
    }
  }

  // Otherwise, fallback to allocating like normal.
  return TryAllocateRegister(value);
}

bool RegisterAllocationPass::TryAllocateRegister(Value* value) {
  // Get the set this register is in.
  RegisterSetUsage* usage_set = RegisterSetForValue(value);

  // Find the first free register, if any.
  // We have to ensure it's a valid one (in our count).
  uint32_t first_unused = 0;
  bool none_used = xe::bit_scan_forward(
      static_cast<uint32_t>(usage_set->availability.to_ulong()), &first_unused);
  if (none_used && first_unused < usage_set->count) {
    // Available! Use it!
    value->reg.set = usage_set->set;
    value->reg.index = first_unused;
    MarkRegUsed(value->reg, value, value->use_head);
    return true;
  }

  // None available! Spill required.
  return false;
}

bool RegisterAllocationPass::SpillOneRegister(HIRBuilder* builder, Block* block,
                                              TypeName required_type) {
  // Get the set that we will be picking from.
  RegisterSetUsage* usage_set;
  if (required_type <= INT64_TYPE) {
    usage_set = usage_sets_.int_set;
  } else if (required_type <= FLOAT64_TYPE) {
    usage_set = usage_sets_.float_set;
  } else {
    usage_set = usage_sets_.vec_set;
  }

  DumpUsage("SpillOneRegister (pre)");
  // Pick the one with the furthest next use.
  assert_true(!usage_set->upcoming_uses.empty());
  auto furthest_usage = std::max_element(usage_set->upcoming_uses.begin(),
                                         usage_set->upcoming_uses.end(),
                                         RegisterUsage::Comparer());
  assert_true(furthest_usage->value->def->block == block);
  assert_true(furthest_usage->use->instr->block == block);
  auto spill_value = furthest_usage->value;
  Value::Use* prev_use = furthest_usage->use->prev;
  Value::Use* next_use = furthest_usage->use;
  assert_not_null(next_use);
  usage_set->upcoming_uses.erase(furthest_usage);
  DumpUsage("SpillOneRegister (post)");
  const auto reg = spill_value->reg;

  // We know the spill_value use list is sorted, so we can cut it right now.
  // This makes it easier down below.
  auto new_head_use = next_use;

  // Allocate local.
  if (spill_value->local_slot) {
    // Value is already assigned a slot. Since we allocate in order and this is
    // all SSA we know the stored value will be exactly what we want. Yay,
    // we can prevent the redundant store!
    // In fact, we may even want to pin this spilled value so that we always
    // use the spilled value and prevent the need for more locals.
  } else {
    // Allocate a local slot.
    spill_value->local_slot = builder->AllocLocal(spill_value->type);

    // Add store.
    builder->StoreLocal(spill_value->local_slot, spill_value);
    auto spill_store = builder->last_instr();
    auto spill_store_use = spill_store->src2_use;
    assert_null(spill_store_use->prev);
    if (prev_use && prev_use->instr->opcode->flags & OPCODE_FLAG_PAIRED_PREV) {
      // Instruction is paired. This is bad. We will insert the spill after the
      // paired instruction.
      assert_not_null(prev_use->instr->next);
      spill_store->MoveBefore(prev_use->instr->next);

      // Update last use.
      spill_value->last_use = spill_store;
    } else if (prev_use) {
      // We insert the store immediately before the previous use.
      // If we were smarter we could then re-run allocation and reuse the
      // register
      // once dropped.
      spill_store->MoveBefore(prev_use->instr);

      // Update last use.
      spill_value->last_use = prev_use->instr;
    } else {
      // This is the first use, so the only thing we have is the define.
      // Move the store to right after that.
      spill_store->MoveBefore(spill_value->def->next);

      // Update last use.
      spill_value->last_use = spill_store;
    }
  }

#if ASSERT_NO_CYCLES
  builder->AssertNoCycles();
  spill_value->def->block->AssertNoCycles();
#endif  // ASSERT_NO_CYCLES

  // Add load.
  // Inserted immediately before the next use. Since by definition the next
  // use is after the instruction requesting the spill we know we haven't
  // done allocation for that code yet and can let that be handled
  // automatically when we get to it.
  auto new_value = builder->LoadLocal(spill_value->local_slot);
  auto spill_load = builder->last_instr();
  spill_load->MoveBefore(next_use->instr);
  // Note: implicit first use added.

#if ASSERT_NO_CYCLES
  builder->AssertNoCycles();
  spill_value->def->block->AssertNoCycles();
#endif  // ASSERT_NO_CYCLES

  // Set the local slot of the new value to our existing one. This way we will
  // reuse that same memory if needed.
  new_value->local_slot = spill_value->local_slot;

  // Rename all future uses of the SSA value to the new value as loaded
  // from the local.
  // We can quickly do this by walking the use list. Because the list is
  // already sorted we know we are going to end up with a sorted list.
  auto walk_use = new_head_use;
  auto new_use_tail = walk_use;
  while (walk_use) {
    auto next_walk_use = walk_use->next;
    auto instr = walk_use->instr;

    uint32_t signature = instr->opcode->signature;
    if (GET_OPCODE_SIG_TYPE_SRC1(signature) == OPCODE_SIG_TYPE_V) {
      if (instr->src1.value == spill_value) {
        instr->set_src1(new_value);
      }
    }
    if (GET_OPCODE_SIG_TYPE_SRC2(signature) == OPCODE_SIG_TYPE_V) {
      if (instr->src2.value == spill_value) {
        instr->set_src2(new_value);
      }
    }
    if (GET_OPCODE_SIG_TYPE_SRC3(signature) == OPCODE_SIG_TYPE_V) {
      if (instr->src3.value == spill_value) {
        instr->set_src3(new_value);
      }
    }

    walk_use = next_walk_use;
    if (walk_use) {
      new_use_tail = walk_use;
    }
  }
  new_value->last_use = new_use_tail->instr;

  // Update tracking.
  MarkRegAvailable(reg);

  return true;
}

RegisterAllocationPass::RegisterSetUsage*
RegisterAllocationPass::RegisterSetForValue(const Value* value) {
  if (value->type <= INT64_TYPE) {
    return usage_sets_.int_set;
  } else if (value->type <= FLOAT64_TYPE) {
    return usage_sets_.float_set;
  } else {
    return usage_sets_.vec_set;
  }
}

namespace {
int CompareValueUse(const Value::Use* a, const Value::Use* b) {
  return a->instr->ordinal - b->instr->ordinal;
}
}  // namespace
void RegisterAllocationPass::SortUsageList(Value* value) {
  // Modified in-place linked list sort from:
  // http://www.chiark.greenend.org.uk/~sgtatham/algorithms/listsort.c
  if (!value->use_head) {
    return;
  }
  Value::Use* head = value->use_head;
  Value::Use* tail = nullptr;
  int insize = 1;
  while (true) {
    auto p = head;
    head = nullptr;
    tail = nullptr;
    // count number of merges we do in this pass
    int nmerges = 0;
    while (p) {
      // there exists a merge to be done
      nmerges++;
      // step 'insize' places along from p
      auto q = p;
      int psize = 0;
      for (int i = 0; i < insize; i++) {
        psize++;
        q = q->next;
        if (!q) break;
      }
      // if q hasn't fallen off end, we have two lists to merge
      int qsize = insize;
      // now we have two lists; merge them
      while (psize > 0 || (qsize > 0 && q)) {
        // decide whether next element of merge comes from p or q
        Value::Use* e = nullptr;
        if (psize == 0) {
          // p is empty; e must come from q
          e = q;
          q = q->next;
          qsize--;
        } else if (qsize == 0 || !q) {
          // q is empty; e must come from p
          e = p;
          p = p->next;
          psize--;
        } else if (CompareValueUse(p, q) <= 0) {
          // First element of p is lower (or same); e must come from p
          e = p;
          p = p->next;
          psize--;
        } else {
          // First element of q is lower; e must come from q
          e = q;
          q = q->next;
          qsize--;
        }
        // add the next element to the merged list
        if (tail) {
          tail->next = e;
        } else {
          head = e;
        }
        // Maintain reverse pointers in a doubly linked list.
        e->prev = tail;
        tail = e;
      }
      // now p has stepped 'insize' places along, and q has too
      p = q;
    }
    if (tail) {
      tail->next = nullptr;
    }
    // If we have done only one merge, we're finished
    if (nmerges <= 1) {
      // allow for nmerges==0, the empty list case
      break;
    }
    // Otherwise repeat, merging lists twice the size
    insize *= 2;
  }

  value->use_head = head;
  value->last_use = tail->instr;
}

}  // namespace passes
}  // namespace compiler
}  // namespace cpu
}  // namespace xe
