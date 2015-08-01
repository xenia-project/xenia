/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/stack_walker.h"

#include <gflags/gflags.h>

#include <mutex>

#include "xenia/base/logging.h"
#include "xenia/base/platform_win.h"
#include "xenia/cpu/backend/backend.h"
#include "xenia/cpu/backend/code_cache.h"
#include "xenia/cpu/processor.h"

DEFINE_bool(debug_symbol_loader, false,
            "Enable dbghelp debug logging and validation.");

// Must be included after platform_win.h:
#pragma warning(push)
#pragma warning(disable : 4091)
#include <dbghelp.h>
#pragma warning(pop)

typedef DWORD(__stdcall* LPSYMGETOPTIONS)(VOID);
typedef DWORD(__stdcall* LPSYMSETOPTIONS)(IN DWORD SymOptions);
typedef BOOL(__stdcall* LPSYMINITIALIZE)(IN HANDLE hProcess,
                                         IN PSTR UserSearchPath,
                                         IN BOOL fInvadeProcess);
typedef BOOL(__stdcall* LPSTACKWALK64)(
    DWORD MachineType, HANDLE hProcess, HANDLE hThread,
    LPSTACKFRAME64 StackFrame, PVOID ContextRecord,
    PREAD_PROCESS_MEMORY_ROUTINE64 ReadMemoryRoutine,
    PFUNCTION_TABLE_ACCESS_ROUTINE64 FunctionTableAccessRoutine,
    PGET_MODULE_BASE_ROUTINE64 GetModuleBaseRoutine,
    PTRANSLATE_ADDRESS_ROUTINE64 TranslateAddress);
typedef PVOID(__stdcall* LPSYMFUNCTIONTABLEACCESS64)(
    HANDLE hProcess,
    DWORD64 AddrBase);  // DbgHelp.h typedef PFUNCTION_TABLE_ACCESS_ROUTINE64
typedef DWORD64(__stdcall* LPSYMGETMODULEBASE64)(
    HANDLE hProcess,
    DWORD64 AddrBase);  // DbgHelp.h typedef PGET_MODULE_BASE_ROUTINE64
typedef BOOL(__stdcall* LPSYMGETSYMFROMADDR64)(IN HANDLE hProcess,
                                               IN DWORD64 qwAddr,
                                               OUT PDWORD64 pdwDisplacement,
                                               OUT PIMAGEHLP_SYMBOL64 Symbol);

LPSYMGETOPTIONS sym_get_options_ = nullptr;
LPSYMSETOPTIONS sym_set_options_ = nullptr;
LPSYMINITIALIZE sym_initialize_ = nullptr;
LPSTACKWALK64 stack_walk_64_ = nullptr;
LPSYMFUNCTIONTABLEACCESS64 sym_function_table_access_64_ = nullptr;
LPSYMGETMODULEBASE64 sym_get_module_base_64_ = nullptr;
LPSYMGETSYMFROMADDR64 sym_get_sym_from_addr_64_ = nullptr;

namespace xe {
namespace cpu {

bool InitializeStackWalker() {
  if (sym_get_options_) {
    // Already initialized.
    return true;
  }

  // Attempt to load dbghelp.
  // NOTE: we never free it. That's fine.
  HMODULE module = LoadLibrary(TEXT("dbghelp.dll"));
  if (!module) {
    XELOGE("Unable to load dbghelp.dll - not found on path or invalid");
    return false;
  }
  sym_get_options_ = reinterpret_cast<LPSYMGETOPTIONS>(
      GetProcAddress(module, "SymGetOptions"));
  sym_set_options_ = reinterpret_cast<LPSYMSETOPTIONS>(
      GetProcAddress(module, "SymSetOptions"));
  sym_initialize_ = reinterpret_cast<LPSYMINITIALIZE>(
      GetProcAddress(module, "SymInitialize"));
  stack_walk_64_ =
      reinterpret_cast<LPSTACKWALK64>(GetProcAddress(module, "StackWalk64"));
  sym_function_table_access_64_ = reinterpret_cast<LPSYMFUNCTIONTABLEACCESS64>(
      GetProcAddress(module, "SymFunctionTableAccess64"));
  sym_get_module_base_64_ = reinterpret_cast<LPSYMGETMODULEBASE64>(
      GetProcAddress(module, "SymGetModuleBase64"));
  sym_get_sym_from_addr_64_ = reinterpret_cast<LPSYMGETSYMFROMADDR64>(
      GetProcAddress(module, "SymGetSymFromAddr64"));
  if (!sym_get_options_ || !sym_set_options_ || !sym_initialize_ ||
      !stack_walk_64_ || !sym_function_table_access_64_ ||
      !sym_get_module_base_64_ || !sym_get_sym_from_addr_64_) {
    XELOGE("Unable to get one or more symbols from dbghelp.dll");
    return false;
  }

  // Initialize the symbol lookup services.
  DWORD options = sym_get_options_();
  if (FLAGS_debug_symbol_loader) {
    options |= SYMOPT_DEBUG;
  }
  options |= SYMOPT_DEFERRED_LOADS;
  options |= SYMOPT_LOAD_LINES;
  options |= SYMOPT_FAIL_CRITICAL_ERRORS;
  sym_set_options_(options);
  if (!sym_initialize_(GetCurrentProcess(), nullptr, TRUE)) {
    XELOGE("Unable to initialize symbol services");
    return false;
  }

  return true;
}

class Win32StackWalker : public StackWalker {
 public:
  Win32StackWalker(backend::CodeCache* code_cache) {
    // Get the boundaries of the code cache so we can quickly tell if a symbol
    // is ours or not.
    // We store these globally so that the Sym* callbacks can access them.
    // They never change, so it's fine even if they are touched from multiple
    // threads.
    code_cache_ = code_cache;
    code_cache_min_ = code_cache_->base_address();
    code_cache_max_ = code_cache_->base_address() + code_cache_->total_size();
  }

  bool Initialize() {
    std::lock_guard<std::mutex> lock(dbghelp_mutex_);
    return InitializeStackWalker();
  }

  size_t CaptureStackTrace(uint64_t* frame_host_pcs, size_t frame_offset,
                           size_t frame_count,
                           uint64_t* out_stack_hash) override {
    *out_stack_hash = 0;
    // Simple method: captures just stack frame PC addresses, optionally
    // computing a whole-stack hash.
    ULONG back_trace_hash = 0;
    DWORD frames_to_skip = DWORD(frame_offset) + 1;
    DWORD frames_to_capture =
        std::min(DWORD(frame_count), UINT16_MAX - frames_to_skip);
    USHORT captured_count = CaptureStackBackTrace(
        frames_to_skip, frames_to_capture,
        reinterpret_cast<PVOID*>(frame_host_pcs), &back_trace_hash);
    if (out_stack_hash) {
      *out_stack_hash = back_trace_hash;
    }
    return captured_count;
  }

  size_t CaptureStackTrace(void* thread_handle, uint64_t* frame_host_pcs,
                           size_t frame_offset, size_t frame_count,
                           uint64_t* out_stack_hash) override {
    // Query context. Thread must be suspended.
    // Need at least CONTEXT_CONTROL (for rip and rsp) and CONTEXT_INTEGER (for
    // rbp).
    CONTEXT thread_context;
    thread_context.ContextFlags = CONTEXT_FULL;
    if (!GetThreadContext(thread_handle, &thread_context)) {
      XELOGE("Unable to read thread context for stack walk");
      return 0;
    }

    // Setup the frame for walking.
    STACKFRAME64 stack_frame = {0};
    stack_frame.AddrPC.Mode = AddrModeFlat;
    stack_frame.AddrPC.Offset = thread_context.Rip;
    stack_frame.AddrFrame.Mode = AddrModeFlat;
    stack_frame.AddrFrame.Offset = thread_context.Rbp;
    stack_frame.AddrStack.Mode = AddrModeFlat;
    stack_frame.AddrStack.Offset = thread_context.Rsp;

    // Walk the stack.
    // Note that StackWalk64 is thread safe, though other dbghelp functions are
    // not.
    size_t frame_index = 0;
    while (frame_index < frame_count &&
           stack_walk_64_(IMAGE_FILE_MACHINE_AMD64, GetCurrentProcess(),
                          thread_handle, &stack_frame, &thread_context, nullptr,
                          XSymFunctionTableAccess64, XSymGetModuleBase64,
                          nullptr) == TRUE) {
      if (frame_index >= frame_offset) {
        frame_host_pcs[frame_index - frame_offset] = stack_frame.AddrPC.Offset;
      }
      ++frame_index;
    }

    return frame_index - frame_offset;
  }

  bool ResolveStack(uint64_t* frame_host_pcs, StackFrame* frames,
                    size_t frame_count) override {
    // TODO(benvanik): collect symbols to resolve with dbghelp and resolve
    // afterward in a smaller lock.
    std::lock_guard<std::mutex> lock(dbghelp_mutex_);

    for (size_t i = 0; i < frame_count; ++i) {
      auto& frame = frames[i];
      frame.host_pc = frame_host_pcs[i];
      frame.host_symbol.name[0] = 0;
      frame.guest_pc = 0;
      frame.guest_symbol.function_info = nullptr;

      // If in the generated range, we know it's ours.
      if (frame.host_pc >= code_cache_min_ && frame.host_pc < code_cache_max_) {
        // Guest symbol, so we can look it up quickly in the code cache.
        frame.type = StackFrame::Type::kGuest;
        auto function_info = code_cache_->LookupFunction(frame.host_pc);
        if (function_info) {
          frame.guest_symbol.function_info = function_info;
          // Figure out where in guest code we are by looking up the
          // displacement in x64 from the JIT'ed code start to the PC.
          uint32_t host_displacement =
              uint32_t(frame.host_pc) -
              uint32_t(uint64_t(function_info->function()->machine_code()));
          auto entry =
              function_info->function()->LookupCodeOffset(host_displacement);
          frame.guest_pc = entry->source_offset;
        } else {
          frame.guest_symbol.function_info = nullptr;
        }
      } else {
        // Host symbol, which means either emulator or system.
        frame.type = StackFrame::Type::kHost;
        // TODO(benvanik): cache so that we can avoid calling into dbghelp (and
        // taking the lock).
        union {
          IMAGEHLP_SYMBOL64 info;
          uint8_t buffer[sizeof(IMAGEHLP_SYMBOL64) +
                         MAX_SYM_NAME * sizeof(CHAR) + sizeof(ULONG64) - 1];
        } symbol;
        symbol.info.SizeOfStruct = sizeof(IMAGEHLP_SYMBOL64);
        symbol.info.MaxNameLength = MAX_SYM_NAME;
        uint64_t displacement = 0;
        if (sym_get_sym_from_addr_64_(GetCurrentProcess(), frame.host_pc,
                                      &displacement, &symbol.info)) {
          // Resolved successfully.
          // TODO(benvanik): stash: module, base, displacement, name?
          std::strncpy(frame.host_symbol.name, symbol.info.Name, 256);
        }
      }
    }
    return true;
  }

 private:
  static PVOID WINAPI XSymFunctionTableAccess64(__in HANDLE hProcess,
                                                __in DWORD64 AddrBase) {
    if (AddrBase >= code_cache_min_ && AddrBase < code_cache_max_) {
      // Within our generated range so ask code cache.
      return code_cache_->LookupUnwindInfo(AddrBase);
    }
    // Normal symbol lookup.
    return sym_function_table_access_64_(hProcess, AddrBase);
  }

  static DWORD64 WINAPI XSymGetModuleBase64(_In_ HANDLE hProcess,
                                            _In_ DWORD64 dwAddr) {
    if (dwAddr >= code_cache_min_ && dwAddr < code_cache_max_) {
      // In our generated range all addresses are relative to the code cache
      // base.
      return code_cache_min_;
    }
    // Normal module base lookup.
    return sym_get_module_base_64_(hProcess, dwAddr);
  }

  std::mutex dbghelp_mutex_;

  static xe::cpu::backend::CodeCache* code_cache_;
  static uint32_t code_cache_min_;
  static uint32_t code_cache_max_;
};

xe::cpu::backend::CodeCache* Win32StackWalker::code_cache_ = nullptr;
uint32_t Win32StackWalker::code_cache_min_ = 0;
uint32_t Win32StackWalker::code_cache_max_ = 0;

std::unique_ptr<StackWalker> StackWalker::Create(
    backend::CodeCache* code_cache) {
  auto stack_walker = std::make_unique<Win32StackWalker>(code_cache);
  if (!stack_walker->Initialize()) {
    XELOGE("Unable to initialize stack walker");
    return nullptr;
  }
  return std::unique_ptr<StackWalker>(stack_walker.release());
}

}  // namespace cpu
}  // namespace xe
