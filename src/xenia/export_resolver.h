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

#include <xenia/core.h>

#include <vector>


typedef struct xe_ppc_state xe_ppc_state_t;


namespace xe {


typedef void (*xe_kernel_export_impl_fn)();
typedef void (*xe_kernel_export_shim_fn)(xe_ppc_state_t*, void*);

class KernelExport {
public:
  enum ExportType {
    Function = 0,
    Variable = 1,
  };

  uint32_t      ordinal;
  ExportType    type;
  uint32_t      flags;
  char          name[96];

  bool          is_implemented;

  union {
    // Variable data. Only valid when kXEKernelExportFlagVariable is set.
    // This is an address in the client memory space that the variable can
    // be found at.
    uint32_t    variable_ptr;

    struct {
      // Second argument passed to the shim function.
      void* shim_data;

      // Shimmed implementation.
      // This is called directly from generated code.
      // It should parse args, do fixups, and call the impl.
      xe_kernel_export_shim_fn shim;

      // Real function implementation.
      xe_kernel_export_impl_fn impl;
    } function_data;
  };
};


class ExportResolver {
public:
  ExportResolver();
  ~ExportResolver();

  void RegisterTable(const char* library_name, KernelExport* exports,
                     const size_t count);

  uint16_t GetLibraryOrdinal(const char* library_name);

  KernelExport* GetExportByOrdinal(const uint16_t library_ordinal,
                                   const uint32_t ordinal);
  KernelExport* GetExportByOrdinal(const char* library_name,
                                   const uint32_t ordinal);
  KernelExport* GetExportByName(const char* library_name, const char* name);

  void SetVariableMapping(const char* library_name, const uint32_t ordinal,
                          uint32_t value);
  void SetFunctionMapping(const char* library_name, const uint32_t ordinal,
                          void* shim_data, xe_kernel_export_shim_fn shim,
                          xe_kernel_export_impl_fn impl);

private:
  class ExportTable {
  public:
    char          name[32];
    KernelExport* exports;
    size_t        count;
  };

  std::vector<ExportTable> tables_;
};


}  // namespace xe


#endif  // XENIA_EXPORT_RESOLVER_H_
