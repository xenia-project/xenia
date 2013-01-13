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


void do_cpu_stuff() {
  XELOGCPU(XT("cpu"));

  LLVMContext &context = getGlobalContext();
  //IRBuilder<> builder(context);
  Module *module = new Module("my cool jit", context);

  Constant* c = module->getOrInsertFunction("mul_add",
  /*ret type*/                           IntegerType::get(context, 32),
  /*args*/                               IntegerType::get(context, 32),
                                         IntegerType::get(context, 32),
                                         IntegerType::get(context, 32),
  /*varargs terminated with null*/       NULL);

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

  module->dump();
  delete module;
}
