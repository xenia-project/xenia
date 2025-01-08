/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/xex_module.h"

#include <algorithm>

#include "third_party/fmt/include/fmt/format.h"

#include "xenia/base/byte_order.h"
#include "xenia/base/cvar.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/base/memory.h"

#include "xenia/cpu/cpu_flags.h"
#include "xenia/cpu/export_resolver.h"
#include "xenia/cpu/lzx.h"
#include "xenia/cpu/processor.h"
#include "xenia/emulator.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/xmodule.h"

#include "third_party/crypto/TinySHA1.hpp"
#include "third_party/crypto/rijndael-alg-fst.c"
#include "third_party/crypto/rijndael-alg-fst.h"
#include "third_party/pe/pe_image.h"
#include "xenia/cpu/ppc/ppc_decode_data.h"
#include "xenia/cpu/ppc/ppc_instr.h"
DEFINE_bool(disable_instruction_infocache, false,
            "Disables caching records of called instructions/mmio accesses.",
            "CPU");

DEFINE_bool(writable_code_segments, false,
            "Enables a program to write to its own code segments in memory.",
            "CPU");

DEFINE_bool(
    enable_early_precompilation, false,
    "Enable pre-compiling guest functions that we know we've called/that "
    "we've recognized as being functions via simple heuristics, good for error "
    "finding/stress testing with the JIT",
    "CPU");

DECLARE_bool(allow_plugins);

static const uint8_t xe_xex2_retail_key[16] = {
    0x20, 0xB1, 0x85, 0xA5, 0x9D, 0x28, 0xFD, 0xC3,
    0x40, 0x58, 0x3F, 0xBB, 0x08, 0x96, 0xBF, 0x91};
static const uint8_t xe_xex2_devkit_key[16] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

void aes_decrypt_buffer(const uint8_t* session_key, const uint8_t* input_buffer,
                        const size_t input_size, uint8_t* output_buffer,
                        const size_t output_size) {
  uint32_t rk[4 * (MAXNR + 1)];
  uint8_t ivec[16] = {0};
  int32_t Nr = rijndaelKeySetupDec(rk, session_key, 128);
  const uint8_t* ct = input_buffer;
  uint8_t* pt = output_buffer;
  for (size_t n = 0; n < input_size; n += 16, ct += 16, pt += 16) {
    // Decrypt 16 uint8_ts from input -> output.
    rijndaelDecrypt(rk, Nr, ct, pt);
    for (size_t i = 0; i < 16; i++) {
      // XOR with previous.
      pt[i] ^= ivec[i];
      // Set previous.
      ivec[i] = ct[i];
    }
  }
}

namespace xe {
namespace cpu {

using xe::kernel::KernelState;

XexModule::XexModule(Processor* processor, KernelState* kernel_state)
    : Module(processor), processor_(processor), kernel_state_(kernel_state) {}

XexModule::~XexModule() {}

bool XexModule::GetOptHeader(const xex2_header* header, xex2_header_keys key,
                             void** out_ptr) {
  assert_not_null(header);
  assert_not_null(out_ptr);

  for (uint32_t i = 0; i < header->header_count; i++) {
    const xex2_opt_header& opt_header = header->headers[i];
    if (opt_header.key == key) {
      // Match!
      switch (key & 0xFF) {
        case 0x00: {
          // We just return the value of the optional header.
          // Assume that the output pointer points to a uint32_t.
          *reinterpret_cast<uint32_t*>(out_ptr) =
              static_cast<uint32_t>(opt_header.value);
        } break;
        case 0x01: {
          // Pointer to the value on the optional header.
          *out_ptr = const_cast<void*>(
              reinterpret_cast<const void*>(&opt_header.value));
        } break;
        default: {
          // Pointer to the header.
          *out_ptr =
              reinterpret_cast<void*>(uintptr_t(header) + opt_header.offset);
        } break;
      }

      return true;
    }
  }

  return false;
}

bool XexModule::GetOptHeader(xex2_header_keys key, void** out_ptr) const {
  return XexModule::GetOptHeader(xex_header(), key, out_ptr);
}

const void* XexModule::GetSecurityInfo(const xex2_header* header) {
  return reinterpret_cast<const void*>(uintptr_t(header) +
                                       header->security_offset);
}

const PESection* XexModule::GetPESection(const char* name) {
  for (std::vector<PESection>::iterator it = pe_sections_.begin();
       it != pe_sections_.end(); ++it) {
    if (!strcmp(it->name, name)) {
      return &(*it);
    }
  }
  return nullptr;
}

uint32_t XexModule::GetProcAddress(uint16_t ordinal) const {
  // First: Check the xex2 export table.
  if (xex_security_info()->export_table) {
    auto export_table = memory()->TranslateVirtual<const xex2_export_table*>(
        xex_security_info()->export_table);

    ordinal -= export_table->base;
    if (ordinal >= export_table->count) {
      XELOGE("GetProcAddress({:03X}): ordinal out of bounds", ordinal);
      return 0;
    }

    uint32_t num = ordinal;
    uint32_t ordinal_offset = export_table->ordOffset[num];
    ordinal_offset += export_table->imagebaseaddr << 16;
    return ordinal_offset;
  }

  // Second: Check the PE exports.
  assert_not_zero(base_address_);

  xex2_opt_data_directory* pe_export_directory = 0;
  if (GetOptHeader(XEX_HEADER_EXPORTS_BY_NAME, &pe_export_directory)) {
    auto e = memory()->TranslateVirtual<const X_IMAGE_EXPORT_DIRECTORY*>(
        base_address_ + pe_export_directory->offset);
    assert_not_null(e);

    uint32_t* function_table =
        reinterpret_cast<uint32_t*>(uintptr_t(e) + e->AddressOfFunctions);

    if (ordinal < e->NumberOfFunctions) {
      return base_address_ + function_table[ordinal];
    }
  }

  return 0;
}

uint32_t XexModule::GetProcAddress(const std::string_view name) const {
  assert_not_zero(base_address_);

  xex2_opt_data_directory* pe_export_directory = 0;
  if (!GetOptHeader(XEX_HEADER_EXPORTS_BY_NAME, &pe_export_directory)) {
    // No exports by name.
    return 0;
  }

  auto e = memory()->TranslateVirtual<const X_IMAGE_EXPORT_DIRECTORY*>(
      base_address_ + pe_export_directory->offset);
  assert_not_null(e);

  // e->AddressOfX RVAs are relative to the IMAGE_EXPORT_DIRECTORY!
  uint32_t* function_table =
      reinterpret_cast<uint32_t*>(uintptr_t(e) + e->AddressOfFunctions);

  // Names relative to directory
  uint32_t* name_table =
      reinterpret_cast<uint32_t*>(uintptr_t(e) + e->AddressOfNames);

  // Table of ordinals (by name)
  uint16_t* ordinal_table =
      reinterpret_cast<uint16_t*>(uintptr_t(e) + e->AddressOfNameOrdinals);

  for (uint32_t i = 0; i < e->NumberOfNames; i++) {
    auto fn_name = reinterpret_cast<const char*>(uintptr_t(e) + name_table[i]);
    uint16_t ordinal = ordinal_table[i];
    uint32_t addr = base_address_ + function_table[ordinal];
    if (name == std::string_view(fn_name)) {
      // We have a match!
      return addr;
    }
  }

  // No match
  return 0;
}

int XexModule::ApplyPatch(XexModule* module) {
  if (!is_patch()) {
    // This isn't a XEX2 patch.
    return 1;
  }

  const uint32_t original_base_address = module->base_address();

  // Grab the delta descriptor and get to work.
  xex2_opt_delta_patch_descriptor* patch_header = nullptr;
  GetOptHeader(XEX_HEADER_DELTA_PATCH_DESCRIPTOR,
               reinterpret_cast<void**>(&patch_header));
  assert_not_null(patch_header);

  uint32_t size = module->xex_header()->header_size;
  if (patch_header->delta_headers_source_offset > size) {
    XELOGE("XEX header patch source is outside base XEX header area");
    return 2;
  }

  uint32_t header_size_available =
      size - patch_header->delta_headers_source_offset;
  if (patch_header->delta_headers_source_size > header_size_available) {
    XELOGE("XEX header patch source is too large");
    return 3;
  }

  if (patch_header->delta_headers_target_offset >
      patch_header->size_of_target_headers) {
    XELOGE("XEX header patch target is outside base XEX header area");
    return 4;
  }

  uint32_t delta_target_size = patch_header->size_of_target_headers -
                               patch_header->delta_headers_target_offset;
  if (patch_header->delta_headers_source_size > delta_target_size) {
    return 5;  // ? unsure what the point of this test is, kernel checks for it
               // though
  }

  // Patch base XEX header
  uint32_t original_image_size = module->image_size();
  uint32_t header_target_size = patch_header->size_of_target_headers;

  if (!header_target_size) {
    header_target_size = patch_header->delta_headers_target_offset +
                         patch_header->delta_headers_source_size;
  }

  size_t mem_size = module->xex_header_mem_.size();

  // Increase xex header buffer length if needed
  if (header_target_size > module->xex_header_mem_.size()) {
    module->xex_header_mem_.resize(header_target_size);
  }

  auto header_ptr = (uint8_t*)module->xex_header();

  // If headers_source_offset is set, copy [source_offset:source_size] to
  // target_offset
  if (patch_header->delta_headers_source_offset) {
    memcpy(header_ptr + patch_header->delta_headers_target_offset,
           header_ptr + patch_header->delta_headers_source_offset,
           patch_header->delta_headers_source_size);
  }

  // If new size is smaller than original, null out the difference
  if (header_target_size < module->xex_header_mem_.size()) {
    memset(header_ptr + header_target_size, 0,
           module->xex_header_mem_.size() - header_target_size);
  }

  auto file_format_header = opt_file_format_info();
  assert_not_null(file_format_header);

  // Apply header patch...
  uint32_t headerpatch_size = patch_header->size;

  int result_code = lzxdelta_apply_patch(
      &patch_header->info, headerpatch_size,
      file_format_header->compression_info.normal.window_size, header_ptr);
  if (result_code) {
    XELOGE("XEX header patch application failed, error code {}", result_code);
    return result_code;
  }

  // Decrease xex header buffer length if needed (but only after patching)
  if (module->xex_header_mem_.size() > header_target_size) {
    module->xex_header_mem_.resize(header_target_size);
  }

  // Update security info context with latest security info data
  module->ReadSecurityInfo();

  uint32_t new_image_size = module->image_size();

  // Check if we need to alloc new memory for the patched xex
  if (new_image_size > original_image_size) {
    uint32_t size_delta = new_image_size - original_image_size;
    uint32_t addr_new_mem = module->base_address_ + original_image_size;

    // Before we allocate new range we must check if patch haven't modified
    // base_address.
    uint32_t new_base_address = module->base_address();
    xe::be<uint32_t>* base_addr_opt = nullptr;
    if (module->GetOptHeader(XEX_HEADER_IMAGE_BASE_ADDRESS, &base_addr_opt)) {
      new_base_address = *base_addr_opt;
    }

    if (original_base_address != new_base_address) {
      XELOGW(
          "Patch for module: {} changed base_address from {:08X} to {:08X}, "
          "need to reallocate xex "
          "data!",
          module->name(), module->base_address_, new_base_address);
      module->base_address_ = new_base_address;
      addr_new_mem = new_base_address;
      size_delta = new_image_size;
    }

    bool alloc_result =
        memory()
            ->LookupHeap(addr_new_mem)
            ->AllocFixed(
                addr_new_mem, size_delta, 4096,
                xe::kMemoryAllocationReserve | xe::kMemoryAllocationCommit,
                xe::kMemoryProtectRead | xe::kMemoryProtectWrite);

    if (!alloc_result) {
      XELOGE("Unable to allocate XEX memory at {:08X}-{:08X}.", addr_new_mem,
             size_delta);
      assert_always();
      return 6;
    }

    // For base_address change we need to copy data from previous allocation to
    // new one
    if (original_base_address != new_base_address) {
      kernel_state_->memory()->Copy(new_base_address, original_base_address,
                                    original_image_size);
    }
  }

  uint8_t orig_session_key[0x10];
  memcpy(orig_session_key, module->session_key_, 0x10);

  // Header patch updated the base XEX key, need to redecrypt it
  aes_decrypt_buffer(
      module->is_dev_kit_ ? xe_xex2_devkit_key : xe_xex2_retail_key,
      reinterpret_cast<const uint8_t*>(module->xex_security_info()->aes_key),
      16, module->session_key_, 16);

  // Decrypt the patch XEX's key using base XEX key
  aes_decrypt_buffer(
      module->session_key_,
      reinterpret_cast<const uint8_t*>(xex_security_info()->aes_key), 16,
      session_key_, 16);

  // Test delta key against our decrypted keys
  // (kernel doesn't seem to check this, but it's the one use for the
  // image_key_source field I can think of...)
  uint8_t test_delta_key[0x10];
  aes_decrypt_buffer(module->session_key_, patch_header->image_key_source, 0x10,
                     test_delta_key, 0x10);

  if (memcmp(test_delta_key, orig_session_key, 0x10) != 0) {
    XELOGE("XEX patch image key doesn't match original XEX!");
    return 7;
  }

  // Decrypt (if needed).
  bool free_input = false;
  const uint8_t* patch_buffer = xexp_data_mem_.data();
  const size_t patch_length = xexp_data_mem_.size();

  const uint8_t* input_buffer = patch_buffer;

  switch (file_format_header->encryption_type) {
    case XEX_ENCRYPTION_NONE:
      // No-op.
      break;
    case XEX_ENCRYPTION_NORMAL:
      // TODO: a way to do without a copy/alloc?
      free_input = true;
      input_buffer = (const uint8_t*)calloc(1, patch_length);
      aes_decrypt_buffer(session_key_, patch_buffer, patch_length,
                         (uint8_t*)input_buffer, patch_length);
      break;
    default:
      assert_always();
      return 8;
  }

  const xex2_compressed_block_info* cur_block =
      &file_format_header->compression_info.normal.first_block;

  const uint8_t* p = input_buffer;
  uint8_t* base_exe = memory()->TranslateVirtual(module->base_address_);

  // If image_source_offset is set, copy [source_offset:source_size] to
  // target_offset
  if (patch_header->delta_image_source_offset) {
    memcpy(base_exe + patch_header->delta_image_target_offset,
           base_exe + patch_header->delta_image_source_offset,
           patch_header->delta_image_source_size);
  }

  // TODO: should we use new_image_size here instead?
  uint32_t image_target_size = patch_header->delta_image_target_offset +
                               patch_header->delta_image_source_size;

  // If new size is smaller than original, null out the difference
  if (image_target_size < original_image_size) {
    memset(base_exe + image_target_size, 0,
           original_image_size - image_target_size);
  }

  uint8_t digest[0x14];
  sha1::SHA1 s;
  // Now loop through each block and apply the delta patches inside
  while (cur_block->block_size) {
    const auto* next_block = (const xex2_compressed_block_info*)p;

    // Compare block hash, if no match we probably used wrong decrypt key
    s.reset();
    s.processBytes(p, cur_block->block_size);
    s.finalize(digest);

    if (memcmp(digest, cur_block->block_hash, 0x14) != 0) {
      result_code = 9;
      XELOGE("XEX patch block hash doesn't match hash inside block info!");
      break;
    }

    // skip block info
    p += 20;
    p += 4;

    uint32_t block_data_size = cur_block->block_size - 20 - 4;

    // Apply delta patch
    result_code = lzxdelta_apply_patch(
        (xex2_delta_patch*)p, block_data_size,
        file_format_header->compression_info.normal.window_size, base_exe);
    if (result_code) {
      break;
    }

    p += block_data_size;
    cur_block = next_block;
  }

  if (!result_code) {
    // Decommit unused pages if new image size is smaller than original
    if (original_image_size > new_image_size) {
      uint32_t size_delta = original_image_size - new_image_size;
      uint32_t addr_free_mem = module->base_address_ + new_image_size;

      bool free_result = memory()
                             ->LookupHeap(addr_free_mem)
                             ->Decommit(addr_free_mem, size_delta);

      if (!free_result) {
        XELOGE("Unable to decommit XEX memory at {:08X}-{:08X}.", addr_free_mem,
               size_delta);
        assert_always();
      }
    }

    xex2_version source_ver, target_ver;
    source_ver = patch_header->source_version();
    target_ver = patch_header->target_version();
    XELOGI(
        "XEX patch applied successfully: base version: {}.{}.{}.{}, new "
        "version: {}.{}.{}.{}",
        source_ver.major, source_ver.minor, source_ver.build, source_ver.qfe,
        target_ver.major, target_ver.minor, target_ver.build, target_ver.qfe);
  } else {
    XELOGE("XEX patch application failed, error code {}", result_code);
  }

  if (free_input) {
    free((void*)input_buffer);
  }
  return result_code;
}

int XexModule::ReadImage(const void* xex_addr, size_t xex_length,
                         bool use_dev_key) {
  if (!opt_file_format_info()) {
    return 1;
  }

  is_dev_kit_ = use_dev_key;

  if (is_patch()) {
    // Make a copy of patch data for other XEX's to use with ApplyPatch()
    const uint32_t data_len =
        static_cast<uint32_t>(xex_length - xex_header()->header_size);
    xexp_data_mem_.resize(data_len);
    std::memcpy(xexp_data_mem_.data(),
                (uint8_t*)xex_addr + xex_header()->header_size, data_len);
    return 0;
  }

  memory()->LookupHeap(base_address_)->Reset();

  aes_decrypt_buffer(
      use_dev_key ? xe_xex2_devkit_key : xe_xex2_retail_key,
      reinterpret_cast<const uint8_t*>(xex_security_info()->aes_key), 16,
      session_key_, 16);

  int result_code = 0;
  switch (opt_file_format_info()->compression_type) {
    case XEX_COMPRESSION_NONE:
      result_code = ReadImageUncompressed(xex_addr, xex_length);
      break;
    case XEX_COMPRESSION_BASIC:
      result_code = ReadImageBasicCompressed(xex_addr, xex_length);
      break;
    case XEX_COMPRESSION_NORMAL:
      result_code = ReadImageCompressed(xex_addr, xex_length);
      break;
    default:
      assert_always();
      return 2;
  }

  if (result_code) {
    return result_code;
  }

  if (is_patch() || is_valid_executable()) {
    return 0;
  }

  // Not a patch and image doesn't have proper PE header, return 3
  return 3;
}

int XexModule::ReadImageUncompressed(const void* xex_addr, size_t xex_length) {
  // Allocate in-place the XEX memory.
  const uint32_t exe_length =
      static_cast<uint32_t>(xex_length - xex_header()->header_size);

  uint32_t uncompressed_size = exe_length;
  bool alloc_result =
      memory()
          ->LookupHeap(base_address_)
          ->AllocFixed(
              base_address_, uncompressed_size, 4096,
              xe::kMemoryAllocationReserve | xe::kMemoryAllocationCommit,
              xe::kMemoryProtectRead | xe::kMemoryProtectWrite);
  if (!alloc_result) {
    XELOGE("Unable to allocate XEX memory at {:08X}-{:08X}.", base_address_,
           uncompressed_size);
    return 2;
  }
  uint8_t* buffer = memory()->TranslateVirtual(base_address_);
  std::memset(buffer, 0, uncompressed_size);

  const uint8_t* p = (const uint8_t*)xex_addr + xex_header()->header_size;

  switch (opt_file_format_info()->encryption_type) {
    case XEX_ENCRYPTION_NONE:
      if (exe_length > uncompressed_size) {
        return 1;
      }
      memcpy(buffer, p, exe_length);
      return 0;
    case XEX_ENCRYPTION_NORMAL:
      aes_decrypt_buffer(session_key_, p, exe_length, buffer,
                         uncompressed_size);
      return 0;
    default:
      assert_always();
      return 1;
  }

  return 0;
}

int XexModule::ReadImageBasicCompressed(const void* xex_addr,
                                        size_t xex_length) {
  const uint32_t exe_length =
      static_cast<uint32_t>(xex_length - xex_header()->header_size);
  const uint8_t* source_buffer =
      (const uint8_t*)xex_addr + xex_header()->header_size;
  const uint8_t* p = source_buffer;

  auto heap = memory()->LookupHeap(base_address_);

  // Calculate uncompressed length.
  uint32_t uncompressed_size = 0;

  auto* file_info = opt_file_format_info();
  auto& comp_info = file_info->compression_info.basic;

  uint32_t block_count = (file_info->info_size - 8) / 8;
  for (uint32_t n = 0; n < block_count; n++) {
    const uint32_t data_size = comp_info.blocks[n].data_size;
    const uint32_t zero_size = comp_info.blocks[n].zero_size;
    uncompressed_size += data_size + zero_size;
  }

  // Calculate the total size of the XEX image from its headers.
  uint32_t total_size = 0;
  for (uint32_t i = 0; i < xex_security_info()->page_descriptor_count; i++) {
    // Byteswap the bitfield manually.
    xex2_page_descriptor desc;
    desc.value = xe::byte_swap(xex_security_info()->page_descriptors[i].value);

    total_size += desc.page_count * heap->page_size();
  }

  // Allocate in-place the XEX memory.
  bool alloc_result = heap->AllocFixed(
      base_address_, total_size, 4096,
      xe::kMemoryAllocationReserve | xe::kMemoryAllocationCommit,
      xe::kMemoryProtectRead | xe::kMemoryProtectWrite);
  if (!alloc_result) {
    XELOGE("Unable to allocate XEX memory at {:08X}-{:08X}.", base_address_,
           uncompressed_size);
    return 1;
  }

  uint8_t* buffer = memory()->TranslateVirtual(base_address_);
  std::memset(buffer, 0, total_size);  // Quickly zero the contents.
  uint8_t* d = buffer;

  uint32_t rk[4 * (MAXNR + 1)];
  uint8_t ivec[16] = {0};
  int32_t Nr = rijndaelKeySetupDec(rk, session_key_, 128);

  for (size_t n = 0; n < block_count; n++) {
    const uint32_t data_size = comp_info.blocks[n].data_size;
    const uint32_t zero_size = comp_info.blocks[n].zero_size;

    switch (opt_file_format_info()->encryption_type) {
      case XEX_ENCRYPTION_NONE:
        if (data_size > uncompressed_size - (d - buffer)) {
          // Overflow.
          return 1;
        }
        memcpy(d, p, data_size);
        break;
      case XEX_ENCRYPTION_NORMAL: {
        const uint8_t* ct = p;
        uint8_t* pt = d;
        for (size_t m = 0; m < data_size; m += 16, ct += 16, pt += 16) {
          // Decrypt 16 uint8_ts from input -> output.
          rijndaelDecrypt(rk, Nr, ct, pt);
          for (size_t i = 0; i < 16; i++) {
            // XOR with previous.
            pt[i] ^= ivec[i];
            // Set previous.
            ivec[i] = ct[i];
          }
        }
      } break;
      default:
        assert_always();
        return 1;
    }

    p += data_size;
    d += data_size + zero_size;
  }

  return 0;
}

int XexModule::ReadImageCompressed(const void* xex_addr, size_t xex_length) {
  const uint32_t exe_length =
      static_cast<uint32_t>(xex_length - xex_header()->header_size);
  const uint8_t* exe_buffer =
      (const uint8_t*)xex_addr + xex_header()->header_size;

  // src -> dest:
  // - decrypt (if encrypted)
  // - de-block:
  //    4b total size of next block in uint8_ts
  //   20b hash of entire next block (including size/hash)
  //    Nb block uint8_ts
  // - decompress block contents

  uint8_t* compress_buffer = NULL;
  const uint8_t* p = NULL;
  uint8_t* d = NULL;
  sha1::SHA1 s;

  // Decrypt (if needed).
  bool free_input = false;
  const uint8_t* input_buffer = exe_buffer;
  size_t input_size = exe_length;

  switch (opt_file_format_info()->encryption_type) {
    case XEX_ENCRYPTION_NONE:
      // No-op.
      break;
    case XEX_ENCRYPTION_NORMAL:
      // TODO: a way to do without a copy/alloc?
      free_input = true;
      input_buffer = (const uint8_t*)calloc(1, exe_length);
      aes_decrypt_buffer(session_key_, exe_buffer, exe_length,
                         (uint8_t*)input_buffer, exe_length);
      break;
    default:
      assert_always();
      return 1;
  }

  const auto* compression_info = &opt_file_format_info()->compression_info;
  const xex2_compressed_block_info* cur_block =
      &compression_info->normal.first_block;

  compress_buffer = (uint8_t*)calloc(1, exe_length);

  p = input_buffer;
  d = compress_buffer;

  // De-block.
  int result_code = 0;

  uint8_t block_calced_digest[0x14];
  while (cur_block->block_size) {
    const uint8_t* pnext = p + cur_block->block_size;
    const auto* next_block = (const xex2_compressed_block_info*)p;

    // Compare block hash, if no match we probably used wrong decrypt key
    s.reset();
    s.processBytes(p, cur_block->block_size);
    s.finalize(block_calced_digest);
    if (memcmp(block_calced_digest, cur_block->block_hash, 0x14) != 0) {
      result_code = 2;
      break;
    }

    // skip block info
    p += 4;
    p += 20;

    while (true) {
      const size_t chunk_size = (p[0] << 8) | p[1];
      p += 2;
      if (!chunk_size) {
        break;
      }

      memcpy(d, p, chunk_size);
      p += chunk_size;
      d += chunk_size;
    }

    p = pnext;
    cur_block = next_block;
  }

  if (!result_code) {
    uint32_t uncompressed_size = image_size();

    // Allocate in-place the XEX memory.
    bool alloc_result =
        memory()
            ->LookupHeap(base_address_)
            ->AllocFixed(
                base_address_, uncompressed_size, 4096,
                xe::kMemoryAllocationReserve | xe::kMemoryAllocationCommit,
                xe::kMemoryProtectRead | xe::kMemoryProtectWrite);

    if (alloc_result) {
      uint8_t* buffer = memory()->TranslateVirtual(base_address_);
      std::memset(buffer, 0, uncompressed_size);

      // Decompress into XEX base
      result_code = lzx_decompress(
          compress_buffer, d - compress_buffer, buffer, uncompressed_size,
          compression_info->normal.window_size, nullptr, 0);
    } else {
      XELOGE("Unable to allocate XEX memory at {:08X}-{:08X}.", base_address_,
             uncompressed_size);
      result_code = 3;
    }
  }

  if (compress_buffer) {
    free((void*)compress_buffer);
  }
  if (free_input) {
    free((void*)input_buffer);
  }
  return result_code;
}

int XexModule::ReadPEHeaders() {
  const uint8_t* p = memory()->TranslateVirtual(base_address_);

  // Verify DOS signature (MZ).
  auto doshdr = reinterpret_cast<const IMAGE_DOS_HEADER*>(p);
  if (doshdr->e_magic != IMAGE_DOS_SIGNATURE) {
    XELOGE("PE signature mismatch; likely bad decryption/decompression");
    return 1;
  }

  // Move to the NT header offset from the DOS header.
  p += doshdr->e_lfanew;

  // Verify NT signature (PE\0\0).
  auto nthdr = reinterpret_cast<const IMAGE_NT_HEADERS32*>(p);
  if (nthdr->Signature != IMAGE_NT_SIGNATURE) {
    return 1;
  }

  // Verify matches an Xbox PE.
  const IMAGE_FILE_HEADER* filehdr = &nthdr->FileHeader;
  if ((filehdr->Machine != IMAGE_FILE_MACHINE_POWERPCBE) ||
      !(filehdr->Characteristics & IMAGE_FILE_32BIT_MACHINE)) {
    return 1;
  }
  // Verify the expected size.
  if (filehdr->SizeOfOptionalHeader != IMAGE_SIZEOF_NT_OPTIONAL_HEADER) {
    return 1;
  }

  // Verify optional header is 32bit.
  const IMAGE_OPTIONAL_HEADER32* opthdr = &nthdr->OptionalHeader;
  if (opthdr->Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC) {
    return 1;
  }
  // Verify subsystem.
  if (opthdr->Subsystem != IMAGE_SUBSYSTEM_XBOX) {
    return 1;
  }

// Linker version - likely 8+
// Could be useful for recognizing certain patterns
// opthdr->MajorLinkerVersion; opthdr->MinorLinkerVersion;

// Data directories of interest:
// EXPORT           IMAGE_EXPORT_DIRECTORY
// IMPORT           IMAGE_IMPORT_DESCRIPTOR[]
// EXCEPTION        IMAGE_CE_RUNTIME_FUNCTION_ENTRY[]
// BASERELOC
// DEBUG            IMAGE_DEBUG_DIRECTORY[]
// ARCHITECTURE     /IMAGE_ARCHITECTURE_HEADER/ ----- import thunks!
// TLS              IMAGE_TLS_DIRECTORY
// IAT              Import Address Table ptr
// opthdr->DataDirectory[IMAGE_DIRECTORY_ENTRY_X].VirtualAddress / .Size

// The macros in pe_image.h don't work with clang, for some reason.
// offsetof seems to be unable to find OptionalHeader.
#define offsetof1(type, member) ((std::size_t) & (((type*)0)->member))
#define IMAGE_FIRST_SECTION1(ntheader)                                   \
  ((PIMAGE_SECTION_HEADER)((uint8_t*)ntheader +                          \
                           offsetof1(IMAGE_NT_HEADERS, OptionalHeader) + \
                           ((PIMAGE_NT_HEADERS)(ntheader))               \
                               ->FileHeader.SizeOfOptionalHeader))

  // Quick scan to determine bounds of sections.
  size_t upper_address = 0;
  const IMAGE_SECTION_HEADER* sechdr = IMAGE_FIRST_SECTION1(nthdr);
  for (size_t n = 0; n < filehdr->NumberOfSections; n++, sechdr++) {
    const size_t physical_address = opthdr->ImageBase + sechdr->VirtualAddress;
    upper_address =
        std::max(upper_address, physical_address + sechdr->Misc.VirtualSize);
  }

  // Setup/load sections.
  sechdr = IMAGE_FIRST_SECTION1(nthdr);
  for (size_t n = 0; n < filehdr->NumberOfSections; n++, sechdr++) {
    PESection section;
    memcpy(section.name, sechdr->Name, sizeof(sechdr->Name));
    section.name[8] = 0;
    section.raw_address = sechdr->PointerToRawData;
    section.raw_size = sechdr->SizeOfRawData;
    section.address = base_address_ + sechdr->VirtualAddress;
    section.size = sechdr->Misc.VirtualSize;
    section.flags = sechdr->Characteristics;
    pe_sections_.push_back(section);
  }

  // DumpTLSDirectory(pImageBase, pNTHeader, (PIMAGE_TLS_DIRECTORY32)0);
  // DumpExportsSection(pImageBase, pNTHeader);
  return 0;
}

void XexModule::ReadSecurityInfo() {
  if (xex_format_ == kFormatXex1) {
    const xex1_security_info* xex1_sec_info =
        reinterpret_cast<const xex1_security_info*>(
            GetSecurityInfo(xex_header()));

    security_info_.rsa_signature = xex1_sec_info->rsa_signature;
    security_info_.aes_key = xex1_sec_info->aes_key;
    security_info_.image_size = xex1_sec_info->image_size;
    security_info_.image_flags = xex1_sec_info->image_flags;
    security_info_.export_table = xex1_sec_info->export_table;
    security_info_.load_address = xex1_sec_info->load_address;
    security_info_.page_descriptor_count = xex1_sec_info->page_descriptor_count;
    security_info_.page_descriptors = xex1_sec_info->page_descriptors;
  } else if (xex_format_ == kFormatXex2) {
    const xex2_security_info* xex2_sec_info =
        reinterpret_cast<const xex2_security_info*>(
            GetSecurityInfo(xex_header()));

    security_info_.rsa_signature = xex2_sec_info->rsa_signature;
    security_info_.aes_key = xex2_sec_info->aes_key;
    security_info_.image_size = xex2_sec_info->image_size;
    security_info_.image_flags = xex2_sec_info->image_flags;
    security_info_.export_table = xex2_sec_info->export_table;
    security_info_.load_address = xex2_sec_info->load_address;
    security_info_.page_descriptor_count = xex2_sec_info->page_descriptor_count;
    security_info_.page_descriptors = xex2_sec_info->page_descriptors;
  }
}

bool XexModule::Load(const std::string_view name, const std::string_view path,
                     const void* xex_addr, size_t xex_length) {
  auto src_header = reinterpret_cast<const xex2_header*>(xex_addr);

  if (src_header->magic == kXEX1Signature) {
    xex_format_ = kFormatXex1;
  } else if (src_header->magic == kXEX2Signature) {
    xex_format_ = kFormatXex2;
  } else {
    return false;
  }

  assert_false(loaded_);
  loaded_ = true;

  // Read in XEX headers
  xex_header_mem_.resize(src_header->header_size);
  std::memcpy(xex_header_mem_.data(), src_header, src_header->header_size);

  // Read/convert XEX1/XEX2 security info to a common format
  ReadSecurityInfo();

  auto sec_header = xex_security_info();

  // Try setting our base_address based on XEX_HEADER_IMAGE_BASE_ADDRESS, fall
  // back to xex_security_info otherwise
  base_address_ = xex_security_info()->load_address;
  xe::be<uint32_t>* base_addr_opt = nullptr;
  if (GetOptHeader(XEX_HEADER_IMAGE_BASE_ADDRESS, &base_addr_opt))
    base_address_ = *base_addr_opt;

  // Setup debug info.
  name_ = name;
  path_ = path;

  // Load in the XEX basefile
  // We'll try using both XEX2 keys to see if any give a valid PE
  int result_code = ReadImage(xex_addr, xex_length, false);
  if (result_code) {
    XELOGW("XEX load failed with code {}, trying with devkit encryption key...",
           result_code);

    result_code = ReadImage(xex_addr, xex_length, true);
    if (result_code) {
      XELOGE("XEX load failed with code {}, tried both encryption keys",
             result_code);
      return false;
    }
  }

  // Note: caller will have to call LoadContinue once it's determined whether a
  // patch file exists or not!
  return true;
}

bool XexModule::LoadContinue() {
  // Second part of image load
  // Split from Load() so that we can patch the XEX before loading this data
  assert_false(finished_load_);
  if (finished_load_) {
    return true;
  }

  finished_load_ = true;

  if (ReadPEHeaders()) {
    XELOGE("Failed to load XEX PE headers!");
    return false;
  }

  // Parse any "unsafe" headers into safer variants
  xex2_opt_generic_u32* alternate_titleids;
  if (GetOptHeader(xex2_header_keys::XEX_HEADER_ALTERNATE_TITLE_IDS,
                   &alternate_titleids)) {
    auto count = alternate_titleids->count();
    for (uint32_t i = 0; i < count; i++) {
      opt_alternate_title_ids_.push_back(alternate_titleids->values[i]);
    }
  }

  // Scan and find the low/high addresses.
  // All code sections are continuous, so this should be easy.
  // could use a source for the above information
  auto heap = memory()->LookupHeap(base_address_);
  auto page_size = heap->page_size();

  low_address_ = UINT_MAX;
  high_address_ = 0;

  auto sec_header = xex_security_info();
  for (uint32_t i = 0, page = 0; i < sec_header->page_descriptor_count; i++) {
    // Byteswap the bitfield manually.
    xex2_page_descriptor desc;
    desc.value = xe::byte_swap(sec_header->page_descriptors[i].value);

    const auto start_address = base_address_ + (page * page_size);
    const auto end_address = start_address + (desc.page_count * page_size);
    if (desc.info == XEX_SECTION_CODE) {
      low_address_ = std::min(low_address_, start_address);
      high_address_ = std::max(high_address_, end_address);
    }

    page += desc.page_count;
  }

  // Notify backend that we have an executable range.
  processor_->backend()->CommitExecutableRange(low_address_, high_address_);

  // Add all imports (variables/functions).
  xex2_opt_import_libraries* opt_import_libraries = nullptr;
  GetOptHeader(XEX_HEADER_IMPORT_LIBRARIES, &opt_import_libraries);

  if (opt_import_libraries) {
    // FIXME: Don't know if 32 is the actual limit, but haven't seen more than
    // 2.
    const char* string_table[32];
    std::memset(string_table, 0, sizeof(string_table));

    // Parse the string table
    for (size_t i = 0, o = 0; i < opt_import_libraries->string_table.size &&
                              o < opt_import_libraries->string_table.count;
         ++o) {
      assert_true(o < xe::countof(string_table));
      const char* str = &opt_import_libraries->string_table.data[i];

      string_table[o] = str;
      i += std::strlen(str) + 1;

      // Padding
      if ((i % 4) != 0) {
        i += 4 - (i % 4);
      }
    }

    auto library_data = reinterpret_cast<uint8_t*>(opt_import_libraries);
    uint32_t library_offset = opt_import_libraries->string_table.size + 12;
    while (library_offset < opt_import_libraries->size) {
      auto library =
          reinterpret_cast<xex2_import_library*>(library_data + library_offset);
      if (!library->size) {
        break;
      }
      size_t library_name_index = library->name_index & 0xFF;
      assert_true(library_name_index <
                  opt_import_libraries->string_table.count);
      assert_not_null(string_table[library_name_index]);
      auto library_name = std::string(string_table[library_name_index]);
      SetupLibraryImports(library_name, library);
      library_offset += library->size;
    }
  }

  // Load a specified module map and diff.
  if (cvars::load_module_map.size()) {
    if (!ReadMap(cvars::load_module_map.c_str())) {
      return false;
    }
  }

  // Disable write protection if plugins are enabled
  if (cvars::allow_plugins && !cvars::writable_code_segments) {
    OVERRIDE_bool(writable_code_segments, true);
  }

  // Setup memory protection.
  for (uint32_t i = 0, page = 0; i < sec_header->page_descriptor_count; i++) {
    // Byteswap the bitfield manually.
    xex2_page_descriptor desc;
    desc.value = xe::byte_swap(sec_header->page_descriptors[i].value);

    auto address = base_address_ + (page * page_size);
    auto size = desc.page_count * page_size;
    switch (desc.info) {
      case XEX_SECTION_CODE:
      case XEX_SECTION_READONLY_DATA:
        heap->Protect(address, size,
                      cvars::writable_code_segments
                          ? kMemoryProtectRead | kMemoryProtectWrite
                          : kMemoryProtectRead);
        break;
      case XEX_SECTION_DATA:
        heap->Protect(address, size, kMemoryProtectRead | kMemoryProtectWrite);
        break;
    }

    page += desc.page_count;
  }

  return true;
}

void XexModule::Precompile() {
  sha1::SHA1 final_image_sha_;

  final_image_sha_.reset();

  unsigned high_code = this->high_address_ - this->low_address_;

  final_image_sha_.processBytes(memory()->TranslateVirtual(this->low_address_),
                                high_code);
  final_image_sha_.finalize(image_sha_bytes_);

  char fmtbuf[20];

  for (unsigned i = 0; i < 20; ++i) {
#ifdef XE_PLATFORM_WIN32
    sprintf_s(fmtbuf, "%X", image_sha_bytes_[i]);
#else
    snprintf(fmtbuf, sizeof(fmtbuf), "%X", image_sha_bytes_[i]);
#endif
    image_sha_str_ += &fmtbuf[0];
  }

  // Find __savegprlr_* and __restgprlr_* and the others.
  // We can flag these for special handling (inlining/etc).
  if (!FindSaveRest()) {
    return;
  }

  info_cache_.Init(this);
  PrecompileDiscoveredFunctions();
}
bool XexModule::Unload() {
  if (!loaded_) {
    return true;
  }
  loaded_ = false;

  // If this isn't a patch, just deallocate the memory occupied by the exe
  if (!is_patch()) {
    assert_not_zero(base_address_);

    memory()->LookupHeap(base_address_)->Release(base_address_);
  }

  xex_header_mem_.resize(0);

  return true;
}

bool XexModule::SetupLibraryImports(const std::string_view name,
                                    const xex2_import_library* library) {
  ExportResolver* kernel_resolver = nullptr;
  if (kernel_state_->IsKernelModule(name)) {
    kernel_resolver = processor_->export_resolver();
  }

  auto user_module = kernel_state_->GetModule(name);

  auto base_name = utf8::find_base_name_from_guest_path(name);

  ImportLibrary library_info;
  library_info.name = base_name;
  library_info.id = library->id;
  library_info.version.value = library->version().value;
  library_info.min_version.value = library->version_min().value;

  // Imports are stored as {import descriptor, thunk addr, import desc, ...}
  // Even thunks have an import descriptor (albeit unused/useless)
  for (uint32_t i = 0; i < library->count; i++) {
    uint32_t record_addr = library->import_table[i];
    assert_not_zero(record_addr);

    auto record_slot =
        memory()->TranslateVirtual<xe::be<uint32_t>*>(record_addr);
    uint32_t record_value = *record_slot;

    uint16_t record_type = (record_value & 0xFF000000) >> 24;
    uint16_t ordinal = record_value & 0xFFFF;

    Export* kernel_export = nullptr;
    uint32_t user_export_addr = 0;

    if (kernel_resolver) {
      kernel_export = kernel_resolver->GetExportByOrdinal(name, ordinal);
    } else if (user_module) {
      user_export_addr = user_module->GetProcAddressByOrdinal(ordinal);
    }

    // Import not resolved?
    if (!kernel_export && !user_export_addr) {
      XELOGW(
          "WARNING: an import variable was not resolved! (library: {}, import "
          "lib: {}, ordinal: {:03X})",
          name_, name, ordinal);
    }

    StringBuffer import_name;
    if (record_type == 0) {
      // Variable.

      ImportLibraryFn import_info;
      import_info.ordinal = ordinal;
      import_info.value_address = record_addr;
      library_info.imports.push_back(import_info);

      import_name.Append("__imp__");
      if (kernel_export) {
        import_name.Append(kernel_export->name);
      } else {
        import_name.AppendFormat("{}_{:03X}", base_name, ordinal);
      }

      if (kernel_export) {
        if (kernel_export->get_type() == Export::Type::kFunction) {
          // Not exactly sure what this should be...
          // Appears to be ignored.
          *record_slot = 0xDEADC0DE;
        } else if (kernel_export->get_type() == Export::Type::kVariable) {
          // Kernel import variable
          if (kernel_export->is_implemented()) {
            // Implemented - replace with pointer.
            *record_slot = kernel_export->variable_ptr;
          } else {
            // Not implemented - write with a dummy value.
            *record_slot = 0xD000BEEF | (kernel_export->ordinal & 0xFFF) << 16;
            XELOGCPU("WARNING: imported a variable with no value: {}",
                     kernel_export->name);
          }
        }
      } else if (user_export_addr) {
        *record_slot = user_export_addr;
      } else {
        *record_slot = 0xF00DF00D;
      }

      // Setup a variable and define it.
      Symbol* var_info;
      DeclareVariable(record_addr, &var_info);
      var_info->set_name(import_name.to_string_view());
      var_info->set_status(Symbol::Status::kDeclared);
      DefineVariable(var_info);
      var_info->set_status(Symbol::Status::kDefined);
    } else if (record_type == 1) {
      // Thunk.
      if (library_info.imports.size() > 0) {
        auto& prev_import =
            library_info.imports[library_info.imports.size() - 1];
        assert_true(prev_import.ordinal == ordinal);
        prev_import.thunk_address = record_addr;
      }

      if (kernel_export) {
        import_name.Append(kernel_export->name);
      } else {
        import_name.AppendFormat("__{}_{:03X}", base_name, ordinal);
      }

      Function* function;
      DeclareFunction(record_addr, &function);
      function->set_end_address(record_addr + 16 - 4);
      function->set_name(import_name.to_string_view());

      if (user_export_addr) {
        // Rewrite PPC code to set r11 to the target address
        // So we'll have:
        //    lis r11, user_export_addr
        //    ori r11, r11, user_export_addr
        //    mtspr CTR, r11
        //    bctr
        uint16_t hi_addr = (user_export_addr >> 16) & 0xFFFF;
        uint16_t low_addr = user_export_addr & 0xFFFF;

        uint8_t* p = memory()->TranslateVirtual(record_addr);
        xe::store_and_swap<uint32_t>(p + 0x0, 0x3D600000 | hi_addr);
        xe::store_and_swap<uint32_t>(p + 0x4, 0x616B0000 | low_addr);
      } else {
        // On load we have something like this in memory:
        //     li r3, 0
        //     li r4, 0x1F5
        //     mtspr CTR, r11
        //     bctr
        // Real consoles rewrite this with some code that sets r11.
        // If we did that we'd still have to put a thunk somewhere and do the
        // dynamic lookup. Instead, we rewrite it to use syscalls.
        // We use sc with a LEV operand of 2, which is reserved usage and
        // should never see actual usage outside of our rewrite.
        // CPU backends can either take the special form syscall or do
        // something smarter.
        //     sc 2
        //     blr
        //     nop
        //     nop
        uint8_t* p = memory()->TranslateVirtual(record_addr);
        xe::store_and_swap<uint32_t>(p + 0x0, 0x44000042);
        xe::store_and_swap<uint32_t>(p + 0x4, 0x4E800020);
        xe::store_and_swap<uint32_t>(p + 0x8, 0x60000000);
        xe::store_and_swap<uint32_t>(p + 0xC, 0x60000000);

        // Note that we may not have a handler registered - if not, eventually
        // we'll get directed to UndefinedImport.
        GuestFunction::ExternHandler handler = nullptr;
        if (kernel_export) {
          if (kernel_export->function_data.trampoline) {
            handler = (GuestFunction::ExternHandler)
                          kernel_export->function_data.trampoline;
          } else {
            //__debugbreak();
            // handler =
            //     (GuestFunction::ExternHandler)kernel_export->function_data.shim;
          }
        } else {
          XELOGW("WARNING: Imported kernel function {} is unimplemented!",
                 import_name.to_string_view());
        }
        static_cast<GuestFunction*>(function)->SetupExtern(handler,
                                                           kernel_export);
      }
      function->set_status(Symbol::Status::kDeclared);
    } else {
      // Bad.
      assert_always();
    }
  }

  import_libs_.push_back(library_info);

  return true;
}

bool XexModule::ContainsAddress(uint32_t address) {
  return address >= low_address_ && address < high_address_;
}

std::unique_ptr<Function> XexModule::CreateFunction(uint32_t address) {
  return std::unique_ptr<Function>(
      processor_->backend()->CreateGuestFunction(this, address));
}
void XexInfoCache::Init(XexModule* xexmod) {
  if (cvars::disable_instruction_infocache) {
    return;
  }

  auto emu = xexmod->kernel_state_->emulator();
  std::filesystem::path infocache_path = emu->cache_root();

  infocache_path.append(L"modules");

  infocache_path.append(xexmod->image_sha_str_);

  std::filesystem::create_directories(infocache_path);
  infocache_path.append("executable_addr_flags.bin");

  unsigned num_codebytes = xexmod->high_address_ - xexmod->low_address_;
  num_codebytes += 3;  // round up to nearest multiple of 4
  num_codebytes &= ~3;

  auto try_open = [this, &infocache_path, num_codebytes]() {
    bool did_exist = true;

    if (!std::filesystem::exists(infocache_path)) {
      xe::filesystem::CreateEmptyFile(infocache_path);
      did_exist = false;
    }

    // todo: prepopulate with stuff from pdata, dll exports

    this->executable_addr_flags_ = std::move(xe::MappedMemory::Open(
        infocache_path, xe::MappedMemory::Mode::kReadWrite, 0,
        sizeof(InfoCacheFlagsHeader) +
            (sizeof(InfoCacheFlags) *
             (num_codebytes /
              4))));  // one infocacheflags entry for each PPC instr-sized addr
    return did_exist;
  };

  bool did_exist = try_open();
  if (!GetHeader()) {
    return;
  }

  if (!did_exist) {
    GetHeader()->version = CURRENT_INFOCACHE_VERSION;

  } else {
    if (GetHeader()->version != CURRENT_INFOCACHE_VERSION) {
      this->executable_addr_flags_->Close();
      std::filesystem::remove(infocache_path);
      try_open();
    }
  }
}
InfoCacheFlags* XexModule::GetInstructionAddressFlags(uint32_t guest_addr) {
  if (guest_addr < low_address_ || guest_addr > high_address_) {
    return nullptr;
  }

  guest_addr -= low_address_;

  return info_cache_.LookupFlags(guest_addr);
}
void XexModule::PrecompileDiscoveredFunctions() {
  if (!cvars::enable_early_precompilation) {
    return;
  }
  auto others = PreanalyzeCode();

  for (auto&& other : others) {
    if (other < low_address_ || other >= high_address_) {
      continue;
    }
    auto sym = processor_->LookupFunction(other);

    if (!sym || sym->status() != Symbol::Status::kDefined) {
      processor_->ResolveFunction(other);
    }
  }
}
void XexModule::PrecompileKnownFunctions() {
  if (!cvars::enable_early_precompilation) {
    return;
  }
  uint32_t start = 0;
  uint32_t end = (high_address_ - low_address_) / 4;
  auto flags = info_cache_.LookupFlags(0);
  if (!flags) {
    return;
  }
  // maybe should pre-acquire global crit?
  for (uint32_t i = 0; i < end; i++) {
    if (flags[i].was_resolved) {
      uint32_t addr = low_address_ + (i * 4);
      auto sym = processor_->LookupFunction(addr);

      if (!sym || sym->status() != Symbol::Status::kDefined) {
        processor_->ResolveFunction(addr);
      }
    }
  }
}

static uint32_t GetBLCalledFunction(XexModule* xexmod, uint32_t current_base,
                                    ppc::PPCOpcodeBits wrd) {
  int32_t displ = static_cast<int32_t>(ppc::XEEXTS26(wrd.I.LI << 2));

  if (wrd.I.AA) {
    return static_cast<uint32_t>(displ);
  } else {
    return static_cast<uint32_t>(static_cast<int32_t>(current_base) + displ);
  }
}
static bool IsOpcodeBL(unsigned w) {
  return (w >> (32 - 6)) == 18 && ppc::PPCOpcodeBits{w}.I.LK;
}

std::vector<uint32_t> XexModule::PreanalyzeCode() {
  uint32_t low_8_aligned = xe::align<uint32_t>(low_address_, 8);

  uint32_t highest_exec_addr = 0;

  for (auto&& sec : pe_sections_) {
    if ((sec.flags & kXEPESectionContainsCode)) {
      highest_exec_addr =
          std::max<uint32_t>(highest_exec_addr, sec.address + sec.size);
    }
  }
  uint32_t high_8_aligned = highest_exec_addr & ~(8U - 1);
  uint32_t n_possible_8byte_addresses = (high_8_aligned - low_8_aligned) / 8;
  uint32_t* funcstart_candidate_stack =
      new uint32_t[n_possible_8byte_addresses];
  uint32_t* funcstart_candstack2 = new uint32_t[n_possible_8byte_addresses];

  uint32_t stack_pos = 0;
  {
    // all functions seem to start on 8 byte boundaries, except for obvious ones
    // like the save/rest funcs
    uint32_t* range_start =
        (uint32_t*)memory()->TranslateVirtual(low_8_aligned);
    uint32_t* range_end = (uint32_t*)memory()->TranslateVirtual(
        high_8_aligned);  // align down to multiple of 8

    const uint8_t mfspr_r12_lr[4] = {0x7D, 0x88, 0x02, 0xA6};

    // a blr instruction, with 4 zero bytes afterwards to pad the next address
    // to 8 byte alignment
    // if we see this prior to our address, we can assume we are a function
    // start
    const uint8_t blr[4] = {0x4E, 0x80, 0x0, 0x20};

    uint32_t blr32 = *reinterpret_cast<const uint32_t*>(&blr[0]);

    uint32_t mfspr_r12_lr32 =
        *reinterpret_cast<const uint32_t*>(&mfspr_r12_lr[0]);

    auto add_new_func = [funcstart_candidate_stack, &stack_pos](uint32_t addr) {
      funcstart_candidate_stack[stack_pos++] = addr;
    };
    /*
                First pass: detect save of the link register at an eight byte
       aligned address
        */
    for (uint32_t* first_pass = range_start; first_pass < range_end;
         first_pass += 2) {
      if (*first_pass == mfspr_r12_lr32) {
        // Push our newly discovered function start into our list
        // All addresses in the list are sorted until the second pass
        add_new_func(
            static_cast<uint32_t>(reinterpret_cast<uintptr_t>(first_pass) -
                                  reinterpret_cast<uintptr_t>(range_start)) +
            low_8_aligned);
      } else if (first_pass[-1] == 0 && *first_pass != 0) {
        // originally i checked for blr followed by 0, but some functions are
        // actually aligned to greater boundaries. something that appears to be
        // longjmp (it occurs in most games, so standard library, and loads ctx,
        // so longjmp) is aligned to 16 bytes in most games
        uint32_t* check_iter = &first_pass[-2];

        while (!*check_iter) {
          --check_iter;
        }

        XE_LIKELY_IF(*check_iter == blr32) {
          add_new_func(
              static_cast<uint32_t>(reinterpret_cast<uintptr_t>(first_pass) -
                                    reinterpret_cast<uintptr_t>(range_start)) +
              low_8_aligned);
        }
      }
    }
    uint32_t current_guestaddr = low_8_aligned;
    // Second pass: detect branch with link instructions and decode the target
    // address. We can safely assume that if bl is to address, that address is
    // the start of the function
    for (uint32_t* second_pass = range_start; second_pass < range_end;
         second_pass++, current_guestaddr += 4) {
      uint32_t current_call = xe::byte_swap(*second_pass);

      if (IsOpcodeBL(current_call)) {
        uint32_t called_function = GetBLCalledFunction(
            this, current_guestaddr, ppc::PPCOpcodeBits{current_call});
        // must be 8 byte aligned and in range
        if ((called_function & (8 - 1)) == 0 &&
            called_function >= low_address_ &&
            called_function < high_address_) {
          add_new_func(called_function);
        }
      }
    }

    auto pdata = this->GetPESection(".pdata");

    if (pdata) {
      uint32_t* pdata_base =
          (uint32_t*)this->memory()->TranslateVirtual(pdata->address);

      uint32_t n_pdata_entries = pdata->raw_size / 8;

      for (uint32_t i = 0; i < n_pdata_entries; ++i) {
        uint32_t funcaddr = xe::load_and_swap<uint32_t>(&pdata_base[i * 2]);
        if (funcaddr >= low_address_ && funcaddr <= highest_exec_addr) {
          add_new_func(funcaddr);
        } else {
          // we hit 0 for func addr, that means we're done
          break;
        }
      }
    }
  }

  // Sort the list of function starts and then ensure that all addresses are
  // unique
  uint32_t n_known_funcaddrs = 0;
  {
    // make addresses unique

    std::sort(funcstart_candidate_stack, funcstart_candidate_stack + stack_pos);

    uint32_t read_pos = 0;
    uint32_t write_pos = 0;
    uint32_t previous_addr = ~0u;
    while (read_pos < stack_pos) {
      uint32_t current_addr = funcstart_candidate_stack[read_pos++];

      if (current_addr != previous_addr) {
        previous_addr = current_addr;
        funcstart_candstack2[write_pos++] = current_addr;
      }
    }
    n_known_funcaddrs = write_pos;
  }

  delete[] funcstart_candidate_stack;

  std::vector<uint32_t> result;
  result.resize(n_known_funcaddrs);
  memcpy(&result[0], funcstart_candstack2,
         sizeof(uint32_t) * n_known_funcaddrs);
  delete[] funcstart_candstack2;
  return result;
}
bool XexModule::FindSaveRest() {
  // Special stack save/restore functions.
  // http://research.microsoft.com/en-us/um/redmond/projects/invisible/src/crt/md/ppc/xxx.s.htm
  // It'd be nice to stash these away and mark them as such to allow for
  // special codegen.
  // __savegprlr_14 to __savegprlr_31
  // __restgprlr_14 to __restgprlr_31
  static const uint32_t gprlr_code_values[] = {
      0x68FFC1F9,  // __savegprlr_14
      0x70FFE1F9,  // __savegprlr_15
      0x78FF01FA,  // __savegprlr_16
      0x80FF21FA,  // __savegprlr_17
      0x88FF41FA,  // __savegprlr_18
      0x90FF61FA,  // __savegprlr_19
      0x98FF81FA,  // __savegprlr_20
      0xA0FFA1FA,  // __savegprlr_21
      0xA8FFC1FA,  // __savegprlr_22
      0xB0FFE1FA,  // __savegprlr_23
      0xB8FF01FB,  // __savegprlr_24
      0xC0FF21FB,  // __savegprlr_25
      0xC8FF41FB,  // __savegprlr_26
      0xD0FF61FB,  // __savegprlr_27
      0xD8FF81FB,  // __savegprlr_28
      0xE0FFA1FB,  // __savegprlr_29
      0xE8FFC1FB,  // __savegprlr_30
      0xF0FFE1FB,  // __savegprlr_31
      0xF8FF8191, 0x2000804E,
      0x68FFC1E9,  // __restgprlr_14
      0x70FFE1E9,  // __restgprlr_15
      0x78FF01EA,  // __restgprlr_16
      0x80FF21EA,  // __restgprlr_17
      0x88FF41EA,  // __restgprlr_18
      0x90FF61EA,  // __restgprlr_19
      0x98FF81EA,  // __restgprlr_20
      0xA0FFA1EA,  // __restgprlr_21
      0xA8FFC1EA,  // __restgprlr_22
      0xB0FFE1EA,  // __restgprlr_23
      0xB8FF01EB,  // __restgprlr_24
      0xC0FF21EB,  // __restgprlr_25
      0xC8FF41EB,  // __restgprlr_26
      0xD0FF61EB,  // __restgprlr_27
      0xD8FF81EB,  // __restgprlr_28
      0xE0FFA1EB,  // __restgprlr_29
      0xE8FFC1EB,  // __restgprlr_30
      0xF0FFE1EB,  // __restgprlr_31
      0xF8FF8181, 0xA603887D, 0x2000804E,
  };
  // __savefpr_14 to __savefpr_31
  // __restfpr_14 to __restfpr_31
  static const uint32_t fpr_code_values[] = {
      0x70FFCCD9,  // __savefpr_14
      0x78FFECD9,  // __savefpr_15
      0x80FF0CDA,  // __savefpr_16
      0x88FF2CDA,  // __savefpr_17
      0x90FF4CDA,  // __savefpr_18
      0x98FF6CDA,  // __savefpr_19
      0xA0FF8CDA,  // __savefpr_20
      0xA8FFACDA,  // __savefpr_21
      0xB0FFCCDA,  // __savefpr_22
      0xB8FFECDA,  // __savefpr_23
      0xC0FF0CDB,  // __savefpr_24
      0xC8FF2CDB,  // __savefpr_25
      0xD0FF4CDB,  // __savefpr_26
      0xD8FF6CDB,  // __savefpr_27
      0xE0FF8CDB,  // __savefpr_28
      0xE8FFACDB,  // __savefpr_29
      0xF0FFCCDB,  // __savefpr_30
      0xF8FFECDB,  // __savefpr_31
      0x2000804E,
      0x70FFCCC9,  // __restfpr_14
      0x78FFECC9,  // __restfpr_15
      0x80FF0CCA,  // __restfpr_16
      0x88FF2CCA,  // __restfpr_17
      0x90FF4CCA,  // __restfpr_18
      0x98FF6CCA,  // __restfpr_19
      0xA0FF8CCA,  // __restfpr_20
      0xA8FFACCA,  // __restfpr_21
      0xB0FFCCCA,  // __restfpr_22
      0xB8FFECCA,  // __restfpr_23
      0xC0FF0CCB,  // __restfpr_24
      0xC8FF2CCB,  // __restfpr_25
      0xD0FF4CCB,  // __restfpr_26
      0xD8FF6CCB,  // __restfpr_27
      0xE0FF8CCB,  // __restfpr_28
      0xE8FFACCB,  // __restfpr_29
      0xF0FFCCCB,  // __restfpr_30
      0xF8FFECCB,  // __restfpr_31
      0x2000804E,
  };
  // __savevmx_14 to __savevmx_31
  // __savevmx_64 to __savevmx_127
  // __restvmx_14 to __restvmx_31
  // __restvmx_64 to __restvmx_127
  static const uint32_t vmx_code_values[] = {
      0xE0FE6039,  // __savevmx_14
      0xCE61CB7D, 0xF0FE6039, 0xCE61EB7D, 0x00FF6039, 0xCE610B7E, 0x10FF6039,
      0xCE612B7E, 0x20FF6039, 0xCE614B7E, 0x30FF6039, 0xCE616B7E, 0x40FF6039,
      0xCE618B7E, 0x50FF6039, 0xCE61AB7E, 0x60FF6039, 0xCE61CB7E, 0x70FF6039,
      0xCE61EB7E, 0x80FF6039, 0xCE610B7F, 0x90FF6039, 0xCE612B7F, 0xA0FF6039,
      0xCE614B7F, 0xB0FF6039, 0xCE616B7F, 0xC0FF6039, 0xCE618B7F, 0xD0FF6039,
      0xCE61AB7F, 0xE0FF6039, 0xCE61CB7F, 0xF0FF6039,  // __savevmx_31
      0xCE61EB7F, 0x2000804E,

      0x00FC6039,  // __savevmx_64
      0xCB610B10, 0x10FC6039, 0xCB612B10, 0x20FC6039, 0xCB614B10, 0x30FC6039,
      0xCB616B10, 0x40FC6039, 0xCB618B10, 0x50FC6039, 0xCB61AB10, 0x60FC6039,
      0xCB61CB10, 0x70FC6039, 0xCB61EB10, 0x80FC6039, 0xCB610B11, 0x90FC6039,
      0xCB612B11, 0xA0FC6039, 0xCB614B11, 0xB0FC6039, 0xCB616B11, 0xC0FC6039,
      0xCB618B11, 0xD0FC6039, 0xCB61AB11, 0xE0FC6039, 0xCB61CB11, 0xF0FC6039,
      0xCB61EB11, 0x00FD6039, 0xCB610B12, 0x10FD6039, 0xCB612B12, 0x20FD6039,
      0xCB614B12, 0x30FD6039, 0xCB616B12, 0x40FD6039, 0xCB618B12, 0x50FD6039,
      0xCB61AB12, 0x60FD6039, 0xCB61CB12, 0x70FD6039, 0xCB61EB12, 0x80FD6039,
      0xCB610B13, 0x90FD6039, 0xCB612B13, 0xA0FD6039, 0xCB614B13, 0xB0FD6039,
      0xCB616B13, 0xC0FD6039, 0xCB618B13, 0xD0FD6039, 0xCB61AB13, 0xE0FD6039,
      0xCB61CB13, 0xF0FD6039, 0xCB61EB13, 0x00FE6039, 0xCF610B10, 0x10FE6039,
      0xCF612B10, 0x20FE6039, 0xCF614B10, 0x30FE6039, 0xCF616B10, 0x40FE6039,
      0xCF618B10, 0x50FE6039, 0xCF61AB10, 0x60FE6039, 0xCF61CB10, 0x70FE6039,
      0xCF61EB10, 0x80FE6039, 0xCF610B11, 0x90FE6039, 0xCF612B11, 0xA0FE6039,
      0xCF614B11, 0xB0FE6039, 0xCF616B11, 0xC0FE6039, 0xCF618B11, 0xD0FE6039,
      0xCF61AB11, 0xE0FE6039, 0xCF61CB11, 0xF0FE6039, 0xCF61EB11, 0x00FF6039,
      0xCF610B12, 0x10FF6039, 0xCF612B12, 0x20FF6039, 0xCF614B12, 0x30FF6039,
      0xCF616B12, 0x40FF6039, 0xCF618B12, 0x50FF6039, 0xCF61AB12, 0x60FF6039,
      0xCF61CB12, 0x70FF6039, 0xCF61EB12, 0x80FF6039, 0xCF610B13, 0x90FF6039,
      0xCF612B13, 0xA0FF6039, 0xCF614B13, 0xB0FF6039, 0xCF616B13, 0xC0FF6039,
      0xCF618B13, 0xD0FF6039, 0xCF61AB13, 0xE0FF6039, 0xCF61CB13,
      0xF0FF6039,  // __savevmx_127
      0xCF61EB13, 0x2000804E,

      0xE0FE6039,  // __restvmx_14
      0xCE60CB7D, 0xF0FE6039, 0xCE60EB7D, 0x00FF6039, 0xCE600B7E, 0x10FF6039,
      0xCE602B7E, 0x20FF6039, 0xCE604B7E, 0x30FF6039, 0xCE606B7E, 0x40FF6039,
      0xCE608B7E, 0x50FF6039, 0xCE60AB7E, 0x60FF6039, 0xCE60CB7E, 0x70FF6039,
      0xCE60EB7E, 0x80FF6039, 0xCE600B7F, 0x90FF6039, 0xCE602B7F, 0xA0FF6039,
      0xCE604B7F, 0xB0FF6039, 0xCE606B7F, 0xC0FF6039, 0xCE608B7F, 0xD0FF6039,
      0xCE60AB7F, 0xE0FF6039, 0xCE60CB7F, 0xF0FF6039,  // __restvmx_31
      0xCE60EB7F, 0x2000804E,

      0x00FC6039,  // __restvmx_64
      0xCB600B10, 0x10FC6039, 0xCB602B10, 0x20FC6039, 0xCB604B10, 0x30FC6039,
      0xCB606B10, 0x40FC6039, 0xCB608B10, 0x50FC6039, 0xCB60AB10, 0x60FC6039,
      0xCB60CB10, 0x70FC6039, 0xCB60EB10, 0x80FC6039, 0xCB600B11, 0x90FC6039,
      0xCB602B11, 0xA0FC6039, 0xCB604B11, 0xB0FC6039, 0xCB606B11, 0xC0FC6039,
      0xCB608B11, 0xD0FC6039, 0xCB60AB11, 0xE0FC6039, 0xCB60CB11, 0xF0FC6039,
      0xCB60EB11, 0x00FD6039, 0xCB600B12, 0x10FD6039, 0xCB602B12, 0x20FD6039,
      0xCB604B12, 0x30FD6039, 0xCB606B12, 0x40FD6039, 0xCB608B12, 0x50FD6039,
      0xCB60AB12, 0x60FD6039, 0xCB60CB12, 0x70FD6039, 0xCB60EB12, 0x80FD6039,
      0xCB600B13, 0x90FD6039, 0xCB602B13, 0xA0FD6039, 0xCB604B13, 0xB0FD6039,
      0xCB606B13, 0xC0FD6039, 0xCB608B13, 0xD0FD6039, 0xCB60AB13, 0xE0FD6039,
      0xCB60CB13, 0xF0FD6039, 0xCB60EB13, 0x00FE6039, 0xCF600B10, 0x10FE6039,
      0xCF602B10, 0x20FE6039, 0xCF604B10, 0x30FE6039, 0xCF606B10, 0x40FE6039,
      0xCF608B10, 0x50FE6039, 0xCF60AB10, 0x60FE6039, 0xCF60CB10, 0x70FE6039,
      0xCF60EB10, 0x80FE6039, 0xCF600B11, 0x90FE6039, 0xCF602B11, 0xA0FE6039,
      0xCF604B11, 0xB0FE6039, 0xCF606B11, 0xC0FE6039, 0xCF608B11, 0xD0FE6039,
      0xCF60AB11, 0xE0FE6039, 0xCF60CB11, 0xF0FE6039, 0xCF60EB11, 0x00FF6039,
      0xCF600B12, 0x10FF6039, 0xCF602B12, 0x20FF6039, 0xCF604B12, 0x30FF6039,
      0xCF606B12, 0x40FF6039, 0xCF608B12, 0x50FF6039, 0xCF60AB12, 0x60FF6039,
      0xCF60CB12, 0x70FF6039, 0xCF60EB12, 0x80FF6039, 0xCF600B13, 0x90FF6039,
      0xCF602B13, 0xA0FF6039, 0xCF604B13, 0xB0FF6039, 0xCF606B13, 0xC0FF6039,
      0xCF608B13, 0xD0FF6039, 0xCF60AB13, 0xE0FF6039, 0xCF60CB13,
      0xF0FF6039,  // __restvmx_127
      0xCF60EB13, 0x2000804E,
  };

  // TODO(benvanik): these are almost always sequential, if present.
  //     It'd be smarter to search around the other ones to prevent
  //     3 full module scans.
  uint32_t gplr_start = 0;
  uint32_t fpr_start = 0;
  uint32_t vmx_start = 0;

  auto page_size = base_address_ <= 0x90000000 ? 64 * 1024 : 4 * 1024;
  auto sec_header = xex_security_info();
  std::vector<uint32_t> resolve_on_exit{};
  resolve_on_exit.reserve(256);
  for (uint32_t i = 0, page = 0; i < sec_header->page_descriptor_count; i++) {
    // Byteswap the bitfield manually.
    xex2_page_descriptor desc;
    desc.value = xe::byte_swap(sec_header->page_descriptors[i].value);

    const auto start_address = base_address_ + (page * page_size);
    const auto end_address = start_address + (desc.page_count * page_size);

    if (desc.info == XEX_SECTION_CODE) {
      if (!gplr_start) {
        gplr_start = memory_->SearchAligned(start_address, end_address,
                                            gprlr_code_values,
                                            xe::countof(gprlr_code_values));
      }
      if (!fpr_start) {
        fpr_start =
            memory_->SearchAligned(start_address, end_address, fpr_code_values,
                                   xe::countof(fpr_code_values));
      }
      if (!vmx_start) {
        vmx_start =
            memory_->SearchAligned(start_address, end_address, vmx_code_values,
                                   xe::countof(vmx_code_values));
      }
      if (gplr_start && fpr_start && vmx_start) {
        break;
      }
    }

    page += desc.page_count;
  }

  // Add function stubs.
  char name[32];

  auto AddXexFunction = [this, &resolve_on_exit](uint32_t address,
                                                 Function** function) {
    DeclareFunction(address, function);
    resolve_on_exit.push_back(address);
  };
  if (gplr_start) {
    uint32_t address = gplr_start;
    for (int n = 14; n <= 31; n++) {
      auto format_result =
          fmt::format_to_n(name, xe::countof(name), "__savegprlr_{}", n);
      Function* function;

      AddXexFunction(address, &function);
      function->set_end_address(address + (31 - n) * 4 + 2 * 4);
      function->set_name(std::string_view(name, format_result.size));
      // TODO(benvanik): set type  fn->type = FunctionSymbol::User;
      // TODO(benvanik): set flags fn->flags |= FunctionSymbol::kFlagSaveGprLr;
      function->set_behavior(Function::Behavior::kProlog);
      function->set_status(Symbol::Status::kDeclared);
      function->SetSaverest(cpu::SaveRestoreType::GPR, false, n);

      address += 4;
    }
    address = gplr_start + 20 * 4;
    for (int n = 14; n <= 31; n++) {
      auto format_result =
          fmt::format_to_n(name, xe::countof(name), "__restgprlr_{}", n);
      Function* function;
      AddXexFunction(address, &function);
      function->set_end_address(address + (31 - n) * 4 + 3 * 4);
      function->set_name(std::string_view(name, format_result.size));
      // TODO(benvanik): set type  fn->type = FunctionSymbol::User;
      // TODO(benvanik): set flags fn->flags |= FunctionSymbol::kFlagRestGprLr;
      function->set_behavior(Function::Behavior::kEpilogReturn);
      function->set_status(Symbol::Status::kDeclared);
      function->SetSaverest(cpu::SaveRestoreType::GPR, true, n);
      address += 4;
    }
  }
  if (fpr_start) {
    uint32_t address = fpr_start;
    for (int n = 14; n <= 31; n++) {
      auto format_result =
          fmt::format_to_n(name, xe::countof(name), "__savefpr_{}", n);
      Function* function;
      AddXexFunction(address, &function);
      function->set_end_address(address + (31 - n) * 4 + 1 * 4);
      function->set_name(std::string_view(name, format_result.size));
      // TODO(benvanik): set type  fn->type = FunctionSymbol::User;
      // TODO(benvanik): set flags fn->flags |= FunctionSymbol::kFlagSaveFpr;
      function->set_behavior(Function::Behavior::kProlog);
      function->set_status(Symbol::Status::kDeclared);

      function->SetSaverest(cpu::SaveRestoreType::FPR, false, n);
      address += 4;
    }
    address = fpr_start + (18 * 4) + (1 * 4);
    for (int n = 14; n <= 31; n++) {
      auto format_result =
          fmt::format_to_n(name, xe::countof(name), "__restfpr_{}", n);
      Function* function;
      AddXexFunction(address, &function);
      function->set_end_address(address + (31 - n) * 4 + 1 * 4);
      function->set_name(std::string_view(name, format_result.size));
      // TODO(benvanik): set type  fn->type = FunctionSymbol::User;
      // TODO(benvanik): set flags fn->flags |= FunctionSymbol::kFlagRestFpr;
      function->set_behavior(Function::Behavior::kEpilog);
      function->set_status(Symbol::Status::kDeclared);
      function->SetSaverest(cpu::SaveRestoreType::FPR, true, n);
      address += 4;
    }
  }
  if (vmx_start) {
    // vmx is:
    // 14-31 save
    // 64-127 save
    // 14-31 rest
    // 64-127 rest
    uint32_t address = vmx_start;
    for (int n = 14; n <= 31; n++) {
      auto format_result =
          fmt::format_to_n(name, xe::countof(name), "__savevmx_{}", n);
      Function* function;
      AddXexFunction(address, &function);
      function->set_name(std::string_view(name, format_result.size));
      // TODO(benvanik): set type  fn->type = FunctionSymbol::User;
      // TODO(benvanik): set flags fn->flags |= FunctionSymbol::kFlagSaveVmx;
      function->set_behavior(Function::Behavior::kProlog);
      function->set_status(Symbol::Status::kDeclared);
      function->SetSaverest(cpu::SaveRestoreType::VMX, false, n);
      address += 2 * 4;
    }
    address += 4;
    for (int n = 64; n <= 127; n++) {
      auto format_result =
          fmt::format_to_n(name, xe::countof(name), "__savevmx_{}", n);
      Function* function;
      AddXexFunction(address, &function);
      function->set_name(std::string_view(name, format_result.size));
      // TODO(benvanik): set type  fn->type = FunctionSymbol::User;
      // TODO(benvanik): set flags fn->flags |= FunctionSymbol::kFlagSaveVmx;
      function->set_behavior(Function::Behavior::kProlog);
      function->set_status(Symbol::Status::kDeclared);
      function->SetSaverest(cpu::SaveRestoreType::VMX, false, n);
      address += 2 * 4;
    }
    address = vmx_start + (18 * 2 * 4) + (1 * 4) + (64 * 2 * 4) + (1 * 4);
    for (int n = 14; n <= 31; n++) {
      auto format_result =
          fmt::format_to_n(name, xe::countof(name), "__restvmx_{}", n);
      Function* function;
      AddXexFunction(address, &function);
      function->set_name(std::string_view(name, format_result.size));
      // TODO(benvanik): set type  fn->type = FunctionSymbol::User;
      // TODO(benvanik): set flags fn->flags |= FunctionSymbol::kFlagRestVmx;
      function->set_behavior(Function::Behavior::kEpilog);
      function->set_status(Symbol::Status::kDeclared);
      function->SetSaverest(cpu::SaveRestoreType::VMX, true, n);
      address += 2 * 4;
    }
    address += 4;
    for (int n = 64; n <= 127; n++) {
      auto format_result =
          fmt::format_to_n(name, xe::countof(name), "__restvmx_{}", n);
      Function* function;
      AddXexFunction(address, &function);
      function->set_name(std::string_view(name, format_result.size));
      // TODO(benvanik): set type  fn->type = FunctionSymbol::User;
      // TODO(benvanik): set flags fn->flags |= FunctionSymbol::kFlagRestVmx;
      function->set_behavior(Function::Behavior::kEpilog);
      function->set_status(Symbol::Status::kDeclared);
      function->SetSaverest(cpu::SaveRestoreType::VMX, true, n);
      address += 2 * 4;
    }
  }
  if (cvars::enable_early_precompilation) {
    for (auto&& to_ensure_precompiled : resolve_on_exit) {
      // we want to make sure an address for these functions is available before
      // any other functions are compiled for code generation purposes but we do
      // it outside of our loops, because we also want to make sure we've marked
      // up the symbol with info about it being save/rest and whatnot
      processor_->ResolveFunction(to_ensure_precompiled);
    }
  }
  return true;
}

}  // namespace cpu
}  // namespace xe
