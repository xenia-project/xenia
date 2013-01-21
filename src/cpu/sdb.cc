/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/cpu/sdb.h>

#include <list>
#include <map>

#include <xenia/cpu/ppc/instr.h>


using namespace xe;
using namespace xe::cpu;
using namespace xe::cpu::ppc;
using namespace xe::cpu::sdb;
using namespace xe::kernel;


SymbolDatabase::SymbolDatabase(xe_memory_ref memory, UserModule* module) {
  memory_ = xe_memory_retain(memory);
  module_ = module;
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

  const xe_xex2_header_t *header = module_->xex_header();

  // Find __savegprlr_* and __restgprlr_*.
  FindGplr();

  // Add each import thunk.
  for (size_t n = 0; n < header->import_library_count; n++) {
    AddImports(&header->import_libraries[n]);
  }

  // Add each export root.
  // TODO(benvanik): exports.
  //   - insert fn or variable
  //   - queue fn

  // Add method hints, if available.
  // Not all XEXs have these.
  AddMethodHints();

  // Queue entry point of the application.
  FunctionSymbol* fn = GetOrInsertFunction(header->exe_entry_point);
  fn->name = xestrdupa("<entry>");

  // Keep pumping the queue until there's nothing left to do.
  FlushQueue();

  // Do a pass over the functions to fill holes. A few times. Just to be safe.
  while (true) {
    if (!FillHoles()) {
      break;
    }
    FlushQueue();
  }

  return 0;
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

FunctionSymbol* SymbolDatabase::GetOrInsertFunction(uint32_t address) {
  FunctionSymbol* fn = GetFunction(address);
  if (fn) {
    return fn;
  }

  // Ignore values outside of the .text range.
  if (!IsValueInTextRange(address)) {
    XELOGSDB("Ignoring function outside of .text: %.8X\n", address);
    return NULL;
  }

  fn = new FunctionSymbol();
  fn->start_address = address;
  function_count_++;
  symbols_.insert(SymbolMap::value_type(address, fn));
  scan_queue_.push_back(fn);
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

FunctionSymbol* SymbolDatabase::GetFunction(uint32_t address) {
  SymbolMap::iterator i = symbols_.find(address);
  if (i != symbols_.end() && i->second->symbol_type == Symbol::Function) {
    return static_cast<FunctionSymbol*>(i->second);
  }
  return NULL;
}

VariableSymbol* SymbolDatabase::GetVariable(uint32_t address) {
  SymbolMap::iterator i = symbols_.find(address);
  if (i != symbols_.end() && i->second->symbol_type == Symbol::Variable) {
    return static_cast<VariableSymbol*>(i->second);
  }
  return NULL;
}

int SymbolDatabase::GetAllFunctions(vector<FunctionSymbol*>& functions) {
  for (SymbolMap::iterator it = symbols_.begin(); it != symbols_.end(); ++it) {
    if (it->second->symbol_type == Symbol::Function) {
      functions.push_back(static_cast<FunctionSymbol*>(it->second));
    }
  }
  return 0;
}

void SymbolDatabase::Dump() {
  uint32_t previous = 0;
  for (SymbolMap::iterator it = symbols_.begin(); it != symbols_.end(); ++it) {
    switch (it->second->symbol_type) {
      case Symbol::Function:
        {
          FunctionSymbol* fn = static_cast<FunctionSymbol*>(it->second);
          if (previous && (int)(fn->start_address - previous) > 0) {
            if (fn->start_address - previous > 4 ||
                *((uint32_t*)xe_memory_addr(memory_, previous)) != 0) {
              printf("%.8X-%.8X (%5d) h\n", previous, fn->start_address,
                     fn->start_address - previous);
            }
          }
          printf("%.8X-%.8X (%5d) f %s\n", fn->start_address,
                 fn->end_address + 4,
                 fn->end_address - fn->start_address + 4,
                 fn->name ? fn->name : "<unknown>");
          previous = fn->end_address + 4;
          DumpFunctionBlocks(fn);
        }
        break;
      case Symbol::Variable:
        {
          VariableSymbol* var = static_cast<VariableSymbol*>(it->second);
          printf("%.8X v %s\n", var->address,
                 var->name ? var->name : "<unknown>");
        }
        break;
      case Symbol::ExceptionEntry:
        {
          ExceptionEntrySymbol* ee = static_cast<ExceptionEntrySymbol*>(
              it->second);
          printf("%.8X-%.8X (%5d) e of %.8X\n",
                 ee->address, ee->address + 8, 8,
                 ee->function ? ee->function->start_address : 0);
          previous = ee->address + 8 + 4;
        }
        break;
    }
  }
}

void SymbolDatabase::DumpFunctionBlocks(FunctionSymbol* fn) {
  for (std::map<uint32_t, FunctionBlock*>::iterator it = fn->blocks.begin();
       it != fn->blocks.end(); ++it) {
    FunctionBlock* bb = it->second;
    printf("  bb %.8X\n", bb->start_address);
  }
}

int SymbolDatabase::FindGplr() {
  // Special stack save/restore functions.
  // __savegprlr_14 to __savegprlr_31
  // __restgprlr_14 to __restgprlr_31
  // http://research.microsoft.com/en-us/um/redmond/projects/invisible/src/crt/md/ppc/xxx.s.htm
  // It'd be nice to stash these away and mark them as such to allow for
  // special codegen.
  static const uint32_t code_values[] = {
    0x68FFC1F9, // __savegprlr_14
    0x70FFE1F9, // __savegprlr_15
    0x78FF01FA, // __savegprlr_16
    0x80FF21FA, // __savegprlr_17
    0x88FF41FA, // __savegprlr_18
    0x90FF61FA, // __savegprlr_19
    0x98FF81FA, // __savegprlr_20
    0xA0FFA1FA, // __savegprlr_21
    0xA8FFC1FA, // __savegprlr_22
    0xB0FFE1FA, // __savegprlr_23
    0xB8FF01FB, // __savegprlr_24
    0xC0FF21FB, // __savegprlr_25
    0xC8FF41FB, // __savegprlr_26
    0xD0FF61FB, // __savegprlr_27
    0xD8FF81FB, // __savegprlr_28
    0xE0FFA1FB, // __savegprlr_29
    0xE8FFC1FB, // __savegprlr_30
    0xF0FFE1FB, // __savegprlr_31
    0xF8FF8191,
    0x2000804E,
    0x68FFC1E9, // __restgprlr_14
    0x70FFE1E9, // __restgprlr_15
    0x78FF01EA, // __restgprlr_16
    0x80FF21EA, // __restgprlr_17
    0x88FF41EA, // __restgprlr_18
    0x90FF61EA, // __restgprlr_19
    0x98FF81EA, // __restgprlr_20
    0xA0FFA1EA, // __restgprlr_21
    0xA8FFC1EA, // __restgprlr_22
    0xB0FFE1EA, // __restgprlr_23
    0xB8FF01EB, // __restgprlr_24
    0xC0FF21EB, // __restgprlr_25
    0xC8FF41EB, // __restgprlr_26
    0xD0FF61EB, // __restgprlr_27
    0xD8FF81EB, // __restgprlr_28
    0xE0FFA1EB, // __restgprlr_29
    0xE8FFC1EB, // __restgprlr_30
    0xF0FFE1EB, // __restgprlr_31
    0xF8FF8181,
    0xA603887D,
    0x2000804E,
  };

  uint32_t gplr_start = 0;
  const xe_xex2_header_t* header = module_->xex_header();
  for (size_t n = 0, i = 0; n < header->section_count; n++) {
    const xe_xex2_section_t* section = &header->sections[n];
    const size_t start_address =
        header->exe_address + (i * xe_xex2_section_length);
    const size_t end_address =
        start_address + (section->info.page_count * xe_xex2_section_length);
    if (section->info.type == XEX_SECTION_CODE) {
      gplr_start = xe_memory_search_aligned(
          memory_, start_address, end_address,
          code_values, XECOUNT(code_values));
      if (gplr_start) {
        break;
      }
    }
    i += section->info.page_count;
  }
  if (!gplr_start) {
    return 0;
  }

  // Add function stubs.
  char name[32];
  uint32_t address = gplr_start;
  for (int n = 14; n <= 31; n++) {
    xesnprintf(name, XECOUNT(name), "__savegprlr_%d", n);
    FunctionSymbol* fn = GetOrInsertFunction(address);
    fn->end_address = fn->start_address + (31 - n) * 4 + 2 * 4;
    fn->name = xestrdupa(name);
    fn->type = FunctionSymbol::User;
    fn->flags |= FunctionSymbol::kFlagSaveGprLr;
    address += 4;
  }
  address = gplr_start + 20 * 4;
  for (int n = 14; n <= 31; n++) {
    xesnprintf(name, XECOUNT(name), "__restgprlr_%d", n);
    FunctionSymbol* fn = GetOrInsertFunction(address);
    fn->end_address = fn->start_address + (31 - n) * 4 + 3 * 4;
    fn->name = xestrdupa(name);
    fn->type = FunctionSymbol::User;
    fn->flags |= FunctionSymbol::kFlagRestGprLr;
    address += 4;
  }

  return 0;
}

int SymbolDatabase::AddImports(const xe_xex2_import_library_t* library) {
  xe_xex2_ref xex = module_->xex();
  xe_xex2_import_info_t* import_infos;
  size_t import_info_count;
  if (xe_xex2_get_import_infos(xex, library, &import_infos,
                               &import_info_count)) {
    xe_xex2_release(xex);
    return 1;
  }

  char name[64];
  for (size_t n = 0; n < import_info_count; n++) {
    const xe_xex2_import_info_t* info = &import_infos[n];
    VariableSymbol* var = GetOrInsertVariable(info->value_address);
    // TODO(benvanik): use kernel name
    xesnprintf(name, XECOUNT(name), "__var_%s_%.3X", library->name,
               info->ordinal);
    var->name = xestrdupa(name);
    if (info->thunk_address) {
      FunctionSymbol* fn = GetOrInsertFunction(info->thunk_address);
      // TODO(benvanik): use kernel name
      xesnprintf(name, XECOUNT(name), "__thunk_%s_%.3X", library->name,
                 info->ordinal);
      fn->end_address = fn->start_address + 16 - 4;
      fn->name = xestrdupa(name);
      fn->type = FunctionSymbol::Kernel;
    }
  }

  xe_xex2_release(xex);
  return 0;
}

int SymbolDatabase::AddMethodHints() {
  PEMethodInfo* method_infos;
  size_t method_info_count;
  if (module_->GetMethodHints(&method_infos, &method_info_count)) {
    return 1;
  }

  for (size_t n = 0; n < method_info_count; n++) {
    PEMethodInfo* method_info = &method_infos[n];
    FunctionSymbol* fn = GetOrInsertFunction(method_info->address);
    fn->end_address = method_info->address + method_info->total_length - 4;
    fn->type = FunctionSymbol::User;
    // TODO(benvanik): something with prolog_length?
  }

  return 0;
}

bool SymbolDatabase::IsRestGprLr(uint32_t addr) {
  FunctionSymbol* fn = GetFunction(addr);
  return fn && (fn->flags & FunctionSymbol::kFlagRestGprLr);
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

  if (*((uint32_t*)(p + fn->start_address)) == 0) {
    // Function starts with 0x00000000 - we want to skip this and split.
    XELOGSDB("function starts with 0: %.8X\n", fn->start_address);
    symbols_.erase(fn->start_address);
    if (!GetFunction(fn->start_address + 4)) {
      fn->start_address += 4;
      symbols_.insert(SymbolMap::value_type(fn->start_address, fn));
      scan_queue_.push_back(fn);
    } else {
      delete fn;
    }
    return 0;
  }

  XELOGSDB("Analyzing function %.8X...\n", fn->start_address);

  InstrData i;
  FunctionBlock* block = NULL;
  uint32_t furthest_target = fn->start_address;
  uint32_t addr = fn->start_address;
  while (true) {
    i.code = XEGETUINT32BE(p + addr);
    i.type = ppc::GetInstrType(i.code);
    i.address = addr;

    // If we fetched 0 assume that we somehow hit one of the awesome
    // 'no really we meant to end after that bl' functions.
    if (!i.code) {
      XELOGSDB("function end %.8X (0x00000000 read)\n", addr);
      break;
    }

    if (!i.type) {
      // Invalid instruction.
      XELOGSDB("Invalid instruction at %.8X: %.8X\n", addr, i.code);
      return 1;
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
    if (i.code == 0x4E800020) {
      // blr -- unconditional branch to LR.
      // This is generally a return.
      if (furthest_target > addr) {
        // Remaining targets within function, not end.
        XELOGSDB("ignoring blr %.8X (branch to %.8X)\n", addr, furthest_target);
      } else {
        // Function end point.
        XELOGSDB("function end %.8X\n", addr);
        break;
      }
      ends_block = true;
    } else if (i.type->opcode == 0x48000000) {
      // b/ba/bl/bla
      uint32_t target = XEEXTS26(i.I.LI << 2) + (i.I.AA ? 0 : (int32_t)addr);

      if (i.I.LK) {
        XELOGSDB("bl %.8X -> %.8X\n", addr, target);

        // Queue target if needed.
      } else {
        XELOGSDB("b %.8X -> %.8X\n", addr, target);
        // If the target is back into the function and there's no further target
        // we are at the end of a function.
        if (target >= fn->start_address &&
            target < addr && furthest_target <= addr) {
          XELOGSDB("function end %.8X (back b)\n", addr);
          break;
        }

        // If the target is a __restgprlr_* method it's the end of a function.
        // Note that sometimes functions stick this in a basic block *inside*
        // of the function somewhere, so ensure we don't have any branches over
        // it.
        if (furthest_target <= addr && IsRestGprLr(target)) {
          XELOGSDB("function end %.8X (__restgprlr_*)\n", addr);
          break;
        }

        furthest_target = MAX(furthest_target, target);
      }
      ends_block = true;
    } else if (i.type->opcode == 0x40000000) {
      // bc/bca/bcl/bcla
      uint32_t target = XEEXTS16(i.B.BD << 2) + (i.B.AA ? 0 : (int32_t)addr);
      if (i.B.LK) {
        XELOGSDB("bcl %.8X -> %.8X\n", addr, target);
      } else {
        XELOGSDB("bc %.8X -> %.8X\n", addr, target);

        furthest_target = MAX(furthest_target, target);
      }
      ends_block = true;
    } else if (i.type->opcode == 0x4C000020) {
      // bclr/bclrl
      if (i.XL.LK) {
        XELOGSDB("bclrl %.8X\n", addr);
      } else {
        XELOGSDB("bclr %.8X\n", addr);
      }
      ends_block = true;
    } else if (i.type->opcode == 0x4C000420) {
      // bcctr/bcctrl
      if (i.XL.LK) {
        XELOGSDB("bcctrl %.8X\n", addr);
      } else {
        XELOGSDB("bcctr %.8X\n", addr);
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

    addr += 4;
    if (fn->end_address && addr > fn->end_address) {
      // Hmm....
      XELOGSDB("Ran over function bounds! %.8X-%.8X\n",
             fn->start_address, fn->end_address);
      break;
    }
  }

  if (addr + 4 < fn->end_address) {
    // Ran under the expected value - since we probably got the initial bounds
    // from someplace valid (like method hints) this may indicate an error.
    // It's also possible that we guessed in hole-filling and there's another
    // function below this one.
    XELOGSDB("Function ran under: %.8X-%.8X ended at %.8X\n",
           fn->start_address, fn->end_address, addr + 4);
  }
  fn->end_address = addr;

  // If there's spare bits at the end, split the function.

  XELOGSDB("Finished analyzing %.8X\n", fn->start_address);
  return 0;
}

int SymbolDatabase::FlushQueue() {
  while (scan_queue_.size()) {
    FunctionSymbol* fn = scan_queue_.front();
    scan_queue_.pop_front();
    if (AnalyzeFunction(fn)) {
      XELOGSDB("Aborting analysis!\n");
      return 1;
    }
  }
  return 0;
}

bool SymbolDatabase::IsValueInTextRange(uint32_t value) {
  const xe_xex2_header_t* header = module_->xex_header();
  for (size_t n = 0, i = 0; n < header->section_count; n++) {
    const xe_xex2_section_t* section = &header->sections[n];
    const size_t start_address =
        header->exe_address + (i * xe_xex2_section_length);
    const size_t end_address =
        start_address + (section->info.page_count * xe_xex2_section_length);
    if (value >= start_address && value < end_address) {
      return section->info.type == XEX_SECTION_CODE;
    }
    i += section->info.page_count;
  }
  return false;
}

typedef struct {
  uint32_t start_address;
  uint32_t end_address;
} HoleInfo;

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
                holes.push_back((HoleInfo){previous, fn->start_address});
              }
            } else {
              // Probably legit.
              holes.push_back((HoleInfo){previous, fn->start_address});
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
      char name[32];
      xesnprintf(name, XECOUNT(name), "__ee_data_%.8X", *it);
      var->name = xestrdupa(name);
    }
  }

  for (std::vector<HoleInfo>::iterator it = holes.begin(); it != holes.end();
       ++it) {
    FunctionSymbol* fn = GetOrInsertFunction(it->start_address);
    fn->end_address = it->end_address;
  }

  return holes.size() > 0;
}
