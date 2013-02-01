/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_FS_GDFX_H_
#define XENIA_KERNEL_FS_GDFX_H_

#include <xenia/common.h>
#include <xenia/core.h>

#include <vector>

#include <xenia/kernel/fs/entry.h>


namespace xe {
namespace kernel {
namespace fs {


class GDFX;


class GDFXEntry {
public:
  enum Attributes {
    kAttrNone       = 0x00000000,
    kAttrReadOnly   = 0x00000001,
    kAttrHidden     = 0x00000002,
    kAttrSystem     = 0x00000004,
    kAttrFolder     = 0x00000010,
    kAttrArchived   = 0x00000020,
    kAttrNormal     = 0x00000080,
  };

  GDFXEntry() {}
  ~GDFXEntry();

  GDFXEntry* GetChild(const char* name);

  void Dump(int indent);

  std::string   name;
  uint32_t      attributes;
  size_t        offset;
  size_t        size;

  std::vector<GDFXEntry*> children;
};


class GDFX {
public:
  enum Error {
    kSuccess            = 0,
    kErrorOutOfMemory   = -1,
    kErrorReadError     = -10,
    kErrorFileMismatch  = -30,
    kErrorDamagedFile   = -31,
  };

  GDFX(xe_mmap_ref mmap);
  virtual ~GDFX();

  GDFXEntry* root_entry();

  Error Load();
  void Dump();

private:
  typedef struct {
    uint8_t*  ptr;

    size_t    size;           // Size (bytes) of total image

    size_t    game_offset;    // Offset (bytes) of game partition

    size_t    root_sector;    // Offset (sector) of root
    size_t    root_offset;    // Offset (bytes) of root
    size_t    root_size;      // Size (bytes) of root
  } ParseState;

  Error Verify(ParseState& state);
  bool VerifyMagic(ParseState& state, size_t offset);
  Error ReadAllEntries(ParseState& state, const uint8_t* root_buffer);
  bool ReadEntry(ParseState& state, const uint8_t* buffer,
                 uint16_t entry_ordinal, GDFXEntry* parent);

  xe_mmap_ref mmap_;

  GDFXEntry*  root_entry_;
};


}  // namespace fs
}  // namespace kernel
}  // namespace xe


#endif  // XENIA_KERNEL_FS_GDFX_H_
