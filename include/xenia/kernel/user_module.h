/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_USER_MODULE_H_
#define XENIA_KERNEL_USER_MODULE_H_

#include <xenia/core.h>

#include <vector>

#include <xenia/kernel/export.h>
#include <xenia/kernel/xex2.h>


namespace xe {
namespace kernel {


#define kXEPESectionContainsCode          0x00000020
#define kXEPESectionContainsDataInit      0x00000040
#define kXEPESectionContainsDataUninit    0x00000080
#define kXEPESectionMemoryExecute         0x20000000
#define kXEPESectionMemoryRead            0x40000000
#define kXEPESectionMemoryWrite           0x80000000

class PESection {
public:
  char        name[9];        // 8 + 1 for \0
  uint32_t    raw_address;
  size_t      raw_size;
  uint32_t    address;
  size_t      size;
  uint32_t    flags;          // kXEPESection*
};

class PEMethodInfo {
public:
  uint32_t    address;
  size_t      total_length;   // in bytes
  size_t      prolog_length;  // in bytes
};


class UserModule {
public:
  UserModule(xe_memory_ref memory);
  ~UserModule();

  int Load(const void* addr, const size_t length, const xechar_t* path);

  const xechar_t* path();
  const xechar_t* name();
  uint32_t handle();
  xe_xex2_ref xex();
  const xe_xex2_header_t* xex_header();

  void* GetProcAddress(const uint32_t ordinal);
  PESection* GetSection(const char* name);
  int GetMethodHints(PEMethodInfo** out_method_infos,
                     size_t* out_method_info_count);

  void Dump(ExportResolver* export_resolver);

private:
  int LoadPE();

  xe_memory_ref   memory_;

  xechar_t        path_[2048];
  xechar_t        name_[256];

  uint32_t        handle_;
  xe_xex2_ref     xex_;

  std::vector<PESection*> sections_;
};


}  // namespace kernel
}  // namespace xe


#endif  // XENIA_KERNEL_USER_MODULE_H_
