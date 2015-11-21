/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/util/xex2.h"

#include <gflags/gflags.h>

#include <algorithm>
#include <cstring>
#include <vector>

#include "third_party/crypto/rijndael-alg-fst.c"
#include "third_party/mspack/lzx.h"
#include "third_party/mspack/lzxd.c"
#include "third_party/mspack/mspack.h"
#include "third_party/pe/pe_image.h"

#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/base/memory.h"
#include "xenia/base/platform.h"

namespace xe {}  // namespace xe

DEFINE_bool(xex_dev_key, false, "Use the devkit key.");

typedef struct xe_xex2 {
  xe::Memory* memory;

  xe_xex2_header_t header;

  std::vector<PESection*>* sections;

  struct {
    size_t count;
    xe_xex2_import_info_t* infos;
  } library_imports[16];
} xe_xex2_t;

int xe_xex2_read_header(const uint8_t* addr, const size_t length,
                        xe_xex2_header_t* header);
int xe_xex2_decrypt_key(xe_xex2_header_t* header);
int xe_xex2_read_image(xe_xex2_ref xex, const uint8_t* xex_addr,
                       const uint32_t xex_length, xe::Memory* memory);
int xe_xex2_load_pe(xe_xex2_ref xex);
int xe_xex2_find_import_infos(xe_xex2_ref xex,
                              const xe_xex2_import_library_t* library);

xe_xex2_ref xe_xex2_load(xe::Memory* memory, const void* addr,
                         const size_t length, xe_xex2_options_t options) {
  xe_xex2_ref xex = (xe_xex2_ref)calloc(1, sizeof(xe_xex2));

  xex->memory = memory;
  xex->sections = new std::vector<PESection*>();

  if (xe_xex2_read_header((const uint8_t*)addr, length, &xex->header)) {
    xe_xex2_dealloc(xex);
    return nullptr;
  }

  if (xe_xex2_decrypt_key(&xex->header)) {
    xe_xex2_dealloc(xex);
    return nullptr;
  }

  if (xe_xex2_read_image(xex, (const uint8_t*)addr, uint32_t(length), memory)) {
    xe_xex2_dealloc(xex);
    return nullptr;
  }

  if (xe_xex2_load_pe(xex)) {
    xe_xex2_dealloc(xex);
    return nullptr;
  }

  for (size_t n = 0; n < xex->header.import_library_count; n++) {
    auto library = &xex->header.import_libraries[n];
    if (xe_xex2_find_import_infos(xex, library)) {
      xe_xex2_dealloc(xex);
      return nullptr;
    }
  }

  return xex;
}

void xe_xex2_dealloc(xe_xex2_ref xex) {
  if (!xex) {
    return;
  }

  for (std::vector<PESection*>::iterator it = xex->sections->begin();
       it != xex->sections->end(); ++it) {
    delete *it;
  }

  xe_xex2_header_t* header = &xex->header;
  free(header->sections);
  free(header->resource_infos);
  if (header->file_format_info.compression_type == XEX_COMPRESSION_BASIC) {
    free(header->file_format_info.compression_info.basic.blocks);
  }
  for (size_t n = 0; n < header->import_library_count; n++) {
    xe_xex2_import_library_t* library = &header->import_libraries[n];
    free(library->records);
  }

  xex->memory = NULL;
}

const xe_xex2_header_t* xe_xex2_get_header(xe_xex2_ref xex) {
  return &xex->header;
}

int xe_xex2_read_header(const uint8_t* addr, const size_t length,
                        xe_xex2_header_t* header) {
  const uint8_t* p = addr;
  const uint8_t* pc;
  const uint8_t* ps;
  xe_xex2_loader_info_t* ldr;

  header->xex2 = xe::load_and_swap<uint32_t>(p + 0x00);
  if (header->xex2 != 'XEX2') {
    return 1;
  }

  header->module_flags =
      (xe_xex2_module_flags)xe::load_and_swap<uint32_t>(p + 0x04);
  header->exe_offset = xe::load_and_swap<uint32_t>(p + 0x08);
  header->unknown0 = xe::load_and_swap<uint32_t>(p + 0x0C);
  header->certificate_offset = xe::load_and_swap<uint32_t>(p + 0x10);
  header->header_count = xe::load_and_swap<uint32_t>(p + 0x14);

  for (size_t n = 0; n < header->header_count; n++) {
    const uint8_t* ph = p + 0x18 + (n * 8);
    const uint32_t key = xe::load_and_swap<uint32_t>(ph + 0x00);
    const uint32_t data_offset = xe::load_and_swap<uint32_t>(ph + 0x04);

    xe_xex2_opt_header_t* opt_header = &header->headers[n];
    opt_header->key = key;
    switch (key & 0xFF) {
      case 0x01:
        // dataOffset = data
        opt_header->length = 0;
        opt_header->value = data_offset;
        break;
      case 0xFF:
        // dataOffset = offset (first dword in data is size)
        opt_header->length = xe::load_and_swap<uint32_t>(p + data_offset);
        opt_header->offset = data_offset;
        break;
      default:
        // dataOffset = size in dwords
        opt_header->length = (key & 0xFF) * 4;
        opt_header->offset = data_offset;
        break;
    }

    const uint8_t* pp = p + opt_header->offset;
    switch (opt_header->key) {
      case XEX_HEADER_SYSTEM_FLAGS:
        header->system_flags = (xe_xex2_system_flags)data_offset;
        break;
      case XEX_HEADER_RESOURCE_INFO: {
        header->resource_info_count = (opt_header->length - 4) / 16;
        header->resource_infos = (xe_xex2_resource_info_t*)calloc(
            header->resource_info_count, sizeof(xe_xex2_resource_info_t));
        const uint8_t* phi = pp + 0x04;
        for (size_t m = 0; m < header->resource_info_count; m++) {
          auto& res = header->resource_infos[m];
          memcpy(res.name, phi + 0x00, 8);
          res.address = xe::load_and_swap<uint32_t>(phi + 0x08);
          res.size = xe::load_and_swap<uint32_t>(phi + 0x0C);
          phi += 16;
        }
      } break;
      case XEX_HEADER_EXECUTION_INFO: {
        xe_xex2_execution_info_t* ex = &header->execution_info;
        ex->media_id = xe::load_and_swap<uint32_t>(pp + 0x00);
        ex->version.value = xe::load_and_swap<uint32_t>(pp + 0x04);
        ex->base_version.value = xe::load_and_swap<uint32_t>(pp + 0x08);
        ex->title_id = xe::load_and_swap<uint32_t>(pp + 0x0C);
        ex->platform = xe::load_and_swap<uint8_t>(pp + 0x10);
        ex->executable_table = xe::load_and_swap<uint8_t>(pp + 0x11);
        ex->disc_number = xe::load_and_swap<uint8_t>(pp + 0x12);
        ex->disc_count = xe::load_and_swap<uint8_t>(pp + 0x13);
        ex->savegame_id = xe::load_and_swap<uint32_t>(pp + 0x14);
      } break;
      case XEX_HEADER_GAME_RATINGS: {
        xe_xex2_game_ratings_t* ratings = &header->game_ratings;
        ratings->esrb =
            (xe_xex2_rating_esrb_value)xe::load_and_swap<uint8_t>(pp + 0x00);
        ratings->pegi =
            (xe_xex2_rating_pegi_value)xe::load_and_swap<uint8_t>(pp + 0x01);
        ratings->pegifi =
            (xe_xex2_rating_pegi_fi_value)xe::load_and_swap<uint8_t>(pp + 0x02);
        ratings->pegipt =
            (xe_xex2_rating_pegi_pt_value)xe::load_and_swap<uint8_t>(pp + 0x03);
        ratings->bbfc =
            (xe_xex2_rating_bbfc_value)xe::load_and_swap<uint8_t>(pp + 0x04);
        ratings->cero =
            (xe_xex2_rating_cero_value)xe::load_and_swap<uint8_t>(pp + 0x05);
        ratings->usk =
            (xe_xex2_rating_usk_value)xe::load_and_swap<uint8_t>(pp + 0x06);
        ratings->oflcau =
            (xe_xex2_rating_oflc_au_value)xe::load_and_swap<uint8_t>(pp + 0x07);
        ratings->oflcnz =
            (xe_xex2_rating_oflc_nz_value)xe::load_and_swap<uint8_t>(pp + 0x08);
        ratings->kmrb =
            (xe_xex2_rating_kmrb_value)xe::load_and_swap<uint8_t>(pp + 0x09);
        ratings->brazil =
            (xe_xex2_rating_brazil_value)xe::load_and_swap<uint8_t>(pp + 0x0A);
        ratings->fpb =
            (xe_xex2_rating_fpb_value)xe::load_and_swap<uint8_t>(pp + 0x0B);
      } break;
      case XEX_HEADER_TLS_INFO: {
        xe_xex2_tls_info_t* tls = &header->tls_info;
        tls->slot_count = xe::load_and_swap<uint32_t>(pp + 0x00);
        tls->raw_data_address = xe::load_and_swap<uint32_t>(pp + 0x04);
        tls->data_size = xe::load_and_swap<uint32_t>(pp + 0x08);
        tls->raw_data_size = xe::load_and_swap<uint32_t>(pp + 0x0C);
      } break;
      case XEX_HEADER_IMAGE_BASE_ADDRESS:
        header->exe_address = opt_header->value;
        break;
      case XEX_HEADER_ENTRY_POINT:
        header->exe_entry_point = opt_header->value;
        break;
      case XEX_HEADER_DEFAULT_STACK_SIZE:
        header->exe_stack_size = opt_header->value;
        break;
      case XEX_HEADER_DEFAULT_HEAP_SIZE:
        header->exe_heap_size = opt_header->value;
        break;
      case XEX_HEADER_EXPORTS_BY_NAME: {
        // IMAGE_DATA_DIRECTORY (w/ offset from PE file base)
        header->pe_export_table_offset = xe::load_and_swap<uint32_t>(pp);
        // size = xe::load_and_swap<uint32_t>(pp + 0x04);
      } break;
      case XEX_HEADER_IMPORT_LIBRARIES: {
        auto import_libraries =
            reinterpret_cast<const xe::xex2_opt_import_libraries*>(pp);

        const uint32_t max_count =
            (uint32_t)xe::countof(header->import_libraries);
        uint32_t count = import_libraries->library_count;
        assert_true(count <= max_count);
        if (count > max_count) {
          XELOGW("ignoring %zu extra entries in XEX_HEADER_IMPORT_LIBRARIES",
                 (max_count - import_libraries->library_count));
          count = max_count;
        }
        header->import_library_count = count;

        uint32_t string_table_size = import_libraries->string_table_size;
        const char* string_table[32];  // Pretend 32 is max_count
        std::memset(string_table, 0, sizeof(string_table));

        // Parse the string table
        for (size_t i = 0, j = 0; i < string_table_size; j++) {
          const char* str = import_libraries->string_table + i;

          string_table[j] = str;
          i += std::strlen(str) + 1;

          // Padding
          if ((i % 4) != 0) {
            i += 4 - (i % 4);
          }
        }

        pp += 12 + import_libraries->string_table_size;
        for (size_t m = 0; m < count; m++) {
          xe_xex2_import_library_t* library = &header->import_libraries[m];
          auto src_library = (xe::xex2_import_library*)pp;

          memcpy(library->digest, pp + 0x04, 20);
          library->import_id = src_library->id;
          library->version.value = src_library->version.value;
          library->min_version.value = src_library->version_min.value;

          std::strncpy(library->name, string_table[src_library->name_index],
                       xe::countof(library->name));

          library->record_count = src_library->count;
          library->records =
              (uint32_t*)calloc(library->record_count, sizeof(uint32_t));
          if (!library->records) {
            return 1;
          }
          for (size_t i = 0; i < library->record_count; i++) {
            library->records[i] = src_library->import_table[i];
          }

          pp += src_library->size;
        }
      } break;
      case XEX_HEADER_STATIC_LIBRARIES: {
        const size_t max_count = xe::countof(header->static_libraries);
        size_t count = (opt_header->length - 4) / 16;
        assert_true(count <= max_count);
        if (count > max_count) {
          XELOGW("ignoring %zu extra entries in XEX_HEADER_STATIC_LIBRARIES",
                 (max_count - count));
          count = max_count;
        }
        header->static_library_count = count;
        pp += 4;
        for (size_t m = 0; m < count; m++) {
          xe_xex2_static_library_t* library = &header->static_libraries[m];
          memcpy(library->name, pp + 0x00, 8);
          library->name[8] = 0;
          library->major = xe::load_and_swap<uint16_t>(pp + 0x08);
          library->minor = xe::load_and_swap<uint16_t>(pp + 0x0A);
          library->build = xe::load_and_swap<uint16_t>(pp + 0x0C);
          uint16_t qfeapproval = xe::load_and_swap<uint16_t>(pp + 0x0E);
          library->approval = (xe_xex2_approval_type)(qfeapproval & 0x8000);
          library->qfe = qfeapproval & ~0x8000;
          pp += 16;
        }
      } break;
      case XEX_HEADER_FILE_FORMAT_INFO: {
        xe_xex2_file_format_info_t* fmt = &header->file_format_info;
        fmt->encryption_type =
            (xe_xex2_encryption_type)xe::load_and_swap<uint16_t>(pp + 0x04);
        fmt->compression_type =
            (xe_xex2_compression_type)xe::load_and_swap<uint16_t>(pp + 0x06);
        switch (fmt->compression_type) {
          case XEX_COMPRESSION_NONE:
            // TODO: XEX_COMPRESSION_NONE
            assert_always();
            break;
          case XEX_COMPRESSION_BASIC: {
            xe_xex2_file_basic_compression_info_t* comp_info =
                &fmt->compression_info.basic;
            uint32_t info_size = xe::load_and_swap<uint32_t>(pp + 0x00);
            comp_info->block_count = (info_size - 8) / 8;
            comp_info->blocks = (xe_xex2_file_basic_compression_block_t*)calloc(
                comp_info->block_count,
                sizeof(xe_xex2_file_basic_compression_block_t));
            if (!comp_info->blocks) {
              return 1;
            }
            for (size_t m = 0; m < comp_info->block_count; m++) {
              xe_xex2_file_basic_compression_block_t* block =
                  &comp_info->blocks[m];
              block->data_size =
                  xe::load_and_swap<uint32_t>(pp + 0x08 + (m * 8));
              block->zero_size =
                  xe::load_and_swap<uint32_t>(pp + 0x0C + (m * 8));
            }
          } break;
          case XEX_COMPRESSION_NORMAL: {
            xe_xex2_file_normal_compression_info_t* comp_info =
                &fmt->compression_info.normal;
            uint32_t window_size = xe::load_and_swap<uint32_t>(pp + 0x08);
            uint32_t window_bits = 0;
            for (size_t m = 0; m < 32; m++, window_bits++) {
              window_size >>= 1;
              if (window_size == 0x00000000) {
                break;
              }
            }
            comp_info->window_size = xe::load_and_swap<uint32_t>(pp + 0x08);
            comp_info->window_bits = window_bits;
            comp_info->block_size = xe::load_and_swap<uint32_t>(pp + 0x0C);
            memcpy(comp_info->block_hash, pp + 0x10, 20);
          } break;
          case XEX_COMPRESSION_DELTA:
            // TODO: XEX_COMPRESSION_DELTA
            assert_always();
            break;
        }
      } break;
    }
  }

  // Loader info.
  pc = p + header->certificate_offset;
  ldr = &header->loader_info;
  ldr->header_size = xe::load_and_swap<uint32_t>(pc + 0x000);
  ldr->image_size = xe::load_and_swap<uint32_t>(pc + 0x004);
  memcpy(ldr->rsa_signature, pc + 0x008, 256);
  ldr->unklength = xe::load_and_swap<uint32_t>(pc + 0x108);
  ldr->image_flags =
      (xe_xex2_image_flags)xe::load_and_swap<uint32_t>(pc + 0x10C);
  ldr->load_address = xe::load_and_swap<uint32_t>(pc + 0x110);
  memcpy(ldr->section_digest, pc + 0x114, 20);
  ldr->import_table_count = xe::load_and_swap<uint32_t>(pc + 0x128);
  memcpy(ldr->import_table_digest, pc + 0x12C, 20);
  memcpy(ldr->media_id, pc + 0x140, 16);
  memcpy(ldr->file_key, pc + 0x150, 16);
  ldr->export_table = xe::load_and_swap<uint32_t>(pc + 0x160);
  memcpy(ldr->header_digest, pc + 0x164, 20);
  ldr->game_regions =
      (xe_xex2_region_flags)xe::load_and_swap<uint32_t>(pc + 0x178);
  ldr->media_flags =
      (xe_xex2_media_flags)xe::load_and_swap<uint32_t>(pc + 0x17C);

  // Section info follows loader info.
  ps = p + header->certificate_offset + 0x180;
  header->section_count = xe::load_and_swap<uint32_t>(ps + 0x000);
  ps += 4;
  header->sections = (xe_xex2_section_t*)calloc(header->section_count,
                                                sizeof(xe_xex2_section_t));
  if (!header->sections) {
    return 1;
  }
  for (size_t n = 0; n < header->section_count; n++) {
    xe_xex2_section_t* section = &header->sections[n];
    section->page_size =
        header->exe_address <= 0x90000000 ? 64 * 1024 : 4 * 1024;
    section->info.value = xe::load_and_swap<uint32_t>(ps);
    ps += 4;
    memcpy(section->digest, ps, sizeof(section->digest));
    ps += sizeof(section->digest);
  }

  return 0;
}

int xe_xex2_decrypt_key(xe_xex2_header_t* header) {
  static const uint8_t xe_xex2_retail_key[16] = {
      0x20, 0xB1, 0x85, 0xA5, 0x9D, 0x28, 0xFD, 0xC3,
      0x40, 0x58, 0x3F, 0xBB, 0x08, 0x96, 0xBF, 0x91};
  static const uint8_t xe_xex2_devkit_key[16] = {
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

  // Guess key based on file info.
  // TODO: better way to finding out which key to use?
  const uint8_t* xexkey;
  if (header->execution_info.title_id && !FLAGS_xex_dev_key) {
    xexkey = xe_xex2_retail_key;
  } else {
    xexkey = xe_xex2_devkit_key;
  }

  // Decrypt the header key.
  uint32_t rk[4 * (MAXNR + 1)];
  int32_t Nr = rijndaelKeySetupDec(rk, xexkey, 128);
  rijndaelDecrypt(rk, Nr, header->loader_info.file_key, header->session_key);

  return 0;
}

typedef struct mspack_memory_file_t {
  struct mspack_system sys;
  void* buffer;
  off_t buffer_size;
  off_t offset;
} mspack_memory_file;
mspack_memory_file* mspack_memory_open(struct mspack_system* sys, void* buffer,
                                       const size_t buffer_size) {
  assert_true(buffer_size < INT_MAX);
  if (buffer_size >= INT_MAX) {
    return NULL;
  }
  mspack_memory_file* memfile =
      (mspack_memory_file*)calloc(1, sizeof(mspack_memory_file));
  if (!memfile) {
    return NULL;
  }
  memfile->buffer = buffer;
  memfile->buffer_size = (off_t)buffer_size;
  memfile->offset = 0;
  return memfile;
}
void mspack_memory_close(mspack_memory_file* file) {
  mspack_memory_file* memfile = (mspack_memory_file*)file;
  free(memfile);
}
int mspack_memory_read(struct mspack_file* file, void* buffer, int chars) {
  mspack_memory_file* memfile = (mspack_memory_file*)file;
  const off_t remaining = memfile->buffer_size - memfile->offset;
  const off_t total = std::min(static_cast<off_t>(chars), remaining);
  memcpy(buffer, (uint8_t*)memfile->buffer + memfile->offset, total);
  memfile->offset += total;
  return (int)total;
}
int mspack_memory_write(struct mspack_file* file, void* buffer, int chars) {
  mspack_memory_file* memfile = (mspack_memory_file*)file;
  const off_t remaining = memfile->buffer_size - memfile->offset;
  const off_t total = std::min(static_cast<off_t>(chars), remaining);
  memcpy((uint8_t*)memfile->buffer + memfile->offset, buffer, total);
  memfile->offset += total;
  return (int)total;
}
void* mspack_memory_alloc(struct mspack_system* sys, size_t chars) {
  return calloc(chars, 1);
}
void mspack_memory_free(void* ptr) { free(ptr); }
void mspack_memory_copy(void* src, void* dest, size_t chars) {
  memcpy(dest, src, chars);
}
struct mspack_system* mspack_memory_sys_create() {
  struct mspack_system* sys =
      (struct mspack_system*)calloc(1, sizeof(struct mspack_system));
  if (!sys) {
    return NULL;
  }
  sys->read = mspack_memory_read;
  sys->write = mspack_memory_write;
  sys->alloc = mspack_memory_alloc;
  sys->free = mspack_memory_free;
  sys->copy = mspack_memory_copy;
  return sys;
}
void mspack_memory_sys_destroy(struct mspack_system* sys) { free(sys); }

void xe_xex2_decrypt_buffer(const uint8_t* session_key,
                            const uint8_t* input_buffer,
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

int xe_xex2_read_image_uncompressed(const xe_xex2_header_t* header,
                                    const uint8_t* xex_addr,
                                    const uint32_t xex_length,
                                    xe::Memory* memory) {
  // Allocate in-place the XEX memory.
  const uint32_t exe_length = xex_length - header->exe_offset;
  uint32_t uncompressed_size = exe_length;
  bool alloc_result =
      memory->LookupHeap(header->exe_address)
          ->AllocFixed(
              header->exe_address, uncompressed_size, 4096,
              xe::kMemoryAllocationReserve | xe::kMemoryAllocationCommit,
              xe::kMemoryProtectRead | xe::kMemoryProtectWrite);
  if (!alloc_result) {
    XELOGE("Unable to allocate XEX memory at %.8X-%.8X.", header->exe_address,
           uncompressed_size);
    return 2;
  }
  uint8_t* buffer = memory->TranslateVirtual(header->exe_address);
  std::memset(buffer, 0, uncompressed_size);

  const uint8_t* p = (const uint8_t*)xex_addr + header->exe_offset;

  switch (header->file_format_info.encryption_type) {
    case XEX_ENCRYPTION_NONE:
      if (exe_length > uncompressed_size) {
        return 1;
      }
      memcpy(buffer, p, exe_length);
      return 0;
    case XEX_ENCRYPTION_NORMAL:
      xe_xex2_decrypt_buffer(header->session_key, p, exe_length, buffer,
                             uncompressed_size);
      return 0;
    default:
      assert_always();
      return 1;
  }

  return 0;
}

int xe_xex2_read_image_basic_compressed(const xe_xex2_header_t* header,
                                        const uint8_t* xex_addr,
                                        const uint32_t xex_length,
                                        xe::Memory* memory) {
  const uint32_t exe_length = xex_length - header->exe_offset;
  const uint8_t* source_buffer = (const uint8_t*)xex_addr + header->exe_offset;
  const uint8_t* p = source_buffer;

  // Calculate uncompressed length.
  uint32_t uncompressed_size = 0;
  const xe_xex2_file_basic_compression_info_t* comp_info =
      &header->file_format_info.compression_info.basic;
  for (uint32_t n = 0; n < comp_info->block_count; n++) {
    const uint32_t data_size = comp_info->blocks[n].data_size;
    const uint32_t zero_size = comp_info->blocks[n].zero_size;
    uncompressed_size += data_size + zero_size;
  }

  // Calculate the total size of the XEX image from its headers.
  uint32_t total_size = 0;
  for (uint32_t i = 0; i < header->section_count; i++) {
    xe_xex2_section_t& section = header->sections[i];

    total_size += section.info.page_count * section.page_size;
  }

  // Allocate in-place the XEX memory.
  bool alloc_result =
      memory->LookupHeap(header->exe_address)
          ->AllocFixed(
              header->exe_address, total_size, 4096,
              xe::kMemoryAllocationReserve | xe::kMemoryAllocationCommit,
              xe::kMemoryProtectRead | xe::kMemoryProtectWrite);
  if (!alloc_result) {
    XELOGE("Unable to allocate XEX memory at %.8X-%.8X.", header->exe_address,
           uncompressed_size);
    return 1;
  }
  uint8_t* buffer = memory->TranslateVirtual(header->exe_address);
  std::memset(buffer, 0, total_size);  // Quickly zero the contents.
  uint8_t* d = buffer;

  uint32_t rk[4 * (MAXNR + 1)];
  uint8_t ivec[16] = {0};
  int32_t Nr = rijndaelKeySetupDec(rk, header->session_key, 128);

  for (size_t n = 0; n < comp_info->block_count; n++) {
    const uint32_t data_size = comp_info->blocks[n].data_size;
    const uint32_t zero_size = comp_info->blocks[n].zero_size;

    switch (header->file_format_info.encryption_type) {
      case XEX_ENCRYPTION_NONE:
        if (exe_length - (p - source_buffer) >
            uncompressed_size - (d - buffer)) {
          // Overflow.
          return 1;
        }
        memcpy(d, p, exe_length - (p - source_buffer));
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

int xe_xex2_read_image_compressed(const xe_xex2_header_t* header,
                                  const uint8_t* xex_addr,
                                  const uint32_t xex_length,
                                  xe::Memory* memory) {
  const uint32_t exe_length = xex_length - header->exe_offset;
  const uint8_t* exe_buffer = (const uint8_t*)xex_addr + header->exe_offset;

  // src -> dest:
  // - decrypt (if encrypted)
  // - de-block:
  //    4b total size of next block in uint8_ts
  //   20b hash of entire next block (including size/hash)
  //    Nb block uint8_ts
  // - decompress block contents

  int result_code = 1;

  uint8_t* compress_buffer = NULL;
  const uint8_t* p = NULL;
  uint8_t* d = NULL;
  uint8_t* deblock_buffer = NULL;
  size_t block_size = 0;
  uint32_t uncompressed_size = 0;
  struct mspack_system* sys = NULL;
  mspack_memory_file* lzxsrc = NULL;
  mspack_memory_file* lzxdst = NULL;
  struct lzxd_stream* lzxd = NULL;

  // Decrypt (if needed).
  bool free_input = false;
  const uint8_t* input_buffer = exe_buffer;
  const size_t input_size = exe_length;
  switch (header->file_format_info.encryption_type) {
    case XEX_ENCRYPTION_NONE:
      // No-op.
      break;
    case XEX_ENCRYPTION_NORMAL:
      // TODO: a way to do without a copy/alloc?
      free_input = true;
      input_buffer = (const uint8_t*)calloc(1, input_size);
      xe_xex2_decrypt_buffer(header->session_key, exe_buffer, exe_length,
                             (uint8_t*)input_buffer, input_size);
      break;
    default:
      assert_always();
      return false;
  }

  compress_buffer = (uint8_t*)calloc(1, exe_length);

  p = input_buffer;
  d = compress_buffer;

  // De-block.
  deblock_buffer = (uint8_t*)calloc(1, input_size);
  block_size = header->file_format_info.compression_info.normal.block_size;
  while (block_size) {
    const uint8_t* pnext = p + block_size;
    const size_t next_size = xe::load_and_swap<int32_t>(p);
    p += 4;
    p += 20;  // skip 20b hash

    while (true) {
      const size_t chunk_size = (p[0] << 8) | p[1];
      p += 2;
      if (!chunk_size) {
        break;
      }
      memcpy(d, p, chunk_size);
      p += chunk_size;
      d += chunk_size;

      uncompressed_size += 0x8000;
    }

    p = pnext;
    block_size = next_size;
  }

  // Allocate in-place the XEX memory.
  bool alloc_result =
      memory->LookupHeap(header->exe_address)
          ->AllocFixed(
              header->exe_address, uncompressed_size, 4096,
              xe::kMemoryAllocationReserve | xe::kMemoryAllocationCommit,
              xe::kMemoryProtectRead | xe::kMemoryProtectWrite);
  if (!alloc_result) {
    XELOGE("Unable to allocate XEX memory at %.8X-%.8X.", header->exe_address,
           uncompressed_size);
    result_code = 2;
    // Not doing any cleanup here;
    // TODO(benvanik): rewrite this entire file using RAII.
    assert_always();
    return 1;
  }
  uint8_t* buffer = memory->TranslateVirtual(header->exe_address);
  std::memset(buffer, 0, uncompressed_size);

  // Setup decompressor and decompress.
  sys = mspack_memory_sys_create();
  lzxsrc = mspack_memory_open(sys, (void*)compress_buffer, d - compress_buffer);
  lzxdst = mspack_memory_open(sys, buffer, uncompressed_size);
  lzxd =
      lzxd_init(sys, (struct mspack_file*)lzxsrc, (struct mspack_file*)lzxdst,
                header->file_format_info.compression_info.normal.window_bits, 0,
                32768, (off_t)header->loader_info.image_size);
  result_code = lzxd_decompress(lzxd, (off_t)header->loader_info.image_size);

  if (lzxd) {
    lzxd_free(lzxd);
    lzxd = NULL;
  }
  if (lzxsrc) {
    mspack_memory_close(lzxsrc);
    lzxsrc = NULL;
  }
  if (lzxdst) {
    mspack_memory_close(lzxdst);
    lzxdst = NULL;
  }
  if (sys) {
    mspack_memory_sys_destroy(sys);
    sys = NULL;
  }
  free(compress_buffer);
  free(deblock_buffer);
  if (free_input) {
    free((void*)input_buffer);
  }
  return result_code;
}

int xe_xex2_read_image(xe_xex2_ref xex, const uint8_t* xex_addr,
                       const uint32_t xex_length, xe::Memory* memory) {
  const xe_xex2_header_t* header = &xex->header;
  switch (header->file_format_info.compression_type) {
    case XEX_COMPRESSION_NONE:
      return xe_xex2_read_image_uncompressed(header, xex_addr, xex_length,
                                             memory);
    case XEX_COMPRESSION_BASIC:
      return xe_xex2_read_image_basic_compressed(header, xex_addr, xex_length,
                                                 memory);
    case XEX_COMPRESSION_NORMAL:
      return xe_xex2_read_image_compressed(header, xex_addr, xex_length,
                                           memory);
    default:
      assert_always();
      return 1;
  }
}

int xe_xex2_load_pe(xe_xex2_ref xex) {
  const xe_xex2_header_t* header = &xex->header;
  const uint8_t* p = xex->memory->TranslateVirtual(header->exe_address);

  // Verify DOS signature (MZ).
  const IMAGE_DOS_HEADER* doshdr = (const IMAGE_DOS_HEADER*)p;
  if (doshdr->e_magic != IMAGE_DOS_SIGNATURE) {
    XELOGE("PE signature mismatch; likely bad decryption/decompression");
    return 1;
  }

  // Move to the NT header offset from the DOS header.
  p += doshdr->e_lfanew;

  // Verify NT signature (PE\0\0).
  const IMAGE_NT_HEADERS32* nthdr = (const IMAGE_NT_HEADERS32*)(p);
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

  // Quick scan to determine bounds of sections.
  size_t upper_address = 0;
  const IMAGE_SECTION_HEADER* sechdr = IMAGE_FIRST_SECTION(nthdr);
  for (size_t n = 0; n < filehdr->NumberOfSections; n++, sechdr++) {
    const size_t physical_address = opthdr->ImageBase + sechdr->VirtualAddress;
    upper_address =
        std::max(upper_address, physical_address + sechdr->Misc.VirtualSize);
  }

  // Setup/load sections.
  sechdr = IMAGE_FIRST_SECTION(nthdr);
  for (size_t n = 0; n < filehdr->NumberOfSections; n++, sechdr++) {
    PESection* section = (PESection*)calloc(1, sizeof(PESection));
    memcpy(section->name, sechdr->Name, sizeof(sechdr->Name));
    section->name[8] = 0;
    section->raw_address = sechdr->PointerToRawData;
    section->raw_size = sechdr->SizeOfRawData;
    section->address = header->exe_address + sechdr->VirtualAddress;
    section->size = sechdr->Misc.VirtualSize;
    section->flags = sechdr->Characteristics;
    xex->sections->push_back(section);
  }

  // DumpTLSDirectory(pImageBase, pNTHeader, (PIMAGE_TLS_DIRECTORY32)0);
  // DumpExportsSection(pImageBase, pNTHeader);
  return 0;
}

const PESection* xe_xex2_get_pe_section(xe_xex2_ref xex, const char* name) {
  for (std::vector<PESection*>::iterator it = xex->sections->begin();
       it != xex->sections->end(); ++it) {
    if (!strcmp((*it)->name, name)) {
      return *it;
    }
  }
  return NULL;
}

int xe_xex2_find_import_infos(xe_xex2_ref xex,
                              const xe_xex2_import_library_t* library) {
  auto header = xe_xex2_get_header(xex);

  // Find library index for verification.
  size_t library_index = ~0ull;
  for (size_t n = 0; n < header->import_library_count; n++) {
    if (&header->import_libraries[n] == library) {
      library_index = n;
      break;
    }
  }
  assert_true(library_index != (size_t)-1);

  // Records:
  // The number of records does not correspond to the number of imports!
  // Each record points at either a location in text or data - dereferencing the
  // pointer will yield a value that & 0xFFFF = the import ordinal,
  // >> 16 & 0xFF = import library index, and >> 24 & 0xFF = 0 if a variable
  // (just get address) or 1 if a thunk (needs rewrite).

  // Calculate real count.
  size_t info_count = 0;
  for (size_t n = 0; n < library->record_count; n++) {
    const uint32_t record = library->records[n];
    const uint32_t value =
        xe::load_and_swap<uint32_t>(xex->memory->TranslateVirtual(record));
    if (value & 0xFF000000) {
      // Thunk for previous record - ignore.
    } else {
      // Variable/thunk.
      info_count++;
    }
  }

  // Allocate storage.
  xe_xex2_import_info_t* infos =
      (xe_xex2_import_info_t*)calloc(info_count, sizeof(xe_xex2_import_info_t));
  assert_not_null(infos);

  assert_not_zero(info_count);

  // Construct infos.
  for (size_t n = 0, i = 0; n < library->record_count; n++) {
    const uint32_t record = library->records[n];
    const uint32_t value =
        xe::load_and_swap<uint32_t>(xex->memory->TranslateVirtual(record));
    const uint32_t type = (value & 0xFF000000) >> 24;

    // Verify library index matches given library.
    // assert_true(library_index == ((value >> 16) & 0xFF));

    switch (type) {
      case 0x00: {
        xe_xex2_import_info_t* info = &infos[i++];
        info->ordinal = value & 0xFFFF;
        info->value_address = record;
      } break;
      case 0x01: {
        // Thunk for previous record.
        assert_true(i > 0);
        xe_xex2_import_info_t* info = &infos[i - 1];
        assert_true(info->ordinal == (value & 0xFFFF));
        info->thunk_address = record;
      } break;
      default:
        assert_always();
        break;
    }
  }

  xex->library_imports[library_index].count = info_count;
  xex->library_imports[library_index].infos = infos;
  return 0;
}

int xe_xex2_get_import_infos(xe_xex2_ref xex,
                             const xe_xex2_import_library_t* library,
                             xe_xex2_import_info_t** out_import_infos,
                             size_t* out_import_info_count) {
  auto header = xe_xex2_get_header(xex);

  // Find library index for verification.
  size_t library_index = ~0ull;
  for (size_t n = 0; n < header->import_library_count; n++) {
    if (&header->import_libraries[n] == library) {
      library_index = n;
      break;
    }
  }
  if (library_index == (size_t)-1) {
    return 1;
  }

  *out_import_info_count = xex->library_imports[library_index].count;
  *out_import_infos = xex->library_imports[library_index].infos;
  return 0;
}

uint32_t xe_xex2_lookup_export(xe_xex2_ref xex, const char* name) {
  auto header = xe_xex2_get_header(xex);

  // No exports.
  if (!header->pe_export_table_offset) {
    XELOGE("xe_xex2_lookup_export(%s) failed: no PE export table", name);
    return 0;
  }

  auto e = reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*>(
      xex->memory->TranslateVirtual(header->exe_address +
                                    header->pe_export_table_offset));

  // e->AddressOfX RVAs are relative to the IMAGE_EXPORT_DIRECTORY!
  uint32_t* function_table = (uint32_t*)((uint64_t)e + e->AddressOfFunctions);

  // Names relative to directory.
  uint32_t* name_table = (uint32_t*)((uint64_t)e + e->AddressOfNames);

  // Table of ordinals (by name).
  uint16_t* ordinal_table = (uint16_t*)((uint64_t)e + e->AddressOfNameOrdinals);

  // Module name (sometimes).
  // const char* mod_name = (const char*)((uint64_t)e + e->Name);

  for (uint32_t i = 0; i < e->NumberOfNames; i++) {
    const char* fn_name = (const char*)((uint64_t)e + name_table[i]);
    uint16_t ordinal = ordinal_table[i];
    uint32_t addr = header->exe_address + function_table[ordinal];

    if (!strcmp(name, fn_name)) {
      // We have a match!
      return addr;
    }
  }

  // No match.
  return 0;
}

uint32_t xe_xex2_lookup_export(xe_xex2_ref xex, uint16_t ordinal) {
  auto header = xe_xex2_get_header(xex);

  // XEX-style export table.
  if (header->loader_info.export_table) {
    auto export_table = reinterpret_cast<const xe::xex2_export_table*>(
        xex->memory->TranslateVirtual(header->loader_info.export_table));
    uint32_t ordinal_count = export_table->count;
    uint32_t ordinal_base = export_table->base;
    if (ordinal > ordinal_count) {
      XELOGE("xe_xex2_lookup_export: ordinal out of bounds");
      return 0;
    }
    uint32_t i = ordinal - ordinal_base;
    uint32_t ordinal_offset = export_table->ordOffset[i];
    ordinal_offset += export_table->imagebaseaddr << 16;
    return ordinal_offset;
  }

  // Check for PE-style export table.
  if (!header->pe_export_table_offset) {
    XELOGE("xe_xex2_lookup_export(%.4X) failed: no XEX or PE export table");
    return 0;
  }

  auto e = reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*>(
      xex->memory->TranslateVirtual(header->exe_address +
                                    header->pe_export_table_offset));

  // e->AddressOfX RVAs are relative to the IMAGE_EXPORT_DIRECTORY!
  // Functions relative to base.
  uint32_t* function_table = (uint32_t*)((uint64_t)e + e->AddressOfFunctions);
  if (ordinal < e->NumberOfFunctions) {
    return header->exe_address + function_table[ordinal];
  }

  // No match.
  return 0;
}
