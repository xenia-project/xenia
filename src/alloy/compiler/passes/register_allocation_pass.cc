/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <alloy/compiler/passes/register_allocation_pass.h>

using namespace alloy;
using namespace alloy::backend;
using namespace alloy::compiler;
using namespace alloy::compiler::passes;
using namespace alloy::hir;


struct RegisterAllocationPass::Interval {
  uint32_t start_ordinal;
  uint32_t end_ordinal;
  Value* value;
  RegisterFreeUntilSet* free_until_set;
  // TODO(benvanik): reduce to offsets in arena?
  struct Interval* next;
  struct Interval* prev;

  void AddToList(Interval** list_head) {
    auto list_next = *list_head;
    this->next = list_next;
    if (list_next) {
      list_next->prev = this;
    }
    *list_head = this;
  }

  void InsertIntoList(Interval** list_head) {
    auto it = *list_head;
    while (it) {
      if (it->start_ordinal > this->start_ordinal) {
        // Went too far. Insert before this interval.
        this->prev = it->prev;
        this->next = it;
        if (it->prev) {
          it->prev->next = this;
        } else {
          *list_head = this;
        }
        it->prev = this;
        return;
      }
      if (!it->next) {
        // None found, add at tail.
        it->next = this;
        this->prev = it;
        return;
      }
      it = it->next;
    }
  }

  void RemoveFromList(Interval** list_head) {
    if (this->next) {
      this->next->prev = this->prev;
    }
    if (this->prev) {
      this->prev->next = this->next;
    } else {
      *list_head = this->next;
    }
    this->next = this->prev = NULL;
  }
};

struct RegisterAllocationPass::Intervals {
  Interval* unhandled;
  Interval* active;
  Interval* handled;
};

RegisterAllocationPass::RegisterAllocationPass(
    const MachineInfo* machine_info) :
    machine_info_(machine_info),
    CompilerPass() {
  // Initialize register sets. The values of these will be
  // cleared before use, so just the structure is required.
  auto mi_sets = machine_info->register_sets;
  xe_zero_struct(&free_until_sets_, sizeof(free_until_sets_));
  uint32_t n = 0;
  while (mi_sets[n].count) {
    auto& mi_set = mi_sets[n];
    auto free_until_set = new RegisterFreeUntilSet();
    free_until_sets_.all_sets[n] = free_until_set;
    free_until_set->count = mi_set.count;
    free_until_set->set = &mi_set;
    if (mi_set.types & MachineInfo::RegisterSet::INT_TYPES) {
      free_until_sets_.int_set = free_until_set;
    }
    if (mi_set.types & MachineInfo::RegisterSet::FLOAT_TYPES) {
      free_until_sets_.float_set = free_until_set;
    }
    if (mi_set.types & MachineInfo::RegisterSet::VEC_TYPES) {
      free_until_sets_.vec_set = free_until_set;
    }
    n++;
  }
}

RegisterAllocationPass::~RegisterAllocationPass() {
  for (size_t n = 0; n < XECOUNT(free_until_sets_.all_sets); n++) {
    if (!free_until_sets_.all_sets[n]) {
      break;
    }
    delete free_until_sets_.all_sets[n];
  }
}

int RegisterAllocationPass::Run(HIRBuilder* builder) {
  // A (probably broken) implementation of a linear scan register allocator
  // that operates directly on SSA form:
  // http://www.christianwimmer.at/Publications/Wimmer10a/Wimmer10a.pdf
  //
  // Requirements:
  // - SSA form (single definition for variables)
  // - block should be in linear order:
  //   - dominators *should* come before (a->b->c)
  //   - loop block sequences *should not* have intervening non-loop blocks

  auto arena = scratch_arena();

  // Renumber everything.
  uint32_t block_ordinal = 0;
  uint32_t instr_ordinal = 0;
  auto block = builder->first_block();
  while (block) {
    // Sequential block ordinals.
    block->ordinal = block_ordinal++;
    auto instr = block->instr_head;
    while (instr) {
      // Sequential global instruction ordinals.
      instr->ordinal = instr_ordinal++;
      instr = instr->next;
    }
    block = block->next;
  }

  // Compute all liveness ranges by walking forward through all
  // blocks/instructions and checking the last use of each value. This lets
  // us know the exact order in (block#,instr#) form, which is then used to
  // setup the range.
  // TODO(benvanik): ideally we would have a list of all values and not have
  //     to keep walking instructions over and over.
  Interval* prev_interval = NULL;
  Interval* head_interval = NULL;
  block = builder->first_block();
  while (block) {
    auto instr = block->instr_head;
    while (instr) {
      // Compute last-use for the dest value.
      // Since we know all values of importance must be defined, we can avoid
      // having to check every value and just look at dest.
      const OpcodeInfo* info = instr->opcode;
      if (GET_OPCODE_SIG_TYPE_DEST(info->signature) == OPCODE_SIG_TYPE_V) {
        auto v = instr->dest;
        if (!v->last_use) {
          ComputeLastUse(v);
        }

        // Add interval.
        auto interval = arena->Alloc<Interval>();
        interval->start_ordinal = instr->ordinal;
        interval->end_ordinal = v->last_use ?
            v->last_use->ordinal : v->def->ordinal;
        interval->value = v;
        interval->next = NULL;
        interval->prev = prev_interval;
        if (prev_interval) {
          prev_interval->next = interval;
        } else {
          head_interval = interval;
        }
        prev_interval = interval;

        // Grab register set to use.
        // We do this now so it's only once per interval, and it makes it easy
        // to only compare intervals that overlap their sets.
        if (v->type <= INT64_TYPE) {
          interval->free_until_set = free_until_sets_.int_set;
        } else if (v->type <= FLOAT64_TYPE) {
          interval->free_until_set = free_until_sets_.float_set;
        } else {
          interval->free_until_set = free_until_sets_.vec_set;
        }
      }

      instr = instr->next;
    }
    block = block->next;
  }

  // Now have a sorted list of intervals, minus their ending ordinals.
  Intervals intervals;
  intervals.unhandled = head_interval;
  intervals.active = intervals.handled = NULL;
  while (intervals.unhandled) {
    // Get next unhandled interval.
    auto current = intervals.unhandled;
    intervals.unhandled = intervals.unhandled->next;
    current->RemoveFromList(&intervals.unhandled);

    // Check for intervals in active that are handled or inactive.
    auto it = intervals.active;
    while (it) {
      auto next = it->next;
      if (it->end_ordinal <= current->start_ordinal) {
        // Move from active to handled.
        it->RemoveFromList(&intervals.active);
        it->AddToList(&intervals.handled);
      }
      it = next;
    }

    // Find a register for current.
    if (!TryAllocateFreeReg(current, intervals)) {
      // Failed, spill.
      AllocateBlockedReg(builder, current, intervals);
    }

    if (current->value->reg.index!= -1) {
      // Add current to active.
      current->AddToList(&intervals.active);
    }
  }

  return 0;
}

void RegisterAllocationPass::ComputeLastUse(Value* value) {
  // TODO(benvanik): compute during construction?
  // Note that this list isn't sorted (unfortunately), so we have to scan
  // them all.
  uint32_t max_ordinal = 0;
  Value::Use* last_use = NULL;
  auto use = value->use_head;
  while (use) {
    if (!last_use || use->instr->ordinal >= max_ordinal) {
      last_use = use;
      max_ordinal = use->instr->ordinal;
    }
    use = use->next;
  }
  value->last_use = last_use ? last_use->instr : NULL;
}

bool RegisterAllocationPass::TryAllocateFreeReg(
    Interval* current, Intervals& intervals) {
  // Reset all registers in the set to unused.
  auto free_until_set = current->free_until_set;
  for (uint32_t n = 0; n < free_until_set->count; n++) {
    free_until_set->pos[n] = -1;
  }

  // Mark all active registers as used.
  // TODO(benvanik): keep some kind of bitvector so that this is instant?
  auto it = intervals.active;
  while (it) {
    if (it->free_until_set == free_until_set) {
      free_until_set->pos[it->value->reg.index] = 0;
    }
    it = it->next;
  }

  uint32_t max_pos = 0;
  for (uint32_t n = 0; n < free_until_set->count; n++) {
    if (max_pos == -1) {
      max_pos = n;
    } else {
      if (free_until_set->pos[n] > free_until_set->pos[max_pos]) {
        max_pos = n;
      }
    }
  }
  if (!free_until_set->pos[max_pos]) {
    // No register available without spilling.
    return false;
  }
  if (current->end_ordinal < free_until_set->pos[max_pos]) {
    // Register available for the whole interval.
    current->value->reg.set = free_until_set->set;
    current->value->reg.index = max_pos;
  } else {
    // Register available for the first part of the interval.
    // Split the interval at where it hits the next one.
    //current->value->reg = max_pos;
    //SplitRange(current, free_until_set->pos[max_pos]);
    // TODO(benvanik): actually split -- for now we just spill.
    return false;
  }

  return true;
}

void RegisterAllocationPass::AllocateBlockedReg(
    HIRBuilder* builder, Interval* current, Intervals& intervals) {
  auto free_until_set = current->free_until_set;

  // TODO(benvanik): smart heuristics.
  //     wimmer AllocateBlockedReg has some stuff for deciding whether to
  //     spill current or some other active interval - which we ignore.

  // Pick a random interval. Maybe the first. Sure.
  auto spill_interval = intervals.active;
  Value* spill_value = NULL;
  Instr* prev_use = NULL;
  Instr* next_use = NULL;
  while (spill_interval) {
    if (spill_interval->free_until_set != free_until_set ||
        spill_interval->start_ordinal == current->start_ordinal) {
      // Only interested in ones of the same register set.
      // We also ensure that ones at the same ordinal as us are ignored,
      // which can happen with multiple local inserts/etc.
      spill_interval = spill_interval->next;
      continue;
    }
    spill_value = spill_interval->value;

    // Find the uses right before/after current.
    auto use = spill_value->use_head;
    while (use) {
      if (use->instr->ordinal != -1) {
        if (use->instr->ordinal < current->start_ordinal) {
          if (!prev_use || prev_use->ordinal < use->instr->ordinal) {
            prev_use = use->instr;
          }
        } else if (use->instr->ordinal > current->start_ordinal) {
          if (!next_use || next_use->ordinal > use->instr->ordinal) {
            next_use = use->instr;
          }
        }
      }
      use = use->next;
    }
    if (!prev_use) {
      prev_use = spill_value->def;
    }
    if (prev_use->next == next_use) {
      // Uh, this interval is way too short.
      spill_interval = spill_interval->next;
      continue;
    }
    XEASSERT(prev_use->ordinal != -1);
    XEASSERTNOTNULL(next_use);
    break;
  }
  XEASSERT(spill_interval->free_until_set == free_until_set);

  // Find the real last use -- paired ops may require sequences to stay
  // intact. This is a bad design.
  auto prev_def_tail = prev_use;
  while (prev_def_tail &&
          prev_def_tail->opcode->flags & OPCODE_FLAG_PAIRED_PREV) {
    prev_def_tail = prev_def_tail->prev;
  }

  Value* new_value;
  uint32_t end_ordinal;
  if (spill_value->local_slot) {
    // Value is already assigned a slot, so load from that.
    // We can then split the interval right after the previous use to
    // before the next use.

    // Update the last use of the spilled interval/value.
    end_ordinal = spill_interval->end_ordinal;
    spill_interval->end_ordinal = current->start_ordinal;//prev_def_tail->ordinal;
    XEASSERT(end_ordinal != -1);
    XEASSERT(spill_interval->end_ordinal != -1);

    // Insert a load right before the next use.
    new_value = builder->LoadLocal(spill_value->local_slot);
    builder->last_instr()->MoveBefore(next_use);

    // Update last use info.
    new_value->last_use = spill_value->last_use;
    spill_value->last_use = prev_use;
  } else {
    // Allocate a local slot.
    spill_value->local_slot = builder->AllocLocal(spill_value->type);

    // Insert a spill right after the def.
    builder->StoreLocal(spill_value->local_slot, spill_value);
    auto spill_store = builder->last_instr();
    spill_store->MoveBefore(prev_def_tail->next);

    // Update last use of spilled interval/value.
    end_ordinal = spill_interval->end_ordinal;
    spill_interval->end_ordinal = current->start_ordinal;//prev_def_tail->ordinal;
    XEASSERT(end_ordinal != -1);
    XEASSERT(spill_interval->end_ordinal != -1);

    // Insert a load right before the next use.
    new_value = builder->LoadLocal(spill_value->local_slot);
    builder->last_instr()->MoveBefore(next_use);

    // Update last use info.
    new_value->last_use = spill_value->last_use;
    spill_value->last_use = spill_store;
  }

  // Reuse the same local slot. Hooray SSA.
  new_value->local_slot = spill_value->local_slot;

  // Rename all future uses to that loaded value.
  auto use = spill_value->use_head;
  while (use) {
    // TODO(benvanik): keep use list sorted so we don't have to do this.
    if (use->instr->ordinal <= spill_interval->end_ordinal ||
        use->instr->ordinal == -1) {
      use = use->next;
      continue;
    }
    auto next = use->next;
    auto instr = use->instr;
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
    use = next;
  }

  // Create new interval.
  auto arena = scratch_arena();
  auto new_interval = arena->Alloc<Interval>();
  new_interval->start_ordinal = new_value->def->ordinal;
  new_interval->end_ordinal = end_ordinal;
  new_interval->value = new_value;
  new_interval->next = NULL;
  new_interval->prev = NULL;
  if (new_value->type <= INT64_TYPE) {
    new_interval->free_until_set = free_until_sets_.int_set;
  } else if (new_value->type <= FLOAT64_TYPE) {
    new_interval->free_until_set = free_until_sets_.float_set;
  } else {
    new_interval->free_until_set = free_until_sets_.vec_set;
  }

  // Remove the old interval from the active list, as it's been spilled.
  spill_interval->RemoveFromList(&intervals.active);
  spill_interval->AddToList(&intervals.handled);

  // Insert interval into the right place in the list.
  // We know it's ahead of us.
  new_interval->InsertIntoList(&intervals.unhandled);

  // TODO(benvanik): use the register we just freed?
  //current->value->reg.set = free_until_set->set;
  //current->value->reg.index = spill_interval->value->reg.index;
  bool allocated = TryAllocateFreeReg(current, intervals);
  XEASSERTTRUE(allocated);
}
