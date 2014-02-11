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
#include <alloy/frontend/ppc/ppc_disasm.h>
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
  Backend* backend = frontend->runtime()->backend();

  scanner_ = new PPCScanner(frontend);
  builder_ = new PPCHIRBuilder(frontend);
  compiler_ = new Compiler(frontend->runtime());
  assembler_ = backend->CreateAssembler();
  assembler_->Initialize();

  bool validate = FLAGS_validate_hir;

  // Build the CFG first.
  compiler_->AddPass(new passes::ControlFlowAnalysisPass());

  // Passes are executed in the order they are added. Multiple of the same
  // pass type may be used.
  if (validate) compiler_->AddPass(new passes::ValidationPass());
  compiler_->AddPass(new passes::ContextPromotionPass());
  if (validate) compiler_->AddPass(new passes::ValidationPass());
  compiler_->AddPass(new passes::SimplificationPass());
  if (validate) compiler_->AddPass(new passes::ValidationPass());
  compiler_->AddPass(new passes::ConstantPropagationPass());
  if (validate) compiler_->AddPass(new passes::ValidationPass());
  compiler_->AddPass(new passes::SimplificationPass());
  if (validate) compiler_->AddPass(new passes::ValidationPass());
  //compiler_->AddPass(new passes::DeadStoreEliminationPass());
  //if (validate) compiler_->AddPass(new passes::ValidationPass());
  compiler_->AddPass(new passes::DeadCodeEliminationPass());
  if (validate) compiler_->AddPass(new passes::ValidationPass());

  //// Removes all unneeded variables. Try not to add new ones after this.
  //compiler_->AddPass(new passes::ValueReductionPass());
  //if (validate) compiler_->AddPass(new passes::ValidationPass());

  // Register allocation for the target backend.
  // Will modify the HIR to add loads/stores.
  // This should be the last pass before finalization, as after this all
  // registers are assigned and ready to be emitted.
  compiler_->AddPass(new passes::RegisterAllocationPass(
      backend->machine_info()));
  if (validate) compiler_->AddPass(new passes::ValidationPass());

  // Must come last. The HIR is not really HIR after this.
  compiler_->AddPass(new passes::FinalizationPass());
}

PPCTranslator::~PPCTranslator() {
  delete assembler_;
  delete compiler_;
  delete builder_;
  delete scanner_;
}

int PPCTranslator::Translate(
    FunctionInfo* symbol_info,
    uint32_t debug_info_flags,
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
  if (FLAGS_always_disasm) {
    debug_info_flags |= DEBUG_INFO_ALL_DISASM;
  }
  DebugInfo* debug_info = NULL;
  if (debug_info_flags) {
    debug_info = new DebugInfo();
  }

  // Stash source.
  if (debug_info_flags & DEBUG_INFO_SOURCE_DISASM) {
    DumpSource(symbol_info, &string_buffer_);
    debug_info->set_source_disasm(string_buffer_.ToString());
    string_buffer_.Reset();
  }

  // Emit function.
  int result = builder_->Emit(symbol_info, debug_info != NULL);
  XEEXPECTZERO(result);

  // Stash raw HIR.
  if (debug_info_flags & DEBUG_INFO_RAW_HIR_DISASM) {
    builder_->Dump(&string_buffer_);
    debug_info->set_raw_hir_disasm(string_buffer_.ToString());
    string_buffer_.Reset();
  }

  // Compile/optimize/etc.
  result = compiler_->Compile(builder_);
  XEEXPECTZERO(result);

  // Stash optimized HIR.
  if (debug_info_flags & DEBUG_INFO_HIR_DISASM) {
    builder_->Dump(&string_buffer_);
    debug_info->set_hir_disasm(string_buffer_.ToString());
    string_buffer_.Reset();
  }

  // Assemble to backend machine code.
  result = assembler_->Assemble(
      symbol_info, builder_,
      debug_info_flags, debug_info,
      out_function);
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

    string_buffer->Append("%.8X %.8X   ", address, i.code);
    DisasmPPC(i, string_buffer);
    string_buffer->Append("\n");
  }
}
