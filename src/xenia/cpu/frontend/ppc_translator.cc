/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/frontend/ppc_translator.h"

#include <gflags/gflags.h>

#include "xenia/base/assert.h"
#include "xenia/base/byte_order.h"
#include "xenia/base/memory.h"
#include "xenia/base/profiling.h"
#include "xenia/base/reset_scope.h"
#include "xenia/cpu/compiler/compiler_passes.h"
#include "xenia/cpu/cpu_flags.h"
#include "xenia/cpu/frontend/ppc_disasm.h"
#include "xenia/cpu/frontend/ppc_frontend.h"
#include "xenia/cpu/frontend/ppc_hir_builder.h"
#include "xenia/cpu/frontend/ppc_instr.h"
#include "xenia/cpu/frontend/ppc_scanner.h"
#include "xenia/cpu/processor.h"
#include "xenia/debug/debugger.h"

DEFINE_bool(preserve_hir_disasm, true,
            "Preserves HIR disassembly for the debugger when it is attached.");

namespace xe {
namespace cpu {
namespace frontend {

using xe::cpu::backend::Backend;
using xe::cpu::compiler::Compiler;
namespace passes = xe::cpu::compiler::passes;

PPCTranslator::PPCTranslator(PPCFrontend* frontend) : frontend_(frontend) {
  Backend* backend = frontend->processor()->backend();

  scanner_.reset(new PPCScanner(frontend));
  builder_.reset(new PPCHIRBuilder(frontend));
  compiler_.reset(new Compiler(frontend->processor()));
  assembler_ = backend->CreateAssembler();
  assembler_->Initialize();

  bool validate = FLAGS_validate_hir;

  // Merge blocks early. This will let us use more context in other passes.
  // The CFG is required for simplification and dirtied by it.
  compiler_->AddPass(std::make_unique<passes::ControlFlowAnalysisPass>());
  compiler_->AddPass(std::make_unique<passes::ControlFlowSimplificationPass>());

  // Passes are executed in the order they are added. Multiple of the same
  // pass type may be used.
  if (validate) compiler_->AddPass(std::make_unique<passes::ValidationPass>());
  compiler_->AddPass(std::make_unique<passes::ContextPromotionPass>());
  if (validate) compiler_->AddPass(std::make_unique<passes::ValidationPass>());
  compiler_->AddPass(std::make_unique<passes::SimplificationPass>());
  if (validate) compiler_->AddPass(std::make_unique<passes::ValidationPass>());
  compiler_->AddPass(std::make_unique<passes::ConstantPropagationPass>());
  if (validate) compiler_->AddPass(std::make_unique<passes::ValidationPass>());
  if (backend->machine_info()->supports_extended_load_store) {
    // Backend supports the advanced LOAD/STORE instructions.
    // These will save us a lot of HIR opcodes.
    compiler_->AddPass(
        std::make_unique<passes::MemorySequenceCombinationPass>());
    if (validate)
      compiler_->AddPass(std::make_unique<passes::ValidationPass>());
  }
  compiler_->AddPass(std::make_unique<passes::SimplificationPass>());
  if (validate) compiler_->AddPass(std::make_unique<passes::ValidationPass>());
  // compiler_->AddPass(std::make_unique<passes::DeadStoreEliminationPass>());
  // if (validate)
  // compiler_->AddPass(std::make_unique<passes::ValidationPass>());
  compiler_->AddPass(std::make_unique<passes::DeadCodeEliminationPass>());
  if (validate) compiler_->AddPass(std::make_unique<passes::ValidationPass>());

  //// Removes all unneeded variables. Try not to add new ones after this.
  // compiler_->AddPass(new passes::ValueReductionPass());
  // if (validate) compiler_->AddPass(new passes::ValidationPass());

  // Register allocation for the target backend.
  // Will modify the HIR to add loads/stores.
  // This should be the last pass before finalization, as after this all
  // registers are assigned and ready to be emitted.
  compiler_->AddPass(std::make_unique<passes::RegisterAllocationPass>(
      backend->machine_info()));
  if (validate) compiler_->AddPass(std::make_unique<passes::ValidationPass>());

  // Must come last. The HIR is not really HIR after this.
  compiler_->AddPass(std::make_unique<passes::FinalizationPass>());
}

PPCTranslator::~PPCTranslator() = default;

bool PPCTranslator::Translate(GuestFunction* function,
                              uint32_t debug_info_flags) {
  SCOPE_profile_cpu_f("cpu");

  // Reset() all caching when we leave.
  xe::make_reset_scope(builder_);
  xe::make_reset_scope(compiler_);
  xe::make_reset_scope(assembler_);
  xe::make_reset_scope(&string_buffer_);

  // NOTE: we only want to do this when required, as it's expensive to build.
  if (FLAGS_preserve_hir_disasm && frontend_->processor()->debugger() &&
      frontend_->processor()->debugger()->is_attached()) {
    debug_info_flags |= DebugInfoFlags::kDebugInfoDisasmRawHir |
                        DebugInfoFlags::kDebugInfoDisasmHir;
  }
  if (FLAGS_disassemble_functions) {
    debug_info_flags |= DebugInfoFlags::kDebugInfoAllDisasm;
  }
  if (FLAGS_trace_functions) {
    debug_info_flags |= DebugInfoFlags::kDebugInfoTraceFunctions;
  }
  if (FLAGS_trace_function_coverage) {
    debug_info_flags |= DebugInfoFlags::kDebugInfoTraceFunctionCoverage;
  }
  if (FLAGS_trace_function_references) {
    debug_info_flags |= DebugInfoFlags::kDebugInfoTraceFunctionReferences;
  }
  if (FLAGS_trace_function_data) {
    debug_info_flags |= DebugInfoFlags::kDebugInfoTraceFunctionData;
  }
  std::unique_ptr<DebugInfo> debug_info;
  if (debug_info_flags) {
    debug_info.reset(new DebugInfo());
  }

  // Scan the function to find its extents and gather debug data.
  if (!scanner_->Scan(function, debug_info.get())) {
    return false;
  }

  auto debugger = frontend_->processor()->debugger();
  if (!debugger) {
    debug_info_flags &= ~DebugInfoFlags::kDebugInfoAllTracing;
  }

  // Setup trace data, if needed.
  if (debug_info_flags & DebugInfoFlags::kDebugInfoTraceFunctions) {
    // Base trace data.
    size_t trace_data_size = debug::FunctionTraceData::SizeOfHeader();
    if (debug_info_flags & DebugInfoFlags::kDebugInfoTraceFunctionCoverage) {
      // Additional space for instruction coverage counts.
      trace_data_size += debug::FunctionTraceData::SizeOfInstructionCounts(
          function->address(), function->end_address());
    }
    uint8_t* trace_data = debugger->AllocateFunctionTraceData(trace_data_size);
    if (trace_data) {
      function->trace_data().Reset(trace_data, trace_data_size,
                                   function->address(),
                                   function->end_address());
    }
  }

  // Stash source.
  if (debug_info_flags & DebugInfoFlags::kDebugInfoDisasmSource) {
    DumpSource(function, &string_buffer_);
    debug_info->set_source_disasm(string_buffer_.ToString());
    string_buffer_.Reset();
  }

  if (false) {
    xe::cpu::frontend::DumpAllInstrCounts();
  }

  // Emit function.
  uint32_t emit_flags = 0;
  // if (debug_info) {
  emit_flags |= PPCHIRBuilder::EMIT_DEBUG_COMMENTS;
  //}
  if (!builder_->Emit(function, emit_flags)) {
    return false;
  }

  // Stash raw HIR.
  if (debug_info_flags & DebugInfoFlags::kDebugInfoDisasmRawHir) {
    builder_->Dump(&string_buffer_);
    debug_info->set_raw_hir_disasm(string_buffer_.ToString());
    string_buffer_.Reset();
  }

  // Compile/optimize/etc.
  if (!compiler_->Compile(builder_.get())) {
    return false;
  }

  // Stash optimized HIR.
  if (debug_info_flags & DebugInfoFlags::kDebugInfoDisasmHir) {
    builder_->Dump(&string_buffer_);
    debug_info->set_hir_disasm(string_buffer_.ToString());
    string_buffer_.Reset();
  }

  // Assemble to backend machine code.
  if (!assembler_->Assemble(function, builder_.get(), debug_info_flags,
                            std::move(debug_info))) {
    return false;
  }

  return true;
}

void PPCTranslator::DumpSource(GuestFunction* function,
                               StringBuffer* string_buffer) {
  Memory* memory = frontend_->memory();

  string_buffer->AppendFormat(
      "%s fn %.8X-%.8X %s\n", function->module()->name().c_str(),
      function->address(), function->end_address(), function->name().c_str());

  auto blocks = scanner_->FindBlocks(function);

  uint32_t start_address = function->address();
  uint32_t end_address = function->end_address();
  InstrData i;
  auto block_it = blocks.begin();
  for (uint32_t address = start_address, offset = 0; address <= end_address;
       address += 4, offset++) {
    i.address = address;
    i.code = xe::load_and_swap<uint32_t>(memory->TranslateVirtual(address));
    // TODO(benvanik): find a way to avoid using the opcode tables.
    i.type = GetInstrType(i.code);

    // Check labels.
    if (block_it != blocks.end() && block_it->start_address == address) {
      string_buffer->AppendFormat("%.8X          loc_%.8X:\n", address,
                                  address);
      ++block_it;
    }

    string_buffer->AppendFormat("%.8X %.8X   ", address, i.code);
    DisasmPPC(&i, string_buffer);
    string_buffer->Append('\n');
  }
}

}  // namespace frontend
}  // namespace cpu
}  // namespace xe
