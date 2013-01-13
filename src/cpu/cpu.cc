/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/cpu.h>

#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>

using namespace llvm;


typedef struct xe_cpu {
  xe_ref_t ref;

  xe_cpu_options_t options;

  xe_pal_ref      pal;
  xe_memory_ref   memory;
} xe_cpu_t;


xe_cpu_ref xe_cpu_create(xe_pal_ref pal, xe_memory_ref memory,
                         xe_cpu_options_t options) {
  xe_cpu_ref cpu = (xe_cpu_ref)xe_calloc(sizeof(xe_cpu));
  xe_ref_init((xe_ref)cpu);

  xe_copy_struct(&cpu->options, &options, sizeof(xe_cpu_options_t));

  cpu->pal = xe_pal_retain(pal);
  cpu->memory = xe_memory_retain(memory);

  return cpu;
}

void xe_cpu_dealloc(xe_cpu_ref cpu) {
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

int xe_cpu_prepare_module(xe_cpu_ref cpu, xe_module_ref module) {
  // TODO(benvanik): lookup the module in the cache.

  // TODO(benvanik): implement prepare module.

  XELOGCPU(XT("cpu"));

  LLVMContext &context = getGlobalContext();
  //IRBuilder<> builder(context);
  Module *m = new Module("my cool jit", context);

  Constant* c = m->getOrInsertFunction("mul_add",
  /*ret type*/                         IntegerType::get(context, 32),
  /*args*/                             IntegerType::get(context, 32),
                                       IntegerType::get(context, 32),
                                       IntegerType::get(context, 32),
  /*varargs terminated with null*/     NULL);

  Function* mul_add = cast<Function>(c);
  mul_add->setCallingConv(CallingConv::C);

  Function::arg_iterator args = mul_add->arg_begin();
  Value* x = args++;
  x->setName("x");
  Value* y = args++;
  y->setName("y");
  Value* z = args++;
  z->setName("z");

  BasicBlock* block = BasicBlock::Create(getGlobalContext(), "entry", mul_add);
  IRBuilder<> builder(block);

  Value* tmp = builder.CreateBinOp(Instruction::Mul,
                                   x, y, "tmp");
  Value* tmp2 = builder.CreateBinOp(Instruction::Add,
                                    tmp, z, "tmp2");

  builder.CreateRet(tmp2);

  XELOGD(XT("woo %d"), 123);

  m->dump();
  delete m;
  return 0;
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
