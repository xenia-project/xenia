/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <alloy/frontend/ppc/ppc_translator.h>

#include <alloy/alloy-private.h>
#include <alloy/compiler/compiler_passes.h>
#include <alloy/frontend/tracing.h>
#include <alloy/frontend/ppc/ppc_frontend.h>
#include <alloy/frontend/ppc/ppc_hir_builder.h>
#include <alloy/frontend/ppc/ppc_instr.h>
#include <alloy/frontend/ppc/ppc_scanner.h>
#include <alloy/runtime/runtime.h>

using namespace alloy;
using namespace alloy::backend;
using namespace alloy::compiler;
using namespace alloy::frontend;
using namespace alloy::frontend::ppc;
using namespace alloy::hir;
using namespace alloy::runtime;


PPCTranslator::PPCTranslator(PPCFrontend* frontend) :
    frontend_(frontend) {
  scanner_ = new PPCScanner(frontend);
  builder_ = new PPCHIRBuilder(frontend);

  compiler_ = new Compiler(frontend->runtime());

  compiler_->AddPass(new passes::ContextPromotionPass());
  compiler_->AddPass(new passes::SimplificationPass());
  // TODO(benvanik): run repeatedly?
  compiler_->AddPass(new passes::ConstantPropagationPass());
  //compiler_->AddPass(new passes::TypePropagationPass());
  //compiler_->AddPass(new passes::ByteSwapEliminationPass());
  compiler_->AddPass(new passes::SimplificationPass());
  //compiler_->AddPass(new passes::DeadStoreEliminationPass());
  compiler_->AddPass(new passes::DeadCodeEliminationPass());

  Backend* backend = frontend->runtime()->backend();
  assembler_ = backend->CreateAssembler();
}

PPCTranslator::~PPCTranslator() {
  delete assembler_;
  delete compiler_;
  delete builder_;
  delete scanner_;
}

int PPCTranslator::Translate(
    FunctionInfo* symbol_info,
    bool with_debug_info,
    Function** out_function) {
  // Scan the function to find its extents. We only need to do this if we
  // haven't already been provided with them from some other source.
  if (!symbol_info->has_end_address()) {
    // TODO(benvanik): find a way to remove the need for the scan. A fixup
    //     scheme acting on branches could go back and modify calls to branches
    //     if they are within the extents.
    int result = scanner_->FindExtents(symbol_info);
    if (result) {
      return result;
    }
  }

  // NOTE: we only want to do this when required, as it's expensive to build.
  DebugInfo* debug_info = NULL;
  if (FLAGS_debug || with_debug_info) {
    debug_info = new DebugInfo();
  }

  // Stash source.
  if (debug_info) {
    DumpSource(symbol_info, &string_buffer_);
    debug_info->set_source_disasm(string_buffer_.ToString());
    string_buffer_.Reset();

    DumpSourceJson(symbol_info, &string_buffer_);
    debug_info->set_source_json(string_buffer_.ToString());
    string_buffer_.Reset();
  }

  // Emit function.
  int result = builder_->Emit(symbol_info);
  XEEXPECTZERO(result);

  // Stash raw HIR.
  if (debug_info) {
    builder_->Dump(&string_buffer_);
    debug_info->set_raw_hir_disasm(string_buffer_.ToString());
    string_buffer_.Reset();
  }

  // Compile/optimize/etc.
  result = compiler_->Compile(builder_);
  XEEXPECTZERO(result);

  // Stash optimized HIR.
  if (debug_info) {
    builder_->Dump(&string_buffer_);
    debug_info->set_hir_disasm(string_buffer_.ToString());
    string_buffer_.Reset();
  }

  // Assemble to backend machine code.
  result = assembler_->Assemble(symbol_info, builder_, debug_info, out_function);
  XEEXPECTZERO(result);

  result = 0;

XECLEANUP:
  if (result) {
    delete debug_info;
  }
  builder_->Reset();
  compiler_->Reset();
  assembler_->Reset();
  string_buffer_.Reset();
  return result;
};

void PPCTranslator::DumpSource(
    runtime::FunctionInfo* symbol_info, StringBuffer* string_buffer) {
  Memory* memory = frontend_->memory();
  const uint8_t* p = memory->membase();

  string_buffer->Append("%s fn %.8X-%.8X %s\n",
      symbol_info->module()->name(),
      symbol_info->address(), symbol_info->end_address(),
      symbol_info->name());

  auto blocks = scanner_->FindBlocks(symbol_info);

  uint64_t start_address = symbol_info->address();
  uint64_t end_address = symbol_info->end_address();
  InstrData i;
  auto block_it = blocks.begin();
  for (uint64_t address = start_address, offset = 0; address <= end_address;
       address += 4, offset++) {
    i.address = address;
    i.code = XEGETUINT32BE(p + address);
    // TODO(benvanik): find a way to avoid using the opcode tables.
    i.type = GetInstrType(i.code);

    // Check labels.
    if (block_it != blocks.end() &&
        block_it->start_address == address) {
      string_buffer->Append(
          "%.8X          loc_%.8X:\n", address, address);
      ++block_it;
    }

    if (!i.type) {
      string_buffer->Append("%.8X %.8X ???", address, i.code);
    } else if (i.type->disassemble) {
      ppc::InstrDisasm d;
      i.type->disassemble(i, d);
      std::string disasm;
      d.Dump(disasm);
      string_buffer->Append("%.8X %.8X %s", address, i.code, disasm.c_str());
    } else {
      string_buffer->Append("%.8X %.8X %s ???", address, i.code, i.type->name);
    }
    string_buffer->Append("\n");
  }
}

void PPCTranslator::DumpSourceJson(
    runtime::FunctionInfo* symbol_info, StringBuffer* string_buffer) {
  Memory* memory = frontend_->memory();
  const uint8_t* p = memory->membase();

  string_buffer->Append("{\n");

  auto blocks = scanner_->FindBlocks(symbol_info);
  string_buffer->Append("\"blocks\": [\n");
  for (auto it = blocks.begin(); it != blocks.end(); ++it) {
    string_buffer->Append("{ \"start\": %u, \"end\": %u }%c",
                          it->start_address, it->end_address,
                          (it + 1 != blocks.end()) ? ',' : ' ');
  }
  string_buffer->Append("],\n");

  string_buffer->Append("\"lines\": [\n");
  uint64_t start_address = symbol_info->address();
  uint64_t end_address = symbol_info->end_address();
  InstrData i;
  auto block_it = blocks.begin();
  for (uint64_t address = start_address, offset = 0; address <= end_address;
       address += 4, offset++) {
    i.address = address;
    i.code = XEGETUINT32BE(p + address);
    // TODO(benvanik): find a way to avoid using the opcode tables.
    i.type = GetInstrType(i.code);

    // Check labels.
    if (block_it != blocks.end() &&
        block_it->start_address == address) {
      if (address != start_address) {
        // Whitespace to pad blocks.
        string_buffer->Append(
            "[\"c\", %u, 0, \"\", \"\"],\n",
            address);
      }
      string_buffer->Append(
          "[\"l\", %u, 0, \"loc_%.8X:\", \"\"],\n",
          address, address);
      ++block_it;
    }

    const char* disasm_str = "";
    const char* comment_str = "";
    std::string disasm;
    if (!i.type) {
      disasm_str = "?";
    } else if (i.type->disassemble) {
      ppc::InstrDisasm d;
      i.type->disassemble(i, d);
      d.Dump(disasm);
      disasm_str = disasm.c_str();
    } else {
      disasm_str = i.type->name;
    }
    string_buffer->Append("[\"i\", %u, %u, \"    %s\", \"%s\"]%c\n",
                          address, i.code, disasm_str, comment_str,
                          (address + 4 <= end_address) ? ',' : ' ');
  }
  string_buffer->Append("]\n");

  string_buffer->Append("}\n");
}
