/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <alloy/backend/x64/x64_emitter.h>

#include <alloy/backend/x64/x64_backend.h>
#include <alloy/backend/x64/x64_code_cache.h>
#include <alloy/backend/x64/lir/lir_builder.h>

#include <third_party/xbyak/xbyak/xbyak.h>

using namespace alloy;
using namespace alloy::backend;
using namespace alloy::backend::x64;
using namespace alloy::backend::x64::lir;
using namespace alloy::runtime;

using namespace Xbyak;


namespace alloy {
namespace backend {
namespace x64 {

class XbyakAllocator : public Allocator {
public:
	virtual bool useProtect() const { return false; }
};

class XbyakGenerator : public CodeGenerator {
public:
  XbyakGenerator(XbyakAllocator* allocator);
  virtual ~XbyakGenerator();
  void* Emplace(X64CodeCache* code_cache);
  int Emit(LIRBuilder* builder);
private:
};

}  // namespace x64
}  // namespace backend
}  // namespace alloy


X64Emitter::X64Emitter(X64Backend* backend) :
    backend_(backend),
    code_cache_(backend->code_cache()) {
  allocator_ = new XbyakAllocator();
  generator_ = new XbyakGenerator(allocator_);
}

X64Emitter::~X64Emitter() {
  delete generator_;
  delete allocator_;
}

int X64Emitter::Initialize() {
  return 0;
}

int X64Emitter::Emit(
    LIRBuilder* builder, void*& out_code_address, size_t& out_code_size) {
  // Fill the generator with code.
  int result = generator_->Emit(builder);
  if (result) {
    return result;
  }

  // Copy the final code to the cache and relocate it.
  out_code_size = generator_->getSize();
  out_code_address = generator_->Emplace(code_cache_);

  return 0;
}

XbyakGenerator::XbyakGenerator(XbyakAllocator* allocator) :
    CodeGenerator(1 * 1024 * 1024, AutoGrow, allocator) {
}

XbyakGenerator::~XbyakGenerator() {
}

void* XbyakGenerator::Emplace(X64CodeCache* code_cache) {
  // To avoid changing xbyak, we do a switcharoo here.
  // top_ points to the Xbyak buffer, and since we are in AutoGrow mode
  // it has pending relocations. We copy the top_ to our buffer, swap the
  // pointer, relocate, then return the original scratch pointer for use.
  uint8_t* old_address = top_;
  void* new_address = code_cache->PlaceCode(top_, size_);
  top_ = (uint8_t*)new_address;
  ready();
  top_ = old_address;
  reset();
  return new_address;
}

int XbyakGenerator::Emit(LIRBuilder* builder) {
  //
  xor(rax, rax);
  ret();
  return 0;
}
