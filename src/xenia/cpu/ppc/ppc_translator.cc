/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/ppc/ppc_translator.h"

#include "xenia/base/assert.h"
#include "xenia/base/byte_order.h"
#include "xenia/base/cvar.h"
#include "xenia/base/memory.h"
#include "xenia/base/profiling.h"
#include "xenia/base/reset_scope.h"
#include "xenia/base/string.h"
#include "xenia/cpu/compiler/compiler_passes.h"
#include "xenia/cpu/cpu_flags.h"
#include "xenia/cpu/ppc/ppc_frontend.h"
#include "xenia/cpu/ppc/ppc_hir_builder.h"
#include "xenia/cpu/ppc/ppc_opcode_info.h"
#include "xenia/cpu/ppc/ppc_scanner.h"
#include "xenia/cpu/processor.h"
#include "xenia/cpu/xex_module.h"

DEFINE_bool(dump_translated_hir_functions, false, "dumps translated hir",
            "CPU");

DEFINE_bool(disable_context_promotion, false,
            "Disables Context Promotion optimizations, this may be needed for "
            "some sports games, but will reduce performance.",
            "CPU");

namespace xe {
namespace cpu {
namespace ppc {

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

  bool validate = cvars::validate_hir;

  // Merge blocks early. This will let us use more context in other passes.
  // The CFG is required for simplification and dirtied by it.
  compiler_->AddPass(std::make_unique<passes::ControlFlowAnalysisPass>());
  compiler_->AddPass(std::make_unique<passes::ControlFlowSimplificationPass>());

  // Passes are executed in the order they are added. Multiple of the same
  // pass type may be used.
  if (!cvars::disable_context_promotion) {
    if (validate) {
      compiler_->AddPass(std::make_unique<passes::ValidationPass>());
    }

    compiler_->AddPass(std::make_unique<passes::ContextPromotionPass>());

    if (validate) {
      compiler_->AddPass(std::make_unique<passes::ValidationPass>());
    }
  }
  // Grouped simplification + constant propagation.
  // Loops until no changes are made.
  auto sap = std::make_unique<passes::ConditionalGroupPass>();
  sap->AddPass(std::make_unique<passes::SimplificationPass>());
  if (validate) sap->AddPass(std::make_unique<passes::ValidationPass>());
  sap->AddPass(std::make_unique<passes::ConstantPropagationPass>());
  if (validate) sap->AddPass(std::make_unique<passes::ValidationPass>());
  compiler_->AddPass(std::move(sap));

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

class HirBuilderScope {
  PPCHIRBuilder* builder_;

 public:
  HirBuilderScope(PPCHIRBuilder* builder) : builder_(builder) {
    builder_->MakeCurrent();
  }

  ~HirBuilderScope() {
    if (builder_) {
      builder_->RemoveCurrent();
    }
  }
};
void PPCTranslator::DumpHIR(GuestFunction* function, PPCHIRBuilder* builder) {
  if (cvars::dump_translated_hir_functions) {
    StringBuffer buffer{};
    builder_->Dump(&buffer);

    XexModule* mod = dynamic_cast<XexModule*>(function->module());

    std::wstring folder_name = L"hirdump";

    if (mod) {
      xex2_opt_execution_info* opt_exec_info = nullptr;
      if (mod->GetOptHeader(XEX_HEADER_EXECUTION_INFO, &opt_exec_info)) {
        folder_name =
            L"hirdump_title_" + std::to_wstring(opt_exec_info->title_id);
      }
    }
    std::filesystem::path folder_path{folder_name};

    if (!std::filesystem::exists(folder_path)) {
      std::filesystem::create_directory(folder_path);
    }

    {
      wchar_t tmpbuf[64];
#ifdef XE_PLATFORM_WIN32
      _snwprintf(tmpbuf, 64, L"%X", function->address());
#else
      swprintf(tmpbuf, 64, L"%X", function->address());
#endif
      folder_path.append(&tmpbuf[0]);
    }

    FILE* f = fopen(folder_path.string().c_str(), "w");
    if (f) {
      fputs(buffer.buffer(), f);
      fclose(f);
    }
  }
}
bool PPCTranslator::Translate(GuestFunction* function,
                              uint32_t debug_info_flags) {
  SCOPE_profile_cpu_f("cpu");
  HirBuilderScope hir_build_scope{builder_.get()};
  // Reset() all caching when we leave.
  xe::make_reset_scope(builder_);
  xe::make_reset_scope(compiler_);
  xe::make_reset_scope(assembler_);
  xe::make_reset_scope(&string_buffer_);

  // NOTE: we only want to do this when required, as it's expensive to build.
  if (cvars::disassemble_functions) {
    debug_info_flags |= DebugInfoFlags::kDebugInfoAllDisasm;
  }
  if (cvars::trace_functions) {
    debug_info_flags |= DebugInfoFlags::kDebugInfoTraceFunctions;
  }
  if (cvars::trace_function_coverage) {
    debug_info_flags |= DebugInfoFlags::kDebugInfoTraceFunctionCoverage;
  }
  if (cvars::trace_function_references) {
    debug_info_flags |= DebugInfoFlags::kDebugInfoTraceFunctionReferences;
  }
  if (cvars::trace_function_data) {
    debug_info_flags |= DebugInfoFlags::kDebugInfoTraceFunctionData;
  }
  std::unique_ptr<FunctionDebugInfo> debug_info;
  if (debug_info_flags) {
    debug_info.reset(new FunctionDebugInfo());
  }

  // Scan the function to find its extents and gather debug data.
  if (!scanner_->Scan(function, debug_info.get())) {
    return false;
  }

  // Setup trace data, if needed.
  if (debug_info_flags & DebugInfoFlags::kDebugInfoTraceFunctions) {
    // Base trace data.
    size_t trace_data_size = FunctionTraceData::SizeOfHeader();
    if (debug_info_flags & DebugInfoFlags::kDebugInfoTraceFunctionCoverage) {
      // Additional space for instruction coverage counts.
      trace_data_size += FunctionTraceData::SizeOfInstructionCounts(
          function->address(), function->end_address());
    }
    uint8_t* trace_data =
        frontend_->processor()->AllocateFunctionTraceData(trace_data_size);
    if (trace_data) {
      function->trace_data().Reset(trace_data, trace_data_size,
                                   function->address(),
                                   function->end_address());
    } else {
      debug_info_flags &= ~(DebugInfoFlags::kDebugInfoTraceFunctions |
                            DebugInfoFlags::kDebugInfoTraceFunctionCoverage);
    }
  }

  // Stash source.
  if (debug_info_flags & DebugInfoFlags::kDebugInfoDisasmSource) {
    DumpSource(function, &string_buffer_);
    debug_info->set_source_disasm(xe_strdup(string_buffer_.buffer()));
    string_buffer_.Reset();
  }

  // Emit function.
  uint32_t emit_flags = 0;
  if (debug_info) {
    emit_flags |= PPCHIRBuilder::EMIT_DEBUG_COMMENTS;
  }
  if (!builder_->Emit(function, emit_flags)) {
    return false;
  }

  // Stash raw HIR.
  if (debug_info_flags & DebugInfoFlags::kDebugInfoDisasmRawHir) {
    builder_->Dump(&string_buffer_);
    debug_info->set_raw_hir_disasm(xe_strdup(string_buffer_.buffer()));
    string_buffer_.Reset();
  }

  // Compile/optimize/etc.
  if (!compiler_->Compile(builder_.get())) {
    return false;
  }

  // Stash optimized HIR.
  if (debug_info_flags & DebugInfoFlags::kDebugInfoDisasmHir) {
    builder_->Dump(&string_buffer_);
    debug_info->set_hir_disasm(xe_strdup(string_buffer_.buffer()));
    string_buffer_.Reset();
  }

  DumpHIR(function, builder_.get());

  // Assemble to backend machine code.
  if (!assembler_->Assemble(function, builder_.get(), debug_info_flags,
                            std::move(debug_info))) {
    return false;
  }

  return true;
}
void PPCTranslator::Reset() { builder_->ResetPools(); }
void PPCTranslator::DumpSource(GuestFunction* function,
                               StringBuffer* string_buffer) {
  Memory* memory = frontend_->memory();

  string_buffer->AppendFormat(
      "{} fn {:08X}-{:08X} {}\n", function->module()->name().c_str(),
      function->address(), function->end_address(), function->name().c_str());

  auto blocks = scanner_->FindBlocks(function);

  uint32_t start_address = function->address();
  uint32_t end_address = function->end_address();
  auto block_it = blocks.begin();
  for (uint32_t address = start_address, offset = 0; address <= end_address;
       address += 4, offset++) {
    uint32_t code =
        xe::load_and_swap<uint32_t>(memory->TranslateVirtual(address));

    // Check labels.
    if (block_it != blocks.end() && block_it->start_address == address) {
      string_buffer->AppendFormat("{:08X}          loc_{:08X}:\n", address,
                                  address);
      ++block_it;
    }

    string_buffer->AppendFormat("{:08X} {:08X}   ", address, code);
    DisasmPPC(address, code, string_buffer);
    string_buffer->Append('\n');
  }
}

}  // namespace ppc
}  // namespace cpu
}  // namespace xe
