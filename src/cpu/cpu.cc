/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/cpu.h>

#include <llvm/PassManager.h>
#include <llvm/Analysis/Verifier.h>
#include <llvm/Bitcode/ReaderWriter.h>
#include <llvm/ExecutionEngine/GenericValue.h>
#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/ExecutionEngine/Interpreter.h>
#include <llvm/ExecutionEngine/JIT.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/Host.h>
#include <llvm/Support/ManagedStatic.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/system_error.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/Threading.h>
#include <llvm/Transforms/IPO.h>
#include <llvm/Transforms/IPO/PassManagerBuilder.h>

#include <xenia/cpu/sdb.h>

#include "cpu/codegen.h"
#include "cpu/xethunk/xethunk.h"

using namespace llvm;


typedef struct {
  xe_module_ref   module;
  xe_sdb_ref      sdb;
  LLVMContext     *context;
  Module          *m;
} xe_cpu_module_entry_t;

typedef struct xe_cpu {
  xe_ref_t ref;

  xe_cpu_options_t options;

  xe_pal_ref      pal;
  xe_memory_ref   memory;

  std::vector<xe_cpu_module_entry_t> entries;

  ExecutionEngine *engine;
} xe_cpu_t;


int xe_cpu_setup_engine(xe_cpu_ref cpu, Module *gen_module);
int xe_cpu_init_module(xe_cpu_ref, Module *gen_module);
void xe_cpu_uninit_module(xe_cpu_ref cpu, xe_cpu_module_entry_t *module_entry);


xe_cpu_ref xe_cpu_create(xe_pal_ref pal, xe_memory_ref memory,
                         xe_cpu_options_t options) {
  xe_cpu_ref cpu = (xe_cpu_ref)xe_calloc(sizeof(xe_cpu));
  xe_ref_init((xe_ref)cpu);

  xe_copy_struct(&cpu->options, &options, sizeof(xe_cpu_options_t));

  cpu->pal = xe_pal_retain(pal);
  cpu->memory = xe_memory_retain(memory);

  LLVMLinkInInterpreter();
  LLVMLinkInJIT();
  InitializeNativeTarget();
  XEEXPECTTRUE(llvm_start_multithreaded());

  return cpu;

XECLEANUP:
  xe_cpu_release(cpu);
  return NULL;
}

void xe_cpu_dealloc(xe_cpu_ref cpu) {
  // Cleanup all modules.
  for (std::vector<xe_cpu_module_entry_t>::iterator it = cpu->entries.begin();
       it != cpu->entries.end(); ++it) {
    xe_cpu_uninit_module(cpu, &*it);
    cpu->engine->removeModule(it->m);
    delete it->m;
    delete it->context;
    xe_sdb_release(it->sdb);
    xe_module_release(it->module);
  }

  delete cpu->engine;
  llvm_shutdown();

  xe_memory_release(cpu->memory);
  xe_pal_release(cpu->pal);
}

xe_cpu_ref xe_cpu_retain(xe_cpu_ref cpu) {
  xe_ref_retain((xe_ref)cpu);
  return cpu;
}

void xe_cpu_release(xe_cpu_ref cpu) {
  xe_ref_release((xe_ref)cpu, (xe_ref_dealloc_t)xe_cpu_dealloc);
}

xe_pal_ref xe_cpu_get_pal(xe_cpu_ref cpu) {
  return xe_pal_retain(cpu->pal);
}

xe_memory_ref xe_cpu_get_memory(xe_cpu_ref cpu) {
  return xe_memory_retain(cpu->memory);
}

int xe_cpu_setup_engine(xe_cpu_ref cpu, Module *gen_module) {
  if (cpu->engine) {
    // Engine already initialized - just add the module.
    cpu->engine->addModule(gen_module);
    return 0;
  }

  std::string error_message;
  cpu->engine = ExecutionEngine::create(gen_module, false, &error_message,
                                        CodeGenOpt::Aggressive);

  return 0;
}

int xe_cpu_prepare_module(xe_cpu_ref cpu, xe_module_ref module,
                          xe_kernel_export_resolver_ref export_resolver) {
  int result_code = 1;
  std::string error_message;

  xe_sdb_ref sdb = NULL;
  LLVMContext *context = NULL;
  OwningPtr<MemoryBuffer> shared_module_buffer;
  Module *gen_module = NULL;
  Module *shared_module = NULL;
  raw_ostream *outs = NULL;

  PassManager pm;
  PassManagerBuilder pmb;

  // TODO(benvanik): embed the bc file into the emulator.
  const char *thunk_path = "src/cpu/xethunk/xethunk.bc";

  // Create a LLVM context for this prepare.
  // This is required to ensure thread safety/etc.
  context = new LLVMContext();

  // Calculate a cache path based on the module, the CPU version, and other
  // bits.
  // TODO(benvanik): cache path calculation.
  const char *cache_path = "build/generated.bc";

  // Check the cache to see if the bitcode exists.
  // If it does, load that module directly. In the future we could also cache
  // on linked binaries but that requires more safety around versioning.
  // TODO(benvanik): check cache for module bitcode and load.
  // if (path_exists(cache_key)) {
  //   gen_module = load_bitcode(cache_key);
  //   sdb = load_symbol_table(cache_key);
  // }

  // If not found in cache, generate a new module.
  if (!gen_module) {
    // Load shared bitcode files.
    // These contain globals and common thunk code that are used by the
    // generated code.
    XEEXPECTZERO(MemoryBuffer::getFile(thunk_path, shared_module_buffer));
    shared_module = ParseBitcodeFile(&*shared_module_buffer, *context,
                                     &error_message);
    XEEXPECTNOTNULL(shared_module);

    // Analyze the module and add its symbols to the symbol database.
    sdb = xe_sdb_create(cpu->memory, module);
    XEEXPECTNOTNULL(sdb);
    xe_sdb_dump(sdb);

    // Build the module from the source code.
    xe_codegen_options_t codegen_options;
    xe_zero_struct(&codegen_options, sizeof(codegen_options));
    xe_codegen_ctx_t codegen_ctx;
    xe_zero_struct(&codegen_ctx, sizeof(codegen_ctx));
    codegen_ctx.memory = cpu->memory;
    codegen_ctx.export_resolver = export_resolver;
    codegen_ctx.module = module;
    codegen_ctx.sdb = sdb;
    codegen_ctx.context = context;
    codegen_ctx.shared_module = shared_module;
    gen_module = xe_codegen(&codegen_ctx, codegen_options);

    // Write to cache.
    outs = new raw_fd_ostream(cache_path, error_message,
                              raw_fd_ostream::F_Binary);
    XEEXPECTTRUE(error_message.empty());
    WriteBitcodeToFile(gen_module, *outs);
  }

  // Link optimizations.
  XEEXPECTZERO(gen_module->MaterializeAllPermanently(&error_message));

  // Reset target triple (ignore what's in xethunk).
  gen_module->setTargetTriple(llvm::sys::getDefaultTargetTriple());

  // Run full module optimizations.
  pm.add(new DataLayout(gen_module));
  pm.add(createVerifierPass());
  pmb.OptLevel      = 3;
  pmb.SizeLevel     = 0;
  pmb.Inliner       = createFunctionInliningPass();
  pmb.Vectorize     = true;
  pmb.LoopVectorize = true;
  pmb.populateModulePassManager(pm);
  pmb.populateLTOPassManager(pm, false, true);
  pm.add(createVerifierPass());
  pm.run(*gen_module);

  // TODO(benvanik): experiment with LLD to see if we can write out a dll.

  // Setup the execution engine (if needed).
  // The engine is required to get the data layout and other values.
  XEEXPECTZERO(xe_cpu_setup_engine(cpu, gen_module));

  // Initialize the module.
  XEEXPECTZERO(xe_cpu_init_module(cpu, gen_module));

  // Force JIT of all functions.
  for (Module::iterator I = gen_module->begin(), E = gen_module->end();
       I != E; I++) {
    Function* fn = &*I;
    if (!fn->isDeclaration()) {
      cpu->engine->getPointerToFunction(fn);
    }
  }

  gen_module->dump();

  // Stash the module entry to allow cleanup later.
  xe_cpu_module_entry_t module_entry;
  module_entry.module   = xe_module_retain(module);
  module_entry.sdb      = xe_sdb_retain(sdb);
  module_entry.context  = context;
  module_entry.m        = gen_module;
  cpu->entries.push_back(module_entry);

  result_code = 0;
XECLEANUP:
  delete outs;
  delete shared_module;
  if (result_code) {
    delete gen_module;
    delete context;
  }
  xe_sdb_release(sdb);
  return result_code;
}

int xe_cpu_init_module(xe_cpu_ref cpu, Module *gen_module) {
  // Run static initializers. I'm not sure we'll have any, but who knows.
  cpu->engine->runStaticConstructorsDestructors(gen_module, false);

  // Prepare init options.
  xe_module_init_options_t init_options;
  xe_zero_struct(&init_options, sizeof(init_options));
  init_options.memory_base = xe_memory_addr(cpu->memory, 0);

  // Grab the init function and call it.
  Function *xe_module_init = gen_module->getFunction("xe_module_init");
  std::vector<GenericValue> args;
  args.push_back(GenericValue(&init_options));
  GenericValue ret = cpu->engine->runFunction(xe_module_init, args);

  return ret.IntVal.getSExtValue();
}

void xe_cpu_uninit_module(xe_cpu_ref cpu, xe_cpu_module_entry_t *module_entry) {
  // Grab function and call it.
  Function *xe_module_uninit = module_entry->m->getFunction("xe_module_uninit");
  std::vector<GenericValue> args;
  cpu->engine->runFunction(xe_module_uninit, args);

  // Run static destructors.
  cpu->engine->runStaticConstructorsDestructors(module_entry->m, true);
}

int xe_cpu_execute(xe_cpu_ref cpu, uint32_t address) {
  // TODO(benvanik): implement execute.
  return 0;
}

uint32_t xe_cpu_create_callback(xe_cpu_ref cpu,
                                void (*callback)(void*), void *data) {
  // TODO(benvanik): implement callback creation.
  return 0;
}
