/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_FUNCTION_H_
#define XENIA_CPU_FUNCTION_H_

#include <memory>
#include <vector>

#include "xenia/cpu/function_debug_info.h"
#include "xenia/cpu/function_trace_data.h"
#include "xenia/cpu/ppc/ppc_context.h"
#include "xenia/cpu/symbol.h"
#include "xenia/cpu/thread_state.h"

namespace xe {
namespace cpu {
class Export;
}  // namespace cpu

namespace cpu {

struct SourceMapEntry {
  uint32_t guest_address;  // PPC guest address (0x82....).
  uint32_t hir_offset;     // Block ordinal (16b) | Instr ordinal (16b)
  uint32_t code_offset;    // Offset from emitted code start.
};
enum class SaveRestoreType : uint8_t { NONE, GPR, VMX, FPR };

class Function : public Symbol {
 public:
  enum class Behavior : uint8_t {
    kDefault = 0,
    kProlog,
    kEpilog,
    kEpilogReturn,
    kBuiltin,
    kExtern,
  };

  ~Function() override;

  uint32_t address() const { return address_; }
  bool has_end_address() const { return end_address_ > 0; }
  uint32_t end_address() const { return end_address_; }
  void set_end_address(uint32_t value) { end_address_ = value; }
  Behavior behavior() const { return behavior_; }
  void set_behavior(Behavior value) { behavior_ = value; }
  bool is_guest() const { return behavior_ != Behavior::kBuiltin; }

  void SetSaverest(SaveRestoreType type, bool is_rest, uint8_t index) {
    saverest_type_ = type;
    is_restore_ = is_rest;
    saverest_index_ = index;
  }

  bool IsSaverest() const { return saverest_type_ != SaveRestoreType::NONE; }

  SaveRestoreType SaverestType() const { return saverest_type_; }
  unsigned SaverestIndex() const { return saverest_index_; }

  bool IsSave() const { return IsSaverest() && is_restore_ == 0; }
  bool IsRestore() const { return IsSaverest() && is_restore_; }

  bool ContainsAddress(uint32_t address) const {
    if (!address_ || !end_address_) {
      return false;
    }

    if (address >= address_ && address < end_address_) {
      return true;
    }

    return false;
  }

  virtual bool Call(ThreadState* thread_state, uint32_t return_address) = 0;

 protected:
  Function(Module* module, uint32_t address);

  uint32_t end_address_ = 0;

  Behavior behavior_ = Behavior::kDefault;
  SaveRestoreType saverest_type_ = SaveRestoreType::NONE;
  uint8_t is_restore_ = 0;
  uint8_t saverest_index_ = 0;
};

class BuiltinFunction : public Function {
 public:
  typedef void (*Handler)(ppc::PPCContext* ppc_context, void* arg0, void* arg1);

  BuiltinFunction(Module* module, uint32_t address);
  ~BuiltinFunction() override;

  void SetupBuiltin(Handler handler, void* arg0, void* arg1);

  Handler handler() const { return handler_; }
  void* arg0() const { return arg0_; }
  void* arg1() const { return arg1_; }

  bool Call(ThreadState* thread_state, uint32_t return_address) override;

 protected:
  Handler handler_ = nullptr;
  void* arg0_ = nullptr;
  void* arg1_ = nullptr;
};

class GuestFunction : public Function {
 public:
  typedef void (*ExternHandler)(ppc::PPCContext* ppc_context,
                                kernel::KernelState* kernel_state);

  GuestFunction(Module* module, uint32_t address);
  ~GuestFunction() override;

  uint32_t address() const { return address_; }
  bool has_end_address() const { return end_address_ > 0; }
  uint32_t end_address() const { return end_address_; }
  void set_end_address(uint32_t value) { end_address_ = value; }

  virtual uint8_t* machine_code() const = 0;
  virtual size_t machine_code_length() const = 0;

  FunctionDebugInfo* debug_info() const { return debug_info_.get(); }
  void set_debug_info(std::unique_ptr<FunctionDebugInfo> debug_info) {
    debug_info_ = std::move(debug_info);
  }
  FunctionTraceData& trace_data() { return trace_data_; }
  std::vector<SourceMapEntry>& source_map() { return source_map_; }

  ExternHandler extern_handler() const { return extern_handler_; }
  Export* export_data() const { return export_data_; }
  void SetupExtern(ExternHandler handler, Export* export_data = nullptr);

  const SourceMapEntry* LookupGuestAddress(uint32_t guest_address) const;
  const SourceMapEntry* LookupHIROffset(uint32_t offset) const;
  const SourceMapEntry* LookupMachineCodeOffset(uint32_t offset) const;

  uint32_t MapGuestAddressToMachineCodeOffset(uint32_t guest_address) const;
  uintptr_t MapGuestAddressToMachineCode(uint32_t guest_address) const;
  uint32_t MapMachineCodeToGuestAddress(uintptr_t host_address) const;

  bool Call(ThreadState* thread_state, uint32_t return_address) override;

 protected:
  virtual bool CallImpl(ThreadState* thread_state, uint32_t return_address) = 0;

 protected:
  std::unique_ptr<FunctionDebugInfo> debug_info_;
  FunctionTraceData trace_data_;
  std::vector<SourceMapEntry> source_map_;
  ExternHandler extern_handler_ = nullptr;
  Export* export_data_ = nullptr;
};

}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_FUNCTION_H_
