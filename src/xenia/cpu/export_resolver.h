/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_EXPORT_RESOLVER_H_
#define XENIA_CPU_EXPORT_RESOLVER_H_

#include <string>
#include <vector>

#include "xenia/cpu/frontend/ppc_context.h"

namespace xe {
namespace cpu {

struct ExportTag {
  typedef uint32_t type;

  static const type kImplemented = 1 << 0;
  static const type kSketchy = 1 << 1;
  static const type kHighFrequency = 1 << 2;
  static const type kImportant = 1 << 3;

  static const type kThreading = 1 << 10;
  static const type kInput = 1 << 11;
  static const type kAudio = 1 << 12;
  static const type kVideo = 1 << 13;
  static const type kFileSystem = 1 << 14;
  static const type kModules = 1 << 15;
  static const type kUserProfiles = 1 << 16;

  static const type kLog = 1 << 31;
};

// DEPRECATED
typedef void (*xe_kernel_export_shim_fn)(void*, void*);

class Export {
 public:
  enum class Type {
    kFunction = 0,
    kVariable = 1,
  };

  uint32_t ordinal;
  Type type;
  std::string name;
  ExportTag::type tags;

  void (*trampoline)(xe::cpu::frontend::PPCContext* ppc_context);
  uint64_t call_count;

  bool is_implemented() const {
    return (tags & ExportTag::kImplemented) == ExportTag::kImplemented;
  }

  union {
    // Variable data. Only valid when kXEKernelExportFlagVariable is set.
    // This is an address in the client memory space that the variable can
    // be found at.
    uint32_t variable_ptr;

    struct {
      // Shimmed implementation.
      // This is called directly from generated code.
      // It should parse args, do fixups, and call the impl.
      xe_kernel_export_shim_fn shim;
    } function_data;
  };
};

class ExportResolver {
 public:
  ExportResolver();
  ~ExportResolver();

  void RegisterTable(const std::string& library_name, Export* exports,
                     const size_t count);

  Export* GetExportByOrdinal(const std::string& library_name,
                             const uint32_t ordinal);

  void SetVariableMapping(const std::string& library_name,
                          const uint32_t ordinal, uint32_t value);
  void SetFunctionMapping(const std::string& library_name,
                          const uint32_t ordinal,
                          xe_kernel_export_shim_fn shim);

 private:
  struct ExportTable {
    std::string name;
    std::string simple_name;  // without extension
    Export* exports;
    size_t count;
    ExportTable(const std::string& name, Export* exports, size_t count)
        : name(name), exports(exports), count(count) {
      auto dot_pos = name.find_last_of('.');
      if (dot_pos != std::string::npos) {
        simple_name = name.substr(0, dot_pos);
      } else {
        simple_name = name;
      }
    }
  };
  std::vector<ExportTable> tables_;
};

}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_EXPORT_RESOLVER_H_
