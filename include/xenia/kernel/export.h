/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_EXPORT_H_
#define XENIA_KERNEL_EXPORT_H_

#include <xenia/core.h>

#include <vector>


namespace xe {
namespace kernel {


typedef void (*xe_kernel_export_fn)();

class KernelExport {
public:
  enum ExportType {
    Function = 0,
    Variable = 1,
  };

  uint32_t      ordinal;
  ExportType    type;
  uint32_t      flags;
  char          signature[16];
  char          name[96];

  union {
    // Variable data. Only valid when kXEKernelExportFlagVariable is set.
    void          *variable_data;

    struct {
      // Real function implementation (if present).
      xe_kernel_export_fn impl;

      // Shimmed implementation (call if param structs are big endian).
      // This may be NULL if no shim is needed or present.
      xe_kernel_export_fn shim;
    } function_data;
  };

  bool IsImplemented();
};

#define XE_DECLARE_EXPORT(module, ordinal, name, signature, type, flags) \
  { \
    ordinal, \
    KernelExport::type, \
    flags, \
    #signature, \
    #name, \
  }


class ExportResolver {
public:
  ExportResolver();
  ~ExportResolver();

  void RegisterTable(const char* library_name, KernelExport* exports,
                     const size_t count);

  KernelExport* GetExportByOrdinal(const char* library_name,
                                   const uint32_t ordinal);
  KernelExport* GetExportByName(const char* library_name, const char* name);

private:
  class ExportTable {
  public:
    char          name[32];
    KernelExport* exports;
    size_t        count;
  };

  std::vector<ExportTable> tables_;
};


}  // namespace kernel
}  // namespace xe


#endif  // XENIA_KERNEL_EXPORT_H_
