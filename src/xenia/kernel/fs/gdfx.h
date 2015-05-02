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

#include <vector>

#include "xenia/base/fs.h"
#include "xenia/base/mapped_memory.h"
#include "xenia/kernel/fs/entry.h"
#include "xenia/xbox.h"

namespace xe {
namespace kernel {
namespace fs {

class GDFX;

class GDFXEntry {
 public:
  GDFXEntry();
  ~GDFXEntry();

  typedef std::vector<GDFXEntry*> child_t;
  typedef child_t::iterator child_it_t;

  GDFXEntry* GetChild(const xe::fs::WildcardEngine& engine, child_it_t& ref_it);
  GDFXEntry* GetChild(const char* name);

  void Dump(int indent);

  std::string name;
  X_FILE_ATTRIBUTES attributes;
  size_t offset;
  size_t size;
  child_t children;
};

class GDFX {
 public:
  enum Error {
    kSuccess = 0,
    kErrorOutOfMemory = -1,
    kErrorReadError = -10,
    kErrorFileMismatch = -30,
    kErrorDamagedFile = -31,
  };

  GDFX(MappedMemory* mmap);
  virtual ~GDFX();

  GDFXEntry* root_entry();

  Error Load();
  void Dump();

 private:
  typedef struct {
    uint8_t* ptr;

    // Size (bytes) of total image.
    size_t size;

    // Offset (bytes) of game partition.
    size_t game_offset;

    // Offset (sector) of root.
    size_t root_sector;
    // Offset (bytes) of root.
    size_t root_offset;
    // Size (bytes) of root.
    size_t root_size;
  } ParseState;

  Error Verify(ParseState& state);
  bool VerifyMagic(ParseState& state, size_t offset);
  Error ReadAllEntries(ParseState& state, const uint8_t* root_buffer);
  bool ReadEntry(ParseState& state, const uint8_t* buffer,
                 uint16_t entry_ordinal, GDFXEntry* parent);

  MappedMemory* mmap_;

  GDFXEntry* root_entry_;
};

}  // namespace fs
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_FS_GDFX_H_
