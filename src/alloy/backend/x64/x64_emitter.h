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


namespace alloy {
namespace backend {
namespace x64 {

class X64Backend;
class X64CodeCache;
namespace lir { class LIRBuilder; }

class XbyakAllocator;
class XbyakGenerator;

class X64Emitter {
public:
  X64Emitter(X64Backend* backend);
  ~X64Emitter();

  int Initialize();

  int Emit(lir::LIRBuilder* builder,
           void*& out_code_address, size_t& out_code_size);

private:
  X64Backend*     backend_;
  X64CodeCache*   code_cache_;
  XbyakAllocator* allocator_;
  XbyakGenerator* generator_;
};


}  // namespace x64
}  // namespace backend
}  // namespace alloy


#endif  // ALLOY_BACKEND_X64_X64_EMITTER_H_
