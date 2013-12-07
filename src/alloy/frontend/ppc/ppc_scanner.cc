/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <alloy/frontend/ppc/ppc_scanner.h>

#include <alloy/frontend/tracing.h>
#include <alloy/frontend/ppc/ppc_frontend.h>
#include <alloy/frontend/ppc/ppc_instr.h>

using namespace alloy;
using namespace alloy::frontend;
using namespace alloy::frontend::ppc;
using namespace alloy::runtime;


PPCScanner::PPCScanner(PPCFrontend* frontend) :
    frontend_(frontend) {
}

PPCScanner::~PPCScanner() {
}

bool PPCScanner::IsRestGprLr(uint64_t address) {
  // TODO(benvanik): detect type.
  /*FunctionSymbol* fn = GetFunction(addr);
  return fn && (fn->flags & FunctionSymbol::kFlagRestGprLr);*/
  return false;
}

int PPCScanner::FindExtents(FunctionInfo* symbol_info) {
  // This is a simple basic block analyizer. It walks the start address to the
  // end address looking for branches. Each span of instructions between
  // branches is considered a basic block. When the last blr (that has no
  // branches to after it) is found the function is considered ended. If this
  // is before the expected end address then the function address range is
  // split up and the second half is treated as another function.

  Memory* memory = frontend_->memory();
  const uint8_t* p = memory->membase();

  XELOGSDB("Analyzing function %.8X...", symbol_info->address());

  uint64_t start_address = symbol_info->address();
  uint64_t end_address = symbol_info->end_address();
  uint64_t address = start_address;
  uint64_t furthest_target = start_address;
  size_t blocks_found = 0;
  bool in_block = false;
  bool starts_with_mfspr_lr = false;
  InstrData i;
  while (true) {
    i.address = address;
    i.code = XEGETUINT32BE(p + address);

    // If we fetched 0 assume that we somehow hit one of the awesome
    // 'no really we meant to end after that bl' functions.
    if (!i.code) {
      XELOGSDB("function end %.8X (0x00000000 read)", address);
      // Don't include the 0's.
      address -= 4;
      break;
    }

    // TODO(benvanik): find a way to avoid using the opcode tables.
    // This lookup is *expensive* and should be avoided when scanning.
    i.type = GetInstrType(i.code);

    // Check if the function starts with a mfspr lr, as that's a good indication
    // of whether or not this is a normal function with a prolog/epilog.
    // Some valid leaf functions won't have this, but most will.
    if (address == start_address &&
        i.type &&
        i.type->opcode == 0x7C0002A6 &&
        (((i.XFX.spr & 0x1F) << 5) | ((i.XFX.spr >> 5) & 0x1F)) == 8) {
      starts_with_mfspr_lr = true;
    }

    if (!in_block) {
      in_block = true;
      blocks_found++;
    }

    bool ends_fn = false;
    bool ends_block = false;
    if (!i.type) {
      // Invalid instruction.
      // We can just ignore it because there's (very little)/no chance it'll
      // affect flow control.
      XELOGSDB("Invalid instruction at %.8X: %.8X", address, i.code);
    } else if (i.code == 0x4E800020) {
      // blr -- unconditional branch to LR.
      // This is generally a return.
      if (furthest_target > address) {
        // Remaining targets within function, not end.
        XELOGSDB("ignoring blr %.8X (branch to %.8X)",
                 address, furthest_target);
      } else {
        // Function end point.
        XELOGSDB("function end %.8X", address);
        ends_fn = true;
      }
      ends_block = true;
    } else if (i.code == 0x4E800420) {
      // bctr -- unconditional branch to CTR.
      // This is generally a jump to a function pointer (non-return).
      if (furthest_target > address) {
        // Remaining targets within function, not end.
        XELOGSDB("ignoring bctr %.8X (branch to %.8X)", address,
                 furthest_target);
      } else {
        // Function end point.
        XELOGSDB("function end %.8X", address);
        ends_fn = true;
      }
      ends_block = true;
    } else if (i.type->opcode == 0x48000000) {
      // b/ba/bl/bla
      uint32_t target =
          (uint32_t)XEEXTS26(i.I.LI << 2) + (i.I.AA ? 0 : (int32_t)address);
      
      if (i.I.LK) {
        XELOGSDB("bl %.8X -> %.8X", address, target);
        // Queue call target if needed.
        // GetOrInsertFunction(target);
      } else {
        XELOGSDB("b %.8X -> %.8X", address, target);

        // If the target is back into the function and there's no further target
        // we are at the end of a function.
        // (Indirect branches may still go beyond, but no way of knowing).
        if (target >= start_address &&
            target < address && furthest_target <= address) {
          XELOGSDB("function end %.8X (back b)", addr);
          ends_fn = true;
        }

        // If the target is not a branch and it goes to before the current
        // address it's definitely a tail call.
        if (!ends_fn &&
            target < start_address && furthest_target <= address) {
          XELOGSDB("function end %.8X (back b before addr)", addr);
          ends_fn = true;
        }

        // If the target is a __restgprlr_* method it's the end of a function.
        // Note that sometimes functions stick this in a basic block *inside*
        // of the function somewhere, so ensure we don't have any branches over
        // it.
        if (!ends_fn &&
            furthest_target <= address &&
            IsRestGprLr(target)) {
          XELOGSDB("function end %.8X (__restgprlr_*)", addr);
          ends_fn = true;
        }

        // Heuristic: if there's an unconditional branch in the first block of
        // the function it's likely a thunk.
        // Ex:
        //   li        r3, 0
        //   b         KeBugCheck
        // This check may hit on functions that jump over data code, so only
        // trigger this check in leaf functions (no mfspr lr/prolog).
        if (!ends_fn &&
            !starts_with_mfspr_lr &&
            blocks_found == 1) {
          XELOGSDB("HEURISTIC: ending at simple leaf thunk %.8X", address);
          ends_fn = true;
        }

        // Heuristic: if this is an unconditional branch at the end of the
        // function (nothing jumps over us) and we are jumping forward there's
        // a good chance it's a tail call.
        // This may not be true if the code is jumping over data/etc.
        // TODO(benvanik): figure out how to do this reliably. This check as is
        // is too aggressive and turns a lot of valid branches into tail calls.
        // It seems like a lot of functions end up with some prologue bit then
        // jump deep inside only to jump back towards the top soon after. May
        // need something more complex than just a simple 1-pass system to
        // detect these, unless more signals can be found.
        /*
        if (!ends_fn &&
            target > addr &&
            furthest_target < addr) {
          XELOGSDB("HEURISTIC: ending at tail call branch %.8X", addr);
          ends_fn = true;
        }
        */

        if (!ends_fn) {
          furthest_target = MAX(furthest_target, target);

          // TODO(benvanik): perhaps queue up for a speculative check? I think
          //     we are running over tail-call functions here that branch to
          //     somewhere else.
          //GetOrInsertFunction(target);
        }
      }
      ends_block = true;
    } else if (i.type->opcode == 0x40000000) {
      // bc/bca/bcl/bcla
      uint32_t target =
          (uint32_t)XEEXTS16(i.B.BD << 2) + (i.B.AA ? 0 : (int32_t)address);
      if (i.B.LK) {
        XELOGSDB("bcl %.8X -> %.8X", address, target);

        // Queue call target if needed.
        // TODO(benvanik): see if this is correct - not sure anyone makes
        //     function calls with bcl.
        //GetOrInsertFunction(target);
      } else {
        XELOGSDB("bc %.8X -> %.8X", address, target);

        // TODO(benvanik): GetOrInsertFunction? it's likely a BB

        furthest_target = MAX(furthest_target, target);
      }
      ends_block = true;
    } else if (i.type->opcode == 0x4C000020) {
      // bclr/bclrl
      if (i.XL.LK) {
        XELOGSDB("bclrl %.8X", addr);
      } else {
        XELOGSDB("bclr %.8X", addr);
      }
      ends_block = true;
    } else if (i.type->opcode == 0x4C000420) {
      // bcctr/bcctrl
      if (i.XL.LK) {
        XELOGSDB("bcctrl %.8X", addr);
      } else {
        XELOGSDB("bcctr %.8X", addr);
      }
      ends_block = true;
    }

    if (ends_block) {
      in_block = false;
    }
    if (ends_fn) {
      break;
    }

    address += 4;
    if (end_address && address > end_address) {
      // Hmm....
      XELOGSDB("Ran over function bounds! %.8X-%.8X",
               start_address, end_address);
      break;
    }
  }

  if (end_address && address + 4 < end_address) {
    // Ran under the expected value - since we probably got the initial bounds
    // from someplace valid (like method hints) this may indicate an error.
    // It's also possible that we guessed in hole-filling and there's another
    // function below this one.
    XELOGSDB("Function ran under: %.8X-%.8X ended at %.8X",
             start_address, end_address, address + 4);
  }
  symbol_info->set_end_address(address);

  // If there's spare bits at the end, split the function.
  // TODO(benvanik): splitting?

  // TODO(benvanik): find and record stack information
  // - look for __savegprlr_* and __restgprlr_*
  // - if present, flag function as needing a stack
  // - record prolog/epilog lengths/stack size/etc

  XELOGSDB("Finished analyzing %.8X", start_address);
  return 0;
}
