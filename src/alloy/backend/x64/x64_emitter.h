/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef ALLOY_BACKEND_X64_X64_EMITTER_H_
#define ALLOY_BACKEND_X64_X64_EMITTER_H_

#include <alloy/core.h>

#include <third_party/xbyak/xbyak/xbyak.h>

XEDECLARECLASS2(alloy, hir, HIRBuilder);

namespace alloy {
namespace backend {
namespace x64 {

class X64Backend;
class X64CodeCache;

// Unfortunately due to the design of xbyak we have to pass this to the ctor.
class XbyakAllocator : public Xbyak::Allocator {
public:
	virtual bool useProtect() const { return false; }
};

class X64Emitter : public Xbyak::CodeGenerator {
public:
  X64Emitter(X64Backend* backend, XbyakAllocator* allocator);
  virtual ~X64Emitter();

  int Initialize();

  int Emit(hir::HIRBuilder* builder,
           void*& out_code_address, size_t& out_code_size);

private:
  void* Emplace(X64CodeCache* code_cache);
  int Emit(hir::HIRBuilder* builder);

private:
  X64Backend*     backend_;
  X64CodeCache*   code_cache_;
  XbyakAllocator* allocator_;
};


}  // namespace x64
}  // namespace backend
}  // namespace alloy


#endif  // ALLOY_BACKEND_X64_X64_EMITTER_H_
