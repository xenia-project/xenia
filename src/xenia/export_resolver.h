/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_EXPORT_RESOLVER_H_
#define XENIA_EXPORT_RESOLVER_H_


#include <string>
#include <vector>

namespace xe {

typedef void (*xe_kernel_export_shim_fn)(void*, void*);

class KernelExport {
 public:
  enum ExportType {
    Function = 0,
    Variable = 1,
  };

  uint32_t ordinal;
  ExportType type;
  uint32_t flags;
  char name[96];

  bool is_implemented;

  union {
    // Variable data. Only valid when kXEKernelExportFlagVariable is set.
    // This is an address in the client memory space that the variable can
    // be found at.
    uint32_t variable_ptr;

    struct {
      // Second argument passed to the shim function.
      void* shim_data;

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

  void RegisterTable(const std::string& library_name, KernelExport* exports,
                     const size_t count);

  KernelExport* GetExportByOrdinal(const std::string& library_name,
                                   const uint32_t ordinal);

  void SetVariableMapping(const std::string& library_name,
                          const uint32_t ordinal, uint32_t value);
  void SetFunctionMapping(const std::string& library_name,
                          const uint32_t ordinal, void* shim_data,
                          xe_kernel_export_shim_fn shim);

 private:
  struct ExportTable {
    std::string name;
    KernelExport* exports;
    size_t count;
    ExportTable(const std::string& name, KernelExport* exports, size_t count)
        : name(name), exports(exports), count(count) {}
  };
  std::vector<ExportTable> tables_;
};

}  // namespace xe

#endif  // XENIA_EXPORT_RESOLVER_H_
