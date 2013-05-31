/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/cpu/sdb/symbol_database.h>

#include <fstream>
#include <sstream>

#include <xenia/cpu/ppc/instr.h>


using namespace std;
using namespace xe;
using namespace xe::cpu;
using namespace xe::cpu::ppc;
using namespace xe::cpu::sdb;
using namespace xe::kernel;


SymbolDatabase::SymbolDatabase(xe_memory_ref memory,
                               ExportResolver* export_resolver,
                               SymbolTable* sym_table) {
  memory_ = xe_memory_retain(memory);
  export_resolver_ = export_resolver;
  sym_table_ = sym_table;
}

SymbolDatabase::~SymbolDatabase() {
  for (SymbolMap::iterator it = symbols_.begin(); it != symbols_.end(); ++it) {
    delete it->second;
  }

  xe_memory_release(memory_);
}

int SymbolDatabase::Analyze() {
  // Iteratively run passes over the db.
  // This uses a queue to do a breadth-first search of all accessible
  // functions. Callbacks and such likely won't be hit.

  // Queue entry point of the application.
  FunctionSymbol* fn = GetOrInsertFunction(GetEntryPoint());
  fn->set_name("start");

  // Keep pumping the queue until there's nothing left to do.
  FlushQueue();

  // Do a pass over the functions to fill holes. A few times. Just to be safe.
  for (size_t n = 0; n < 4; n++) {
    if (!FillHoles()) {
      break;
    }
    FlushQueue();
  }

  // Run a pass over all functions and link up their extended data.
  // This can only be performed after we have all functions and basic blocks.
  bool needs_another_pass = false;
  do {
    needs_another_pass = false;
    for (SymbolMap::iterator it = symbols_.begin(); it != symbols_.end();
         ++it) {
      if (it->second->symbol_type == Symbol::Function) {
        if (fn->type == FunctionSymbol::Unknown) {
          XELOGE("UNKNOWN FN %.8X", fn->start_address);
        }
        if (CompleteFunctionGraph(static_cast<FunctionSymbol*>(it->second))) {
          needs_another_pass = true;
        }
      }
    }

    if (needs_another_pass) {
      FlushQueue();
    }
  } while (needs_another_pass);

  return 0;
}

Symbol* SymbolDatabase::GetSymbol(uint32_t address) {
  SymbolMap::iterator i = symbols_.find(address);
  if (i != symbols_.end()) {
    return i->second;
  }
  return NULL;
}

ExceptionEntrySymbol* SymbolDatabase::GetOrInsertExceptionEntry(
    uint32_t address) {
  SymbolMap::iterator i = symbols_.find(address);
  if (i != symbols_.end() && i->second->symbol_type == Symbol::Function) {
    return static_cast<ExceptionEntrySymbol*>(i->second);
  }

  ExceptionEntrySymbol* ee = new ExceptionEntrySymbol();
  ee->address = address;
  symbols_.insert(SymbolMap::value_type(address, ee));
  return ee;
}

FunctionSymbol* SymbolDatabase::GetOrInsertFunction(
    uint32_t address, FunctionSymbol* opt_call_source) {
  FunctionSymbol* fn = GetFunction(address);
  if (fn) {
    if (opt_call_source) {
      FunctionSymbol::AddCall(opt_call_source, fn);
    }
    return fn;
  }

  // Ignore values outside of the .text range.
  if (!IsValueInTextRange(address)) {
    XELOGSDB("Ignoring function outside of .text: %.8X", address);
    return NULL;
  }

  fn = new FunctionSymbol();
  fn->start_address = address;
  function_count_++;
  symbols_.insert(SymbolMap::value_type(address, fn));
  scan_queue_.push_back(fn);

  if (opt_call_source) {
    FunctionSymbol::AddCall(opt_call_source, fn);
  }

  return fn;
}

VariableSymbol* SymbolDatabase::GetOrInsertVariable(uint32_t address) {
  VariableSymbol* var = GetVariable(address);
  if (var) {
    return var;
  }

  var = new VariableSymbol();
  var->address = address;
  variable_count_++;
  symbols_.insert(SymbolMap::value_type(address, var));
  return var;
}

FunctionSymbol* SymbolDatabase::GetFunction(uint32_t address, bool analyze) {
  XEASSERT(address);

  SymbolMap::iterator i = symbols_.find(address);
  if (i != symbols_.end() && i->second->symbol_type == Symbol::Function) {
    return static_cast<FunctionSymbol*>(i->second);
  }

  // Missing function - analyze on demand.
  // TODO(benvanik): track this for statistics.
  FunctionSymbol* fn = new FunctionSymbol();
  fn->start_address = address;
  function_count_++;
  symbols_.insert(SymbolMap::value_type(address, fn));
  scan_queue_.push_back(fn);

  if (analyze) {
    FlushQueue();
  }

  return fn;
}

VariableSymbol* SymbolDatabase::GetVariable(uint32_t address) {
  SymbolMap::iterator i = symbols_.find(address);
  if (i != symbols_.end() && i->second->symbol_type == Symbol::Variable) {
    return static_cast<VariableSymbol*>(i->second);
  }
  return NULL;
}

int SymbolDatabase::GetAllVariables(std::vector<VariableSymbol*>& variables) {
  for (SymbolMap::iterator it = symbols_.begin(); it != symbols_.end(); ++it) {
    if (it->second->symbol_type == Symbol::Variable) {
      variables.push_back(static_cast<VariableSymbol*>(it->second));
    }
  }
  return 0;
}

int SymbolDatabase::GetAllFunctions(vector<FunctionSymbol*>& functions) {
  for (SymbolMap::iterator it = symbols_.begin(); it != symbols_.end(); ++it) {
    if (it->second->symbol_type == Symbol::Function) {
      functions.push_back(static_cast<FunctionSymbol*>(it->second));
    }
  }
  return 0;
}

int SymbolDatabase::AnalyzeFunction(FunctionSymbol* fn) {
  // Ignore functions already analyzed.
  if (fn->blocks.size()) {
    return 0;
  }
  // Ignore kernel thunks.
  if (fn->type == FunctionSymbol::Kernel) {
    return 0;
  }
  // Ignore bad inserts?
  if (fn->start_address == fn->end_address) {
    return 0;
  }

  // This is a simple basic block analyizer. It walks the start address to the
  // end address looking for branches. Each span of instructions between
  // branches is considered a basic block, and the blocks are linked up to
  // create a CFG for the function. When the last blr (that has no branches
  // to after it) is found the function is considered ended. If this is before
  // the expected end address then the function address range is split up and
  // the second half is treated as another function.

  // TODO(benvanik): special branch checks:
  // bl to _XamLoaderTerminateTitle should be treated as b
  // bl to KeBugCheck should be treated as b, and b KeBugCheck should die

  // TODO(benvanik): identify thunks:
  // These look like:
  //   li r5, 0
  //   [etc]
  //   b some_function
  // Can probably be detected by lack of use of LR?

  uint8_t* p = xe_memory_addr(memory_, 0);

  if (XEGETUINT32LE(p + fn->start_address) == 0) {
    // Function starts with 0x00000000 - we want to skip this and split.
    symbols_.erase(fn->start_address);
    // Scan ahead until the first non-zero or the end of the valid range.
    uint32_t next_addr = fn->start_address + 4;
    while (true) {
      if (!IsValueInTextRange(next_addr)) {
        // Ran out of the range. Abort.
        delete fn;
        return 0;
      }
      if (XEGETUINT32LE(p + next_addr)) {
        // Not a zero, maybe valid!
        break;
      }
      next_addr += 4;
    }
    if (!GetFunction(next_addr + 4)) {
      fn->start_address = next_addr;
      symbols_.insert(SymbolMap::value_type(fn->start_address, fn));
      scan_queue_.push_back(fn);
    } else {
      delete fn;
    }
    return 0;
  }

  XELOGSDB("Analyzing function %.8X...", fn->start_address);

  // Set a default name, if it hasn't been named already.
  if (!fn->name()) {
    char name[32];
    xesnprintfa(name, XECOUNT(name), "sub_%.8X", fn->start_address);
    fn->set_name(name);
  }

  // Set type, if needed. We assume user if not set.
  if (fn->type == FunctionSymbol::Unknown) {
    fn->type = FunctionSymbol::User;
  }

  InstrData i;
  FunctionBlock* block = NULL;
  uint32_t furthest_target = fn->start_address;
  uint32_t addr = fn->start_address;
  bool starts_with_mfspr_lr = false;
  while (true) {
    i.code = XEGETUINT32BE(p + addr);
    i.type = ppc::GetInstrType(i.code);
    i.address = addr;

    // If we fetched 0 assume that we somehow hit one of the awesome
    // 'no really we meant to end after that bl' functions.
    if (!i.code) {
      XELOGSDB("function end %.8X (0x00000000 read)", addr);
      break;
    }

    // Check if the function starts with a mfspr lr, as that's a good indication
    // of whether or not this is a normal function with a prolog/epilog.
    // Some valid leaf functions won't have this, but most will.
    if (addr == fn->start_address &&
        i.type &&
        i.type->opcode == 0x7C0002A6 &&
        (((i.XFX.spr & 0x1F) << 5) | ((i.XFX.spr >> 5) & 0x1F)) == 8) {
      starts_with_mfspr_lr = true;
    }

    // Create a new basic block, if needed.
    if (!block) {
      block = new FunctionBlock();
      block->start_address = addr;
      block->end_address = addr;
      fn->blocks.insert(std::pair<uint32_t, FunctionBlock*>(
          block->start_address, block));
    }

    bool ends_block = false;
    bool ends_fn = false;
    if (!i.type) {
      // Invalid instruction.
      // We can just ignore it because there's (very little)/no chance it'll
      // affect flow control.
      XELOGSDB("Invalid instruction at %.8X: %.8X", addr, i.code);
    } else if (i.code == 0x4E800020) {
      // blr -- unconditional branch to LR.
      // This is generally a return.
      block->outgoing_type = FunctionBlock::kTargetLR;
      if (furthest_target > addr) {
        // Remaining targets within function, not end.
        XELOGSDB("ignoring blr %.8X (branch to %.8X)", addr, furthest_target);
      } else {
        // Function end point.
        XELOGSDB("function end %.8X", addr);
        ends_fn = true;
      }
      ends_block = true;
    } else if (i.code == 0x4E800420) {
      // bctr -- unconditional branch to CTR.
      // This is generally a jump to a function pointer (non-return).
      block->outgoing_type = FunctionBlock::kTargetCTR;
      if (furthest_target > addr) {
        // Remaining targets within function, not end.
        XELOGSDB("ignoring bctr %.8X (branch to %.8X)", addr,
                 furthest_target);
      } else {
        // Function end point.
        XELOGSDB("function end %.8X", addr);
        ends_fn = true;
      }
      ends_block = true;
    } else if (i.type->opcode == 0x48000000) {
      // b/ba/bl/bla
      uint32_t target = XEEXTS26(i.I.LI << 2) + (i.I.AA ? 0 : (int32_t)addr);
      block->outgoing_address = target;

      if (i.I.LK) {
        XELOGSDB("bl %.8X -> %.8X", addr, target);

        // Queue call target if needed.
        GetOrInsertFunction(target);
      } else {
        XELOGSDB("b %.8X -> %.8X", addr, target);

        // If the target is back into the function and there's no further target
        // we are at the end of a function.
        if (target >= fn->start_address &&
            target < addr && furthest_target <= addr) {
          XELOGSDB("function end %.8X (back b)", addr);
          ends_fn = true;
        }

        // If the target is not a branch and it goes to before the current
        // address it's definitely a tail call.
        if (!ends_fn &&
            target < addr) {
          XELOGSDB("function end %.8X (back b before addr)", addr);
          ends_fn = true;
        }

        // If the target is a __restgprlr_* method it's the end of a function.
        // Note that sometimes functions stick this in a basic block *inside*
        // of the function somewhere, so ensure we don't have any branches over
        // it.
        if (!ends_fn &&
            furthest_target <= addr && IsRestGprLr(target)) {
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
            fn->blocks.size() == 1) {
          XELOGSDB("HEURISTIC: ending at simple leaf thunk %.8X", addr);
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
      uint32_t target = XEEXTS16(i.B.BD << 2) + (i.B.AA ? 0 : (int32_t)addr);
      block->outgoing_address = target;
      if (i.B.LK) {
        XELOGSDB("bcl %.8X -> %.8X", addr, target);

        // Queue call target if needed.
        // TODO(benvanik): see if this is correct - not sure anyone makes
        //     function calls with bcl.
        //GetOrInsertFunction(target);
      } else {
        XELOGSDB("bc %.8X -> %.8X", addr, target);

        // TODO(benvanik): GetOrInsertFunction? it's likely a BB

        furthest_target = MAX(furthest_target, target);
      }
      ends_block = true;
    } else if (i.type->opcode == 0x4C000020) {
      // bclr/bclrl
      block->outgoing_type = FunctionBlock::kTargetLR;
      if (i.XL.LK) {
        XELOGSDB("bclrl %.8X", addr);
      } else {
        XELOGSDB("bclr %.8X", addr);
      }
      ends_block = true;
    } else if (i.type->opcode == 0x4C000420) {
      // bcctr/bcctrl
      block->outgoing_type = FunctionBlock::kTargetCTR;
      if (i.XL.LK) {
        XELOGSDB("bcctrl %.8X", addr);
      } else {
        XELOGSDB("bcctr %.8X", addr);
      }
      ends_block = true;
    }

    block->end_address = addr;
    if (ends_block) {
      // This instruction is the end of a basic block.
      // Finish up the one we are working on. The next loop around will create
      // a new one to scribble into.
      block = NULL;
    }

    if (ends_fn) {
      break;
    }

    addr += 4;
    if (fn->end_address && addr > fn->end_address) {
      // Hmm....
      XELOGSDB("Ran over function bounds! %.8X-%.8X",
               fn->start_address, fn->end_address);
      break;
    }
  }

  if (addr + 4 < fn->end_address) {
    // Ran under the expected value - since we probably got the initial bounds
    // from someplace valid (like method hints) this may indicate an error.
    // It's also possible that we guessed in hole-filling and there's another
    // function below this one.
    XELOGSDB("Function ran under: %.8X-%.8X ended at %.8X",
             fn->start_address, fn->end_address, addr + 4);
  }
  fn->end_address = addr;

  // If there's spare bits at the end, split the function.
  // TODO(benvanik): splitting?

  // TODO(benvanik): find and record stack information
  // - look for __savegprlr_* and __restgprlr_*
  // - if present, flag function as needing a stack
  // - record prolog/epilog lengths/stack size/etc

  // TODO(benvanik): queue for add without expensive locks?
  sym_table_->AddFunction(fn->start_address, fn);

  // For each basic block:
  // - find outgoing target block or function
  for (std::map<uint32_t, FunctionBlock*>::iterator it = fn->blocks.begin();
       it != fn->blocks.end(); ++it) {
    FunctionBlock* block = it->second;

    // If we have some address try to see what it is.
    if (block->outgoing_address) {
      if (block->outgoing_address >= fn->start_address &&
          block->outgoing_address <= fn->end_address) {
        // Branch into a block in this function.
        block->outgoing_type = FunctionBlock::kTargetBlock;
        block->outgoing_block = fn->GetBlock(block->outgoing_address);
        if (!block->outgoing_block) {
          // Block target not found - we may need to split.
          block->outgoing_block = fn->SplitBlock(block->outgoing_address);
        }
        if (!block->outgoing_block) {
          XELOGE("block target not found: %.8X", block->outgoing_address);
          XEASSERTALWAYS();
        }
      } else {
        // Function call.
        block->outgoing_type = FunctionBlock::kTargetFunction;
        block->outgoing_function = GetFunction(block->outgoing_address);
        XEASSERTNOTNULL(block->outgoing_function);
        FunctionSymbol::AddCall(fn, block->outgoing_function);
      }
    }
  }

  XELOGSDB("Finished analyzing %.8X", fn->start_address);
  return 0;
}

int SymbolDatabase::CompleteFunctionGraph(FunctionSymbol* fn) {
  // Find variable accesses.
  // TODO(benvanik): data analysis to find variable accesses.

  return 0;
}

namespace {
typedef struct {
  uint32_t start_address;
  uint32_t end_address;
} HoleInfo;
}

bool SymbolDatabase::FillHoles() {
  // If 4b, check if 0x00000000 and ignore (alignment padding)
  // If 8b, check if first value is within .text and ignore (EH entry)
  // Else, add to scan queue as function?

  std::vector<HoleInfo> holes;
  std::vector<uint32_t> ees;

  uint32_t previous = 0;
  for (SymbolMap::iterator it = symbols_.begin(); it != symbols_.end(); ++it) {
    switch (it->second->symbol_type) {
      case Symbol::Function:
        {
          FunctionSymbol* fn = static_cast<FunctionSymbol*>(it->second);
          if (previous && (int)(fn->start_address - previous) > 0) {
            // Hole!
            uint32_t* p = (uint32_t*)xe_memory_addr(memory_, previous);
            size_t hole_length = fn->start_address - previous;
            if (hole_length == 4) {
              // Likely a pointer or 0.
              if (*p == 0) {
                // Skip - just a zero.
              } else if (IsValueInTextRange(XEGETUINT32BE(p))) {
                // An address - probably an indirection data value.
              }
            } else if (hole_length == 8) {
              // Possibly an exception handler entry.
              // They look like [some value in .text] + [some pointer].
              if (*p == 0 || IsValueInTextRange(XEGETUINT32BE(p))) {
                // Skip!
                ees.push_back(previous);
              } else {
                // Probably legit.
                HoleInfo hole_info = {previous, fn->start_address};
                holes.push_back(hole_info);
              }
            } else {
              // Probably legit.
              HoleInfo hole_info = {previous, fn->start_address};
              holes.push_back(hole_info);
            }
          }
          previous = fn->end_address + 4;
        }
        break;
      case Symbol::Variable:
      case Symbol::ExceptionEntry:
        break;
    }
  }

  for (std::vector<uint32_t>::iterator it = ees.begin(); it != ees.end();
       ++it) {
    ExceptionEntrySymbol* ee = GetOrInsertExceptionEntry(*it);
    ee->function = GetFunction(ee->address + 8);
    if (ee->function) {
      ee->function->ee = ee;
    }
    uint32_t* p = (uint32_t*)xe_memory_addr(memory_, ee->address);
    uint32_t handler_addr = XEGETUINT32BE(p);
    if (handler_addr) {
      GetOrInsertFunction(handler_addr);
    }
    uint32_t data_addr = XEGETUINT32BE(p + 1);
    if (data_addr) {
      VariableSymbol* var = GetOrInsertVariable(data_addr);
      char name[128];
      if (ee->function) {
        xesnprintfa(name, XECOUNT(name), "__ee_data_%s", ee->function->name());
      } else {
        xesnprintfa(name, XECOUNT(name), "__ee_data_%.8X", *it);
      }
      var->set_name(name);
    }
  }

  bool any_functions_added = false;
  for (std::vector<HoleInfo>::iterator it = holes.begin(); it != holes.end();
       ++it) {
    FunctionSymbol* fn = GetOrInsertFunction(it->start_address);
    if (!fn->end_address) {
      fn->end_address = it->end_address;
      any_functions_added = true;
    }
  }

  return any_functions_added;
}

int SymbolDatabase::FlushQueue() {
  while (scan_queue_.size()) {
    FunctionSymbol* fn = scan_queue_.front();
    scan_queue_.pop_front();
    if (AnalyzeFunction(fn)) {
      XELOGSDB("Aborting analysis!");
      return 1;
    }
  }
  return 0;
}

bool SymbolDatabase::IsRestGprLr(uint32_t addr) {
  FunctionSymbol* fn = GetFunction(addr);
  return fn && (fn->flags & FunctionSymbol::kFlagRestGprLr);
}

void SymbolDatabase::ReadMap(const char* file_name) {
  std::ifstream infile(file_name);

  // Skip until '  Address'. Skip the next blank line.
  std::string line;
  while (std::getline(infile, line)) {
    if (line.find("  Address") == 0) {
      // Skip the next line.
      std::getline(infile, line);
      break;
    }
  }

  std::stringstream sstream;
  std::string ignore;
  std::string name;
  std::string addr_str;
  std::string type_str;
  while (std::getline(infile, line)) {
    // Remove newline.
    while (line.size() &&
           (line[line.size() - 1] == '\r' ||
            line[line.size() - 1] == '\n')) {
      line.erase(line.end() - 1);
    }

    // End when we hit the first whitespace.
    if (line.size() == 0) {
      break;
    }

    // Line is [ws][ignore][ws][name][ws][hex addr][ws][(f)][ws][library]
    sstream.clear();
    sstream.str(line);
    sstream >> std::ws;
    sstream >> ignore;
    sstream >> std::ws;
    sstream >> name;
    sstream >> std::ws;
    sstream >> addr_str;
    sstream >> std::ws;
    sstream >> type_str;

    uint32_t addr = (uint32_t)strtoul(addr_str.c_str(), NULL, 16);
    if (!addr) {
      continue;
    }

    Symbol* symbol = GetSymbol(addr);
    if (symbol) {
      // Symbol found - set name.
      // We could check the type, but it's not needed.
      symbol->set_name(name.c_str());
    } else {
      if (type_str == "f") {
        // Function was not found via analysis.
        // We don't want to add it here as that would make us require maps to
        // get working.
        XELOGSDB("MAP DIFF: function %.8X %s not found during analysis",
                 addr, name.c_str());
      } else {
        // Add a new variable.
        // This is just helpful, but changes no behavior.
        VariableSymbol* var = GetOrInsertVariable(addr);
        var->set_name(name.c_str());
      }
    }
  }
}

void SymbolDatabase::WriteMap(const char* file_name) {
  FILE* file = fopen(file_name, "wt");
  Dump(file);
  fclose(file);
}

void SymbolDatabase::Dump(FILE* file) {
  uint32_t previous = 0;
  for (SymbolMap::iterator it = symbols_.begin(); it != symbols_.end(); ++it) {
    switch (it->second->symbol_type) {
      case Symbol::Function:
      {
        FunctionSymbol* fn = static_cast<FunctionSymbol*>(it->second);
        if (previous && (int)(fn->start_address - previous) > 0) {
          if (fn->start_address - previous > 4 ||
              *((uint32_t*)xe_memory_addr(memory_, previous)) != 0) {
            fprintf(file, "%.8X-%.8X (%5d) h\n", previous, fn->start_address,
                    fn->start_address - previous);
          }
        }
        fprintf(file, "%.8X-%.8X (%5d) f %s\n",
                fn->start_address,
                fn->end_address + 4,
                fn->end_address - fn->start_address + 4,
                fn->name() ? fn->name() : "<unknown>");
        previous = fn->end_address + 4;
        DumpFunctionBlocks(file, fn);
      }
        break;
      case Symbol::Variable:
      {
        VariableSymbol* var = static_cast<VariableSymbol*>(it->second);
        fprintf(file, "%.8X v %s\n", var->address,
               var->name() ? var->name() : "<unknown>");
      }
        break;
      case Symbol::ExceptionEntry:
      {
        ExceptionEntrySymbol* ee = static_cast<ExceptionEntrySymbol*>(
            it->second);
        fprintf(file, "%.8X-%.8X (%5d) e of %.8X\n",
               ee->address, ee->address + 8, 8,
               ee->function ? ee->function->start_address : 0);
        previous = ee->address + 8 + 4;
      }
        break;
    }
  }
}

void SymbolDatabase::DumpFunctionBlocks(FILE* file, FunctionSymbol* fn) {
  for (std::map<uint32_t, FunctionBlock*>::iterator it = fn->blocks.begin();
       it != fn->blocks.end(); ++it) {
    FunctionBlock* block = it->second;
    fprintf(file, "  bb %.8X-%.8X",
            block->start_address, block->end_address + 4);
    switch (block->outgoing_type) {
      case FunctionBlock::kTargetUnknown:
        fprintf(file, " ?\n");
        break;
      case FunctionBlock::kTargetBlock:
        fprintf(file, " branch %.8X\n", block->outgoing_block->start_address);
        break;
      case FunctionBlock::kTargetFunction:
        fprintf(file, " call %.8X %s\n",
               block->outgoing_function->start_address,
               block->outgoing_function->name());
        break;
      case FunctionBlock::kTargetLR:
        fprintf(file, " branch lr\n");
        break;
      case FunctionBlock::kTargetCTR:
        fprintf(file, " branch ctr\n");
        break;
      case FunctionBlock::kTargetNone:
        fprintf(file, "\n");
        break;
    }
  }
}
