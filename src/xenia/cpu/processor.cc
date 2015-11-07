/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/processor.h"

#include <gflags/gflags.h>

#include "xenia/base/assert.h"
#include "xenia/base/atomic.h"
#include "xenia/base/byte_order.h"
#include "xenia/base/logging.h"
#include "xenia/base/memory.h"
#include "xenia/base/profiling.h"
#include "xenia/cpu/cpu_flags.h"
#include "xenia/cpu/export_resolver.h"
#include "xenia/cpu/frontend/ppc_frontend.h"
#include "xenia/cpu/module.h"
#include "xenia/cpu/stack_walker.h"
#include "xenia/cpu/thread_state.h"
#include "xenia/cpu/xex_module.h"
#include "xenia/debug/debugger.h"

// TODO(benvanik): based on compiler support
#include "xenia/cpu/backend/x64/x64_backend.h"

namespace xe {
namespace cpu {

using PPCContext = xe::cpu::frontend::PPCContext;

class BuiltinModule : public Module {
 public:
  explicit BuiltinModule(Processor* processor)
      : Module(processor), name_("builtin") {}

  const std::string& name() const override { return name_; }

  bool ContainsAddress(uint32_t address) override {
    return (address & 0xFFFFFFF0) == 0xFFFFFFF0;
  }

 protected:
  std::unique_ptr<Function> CreateFunction(uint32_t address) override {
    return std::unique_ptr<Function>(new BuiltinFunction(this, address));
  }

 private:
  std::string name_;
};

Processor::Processor(xe::Memory* memory, ExportResolver* export_resolver,
                     debug::Debugger* debugger)
    : memory_(memory), debugger_(debugger), export_resolver_(export_resolver) {}

Processor::~Processor() {
  {
    auto global_lock = global_critical_region_.Acquire();
    modules_.clear();
  }

  frontend_.reset();
  backend_.reset();
}

bool Processor::Setup() {
  // TODO(benvanik): query mode from debugger?
  debug_info_flags_ = 0;

  auto frontend = std::make_unique<xe::cpu::frontend::PPCFrontend>(this);
  // TODO(benvanik): set options/etc.

  // Must be initialized by subclass before calling into this.
  assert_not_null(memory_);

  std::unique_ptr<Module> builtin_module(new BuiltinModule(this));
  builtin_module_ = builtin_module.get();
  modules_.push_back(std::move(builtin_module));

  if (frontend_ || backend_) {
    return false;
  }

  std::unique_ptr<xe::cpu::backend::Backend> backend;
  if (!backend) {
#if defined(XENIA_HAS_X64_BACKEND) && XENIA_HAS_X64_BACKEND
    if (FLAGS_cpu == "x64") {
      backend.reset(new xe::cpu::backend::x64::X64Backend(this));
    }
#endif  // XENIA_HAS_X64_BACKEND
    if (FLAGS_cpu == "any") {
#if defined(XENIA_HAS_X64_BACKEND) && XENIA_HAS_X64_BACKEND
      if (!backend) {
        backend.reset(new xe::cpu::backend::x64::X64Backend(this));
      }
#endif  // XENIA_HAS_X64_BACKEND
    }
  }

  if (!backend) {
    return false;
  }
  if (!backend->Initialize()) {
    return false;
  }
  if (!frontend->Initialize()) {
    return false;
  }

  backend_ = std::move(backend);
  frontend_ = std::move(frontend);

  // Stack walker is used when profiling, debugging, and dumping.
  stack_walker_ = StackWalker::Create(backend_->code_cache());
  if (!stack_walker_) {
    XELOGE("Unable to create stack walker");
    return false;
  }

  return true;
}

bool Processor::AddModule(std::unique_ptr<Module> module) {
  auto global_lock = global_critical_region_.Acquire();
  modules_.push_back(std::move(module));
  return true;
}

Module* Processor::GetModule(const char* name) {
  auto global_lock = global_critical_region_.Acquire();
  for (const auto& module : modules_) {
    if (module->name() == name) {
      return module.get();
    }
  }
  return nullptr;
}

std::vector<Module*> Processor::GetModules() {
  auto global_lock = global_critical_region_.Acquire();
  std::vector<Module*> clone(modules_.size());
  for (const auto& module : modules_) {
    clone.push_back(module.get());
  }
  return clone;
}

Function* Processor::DefineBuiltin(const std::string& name,
                                   BuiltinFunction::Handler handler, void* arg0,
                                   void* arg1) {
  uint32_t address = next_builtin_address_;
  next_builtin_address_ += 4;

  Function* function;
  builtin_module_->DeclareFunction(address, &function);
  function->set_end_address(address + 4);
  function->set_name(name);

  auto builtin_function = static_cast<BuiltinFunction*>(function);
  builtin_function->SetupBuiltin(handler, arg0, arg1);

  function->set_status(Symbol::Status::kDeclared);
  return function;
}

Function* Processor::QueryFunction(uint32_t address) {
  auto entry = entry_table_.Get(address);
  if (!entry) {
    return nullptr;
  }
  return entry->function;
}

std::vector<Function*> Processor::FindFunctionsWithAddress(uint32_t address) {
  return entry_table_.FindWithAddress(address);
}

Function* Processor::ResolveFunction(uint32_t address) {
  Entry* entry;
  Entry::Status status = entry_table_.GetOrCreate(address, &entry);
  if (status == Entry::STATUS_NEW) {
    // Needs to be generated. We have the 'lock' on it and must do so now.

    // Grab symbol declaration.
    auto function = LookupFunction(address);
    if (!function) {
      entry->status = Entry::STATUS_FAILED;
      return nullptr;
    }

    if (!DemandFunction(function)) {
      entry->status = Entry::STATUS_FAILED;
      return nullptr;
    }
    entry->function = function;
    entry->end_address = function->end_address();
    status = entry->status = Entry::STATUS_READY;
  }
  if (status == Entry::STATUS_READY) {
    // Ready to use.
    return entry->function;
  } else {
    // Failed or bad state.
    return nullptr;
  }
}

Function* Processor::LookupFunction(uint32_t address) {
  // TODO(benvanik): fast reject invalid addresses/log errors.

  // Find the module that contains the address.
  Module* code_module = nullptr;
  {
    auto global_lock = global_critical_region_.Acquire();
    // TODO(benvanik): sort by code address (if contiguous) so can bsearch.
    // TODO(benvanik): cache last module low/high, as likely to be in there.
    for (const auto& module : modules_) {
      if (module->ContainsAddress(address)) {
        code_module = module.get();
        break;
      }
    }
  }
  if (!code_module) {
    // No module found that could contain the address.
    return nullptr;
  }

  return LookupFunction(code_module, address);
}

Function* Processor::LookupFunction(Module* module, uint32_t address) {
  // Atomic create/lookup symbol in module.
  // If we get back the NEW flag we must declare it now.
  Function* function = nullptr;
  auto symbol_status = module->DeclareFunction(address, &function);
  if (symbol_status == Symbol::Status::kNew) {
    // Symbol is undeclared, so declare now.
    assert_true(function->is_guest());
    if (!frontend_->DeclareFunction(static_cast<GuestFunction*>(function))) {
      function->set_status(Symbol::Status::kFailed);
      return nullptr;
    }
    function->set_status(Symbol::Status::kDeclared);
  }
  return function;
}

bool Processor::DemandFunction(Function* function) {
  // Lock function for generation. If it's already being generated
  // by another thread this will block and return DECLARED.
  auto module = function->module();
  auto symbol_status = module->DefineFunction(function);
  if (symbol_status == Symbol::Status::kNew) {
    // Symbol is undefined, so define now.
    assert_true(function->is_guest());
    if (!frontend_->DefineFunction(static_cast<GuestFunction*>(function),
                                   debug_info_flags_)) {
      function->set_status(Symbol::Status::kFailed);
      return false;
    }

    // Before we give the symbol back to the rest, let the debugger know.
    if (debugger_) {
      debugger_->OnFunctionDefined(function);
    }

    function->set_status(Symbol::Status::kDefined);
    symbol_status = function->status();
  }

  if (symbol_status == Symbol::Status::kFailed) {
    // Symbol likely failed.
    return false;
  }

  return true;
}

bool Processor::Execute(ThreadState* thread_state, uint32_t address) {
  SCOPE_profile_cpu_f("cpu");

  // Attempt to get the function.
  auto function = ResolveFunction(address);
  if (!function) {
    // Symbol not found in any module.
    XELOGCPU("Execute(%.8X): failed to find function", address);
    return false;
  }

  PPCContext* context = thread_state->context();

  // Pad out stack a bit, as some games seem to overwrite the caller by about
  // 16 to 32b.
  context->r[1] -= 64 + 112;

  // This could be set to anything to give us a unique identifier to track
  // re-entrancy/etc.
  uint64_t previous_lr = context->lr;
  context->lr = 0xBCBCBCBC;

  // Execute the function.
  auto result = function->Call(thread_state, uint32_t(context->lr));

  context->lr = previous_lr;
  context->r[1] += 64 + 112;

  return result;
}

uint64_t Processor::Execute(ThreadState* thread_state, uint32_t address,
                            uint64_t args[], size_t arg_count) {
  SCOPE_profile_cpu_f("cpu");

  PPCContext* context = thread_state->context();
  for (size_t i = 0; i < std::min(arg_count, 8ull); ++i) {
    context->r[3 + i] = args[i];
  }

  if (arg_count > 7) {
    // Rest of the arguments go on the stack.
    // FIXME: This assumes arguments are 32 bits!
    auto stack_arg_base =
        memory()->TranslateVirtual((uint32_t)context->r[1] + 0x54 - (64 + 112));
    for (size_t i = 0; i < arg_count - 8; i++) {
      xe::store_and_swap<uint32_t>(stack_arg_base + (i * 8),
                                   (uint32_t)args[i + 8]);
    }
  }

  if (!Execute(thread_state, address)) {
    return 0xDEADBABE;
  }
  return context->r[3];
}

uint64_t Processor::ExecuteInterrupt(ThreadState* thread_state,
                                     uint32_t address, uint64_t args[],
                                     size_t arg_count) {
  SCOPE_profile_cpu_f("cpu");

  // Hold the global lock during interrupt dispatch.
  // This will block if any code is in a critical region (has interrupts
  // disabled) or if any other interrupt is executing.
  auto global_lock = global_critical_region_.Acquire();

  PPCContext* context = thread_state->context();
  assert_true(arg_count <= 5);
  for (size_t i = 0; i < arg_count; ++i) {
    context->r[3 + i] = args[i];
  }

  // TLS ptr must be zero during interrupts. Some games check this and
  // early-exit routines when under interrupts.
  auto pcr_address =
      memory_->TranslateVirtual(static_cast<uint32_t>(context->r[13]));
  uint32_t old_tls_ptr = xe::load_and_swap<uint32_t>(pcr_address);
  xe::store_and_swap<uint32_t>(pcr_address, 0);

  if (!Execute(thread_state, address)) {
    return 0xDEADBABE;
  }

  // Restores TLS ptr.
  xe::store_and_swap<uint32_t>(pcr_address, old_tls_ptr);

  return context->r[3];
}

Irql Processor::RaiseIrql(Irql new_value) {
  return static_cast<Irql>(
      xe::atomic_exchange(static_cast<uint32_t>(new_value),
                          reinterpret_cast<volatile uint32_t*>(&irql_)));
}

void Processor::LowerIrql(Irql old_value) {
  xe::atomic_exchange(static_cast<uint32_t>(old_value),
                      reinterpret_cast<volatile uint32_t*>(&irql_));
}

}  // namespace cpu
}  // namespace xe
