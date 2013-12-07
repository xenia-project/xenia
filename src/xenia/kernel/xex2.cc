/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/kernel/xex2.h>

#include <vector>

#include <third_party/crypto/rijndael-alg-fst.h>
#include <third_party/crypto/rijndael-alg-fst.c>
#include <third_party/mspack/lzx.h>
#include <third_party/mspack/lzxd.c>
#include <third_party/mspack/mspack.h>
#include <third_party/pe/pe_image.h>

using namespace alloy;


typedef struct xe_xex2 {
  xe_ref_t ref;

  Memory*           memory;

  xe_xex2_header_t  header;

  std::vector<PESection*>* sections;
} xe_xex2_t;


int xe_xex2_read_header(const uint8_t *addr, const size_t length,
                        xe_xex2_header_t *header);
int xe_xex2_decrypt_key(xe_xex2_header_t *header);
int xe_xex2_read_image(xe_xex2_ref xex,
                       const uint8_t *xex_addr, const size_t xex_length,
                       Memory* memory);
int xe_xex2_load_pe(xe_xex2_ref xex);


xe_xex2_ref xe_xex2_load(Memory* memory,
                         const void* addr, const size_t length,
                         xe_xex2_options_t options) {
  xe_xex2_ref xex = (xe_xex2_ref)xe_calloc(sizeof(xe_xex2));
  xe_ref_init((xe_ref)xex);

  xex->memory = memory;
  xex->sections = new std::vector<PESection*>();

  XEEXPECTZERO(xe_xex2_read_header((const uint8_t*)addr, length, &xex->header));

  XEEXPECTZERO(xe_xex2_decrypt_key(&xex->header));

  XEEXPECTZERO(xe_xex2_read_image(xex, (const uint8_t*)addr, length, memory));

  XEEXPECTZERO(xe_xex2_load_pe(xex));

  return xex;

XECLEANUP:
  xe_xex2_release(xex);
  return NULL;
}

void xe_xex2_dealloc(xe_xex2_ref xex) {
  for (std::vector<PESection*>::iterator it = xex->sections->begin();
       it != xex->sections->end(); ++it) {
    delete *it;
  }

  xe_xex2_header_t *header = &xex->header;
  xe_free(header->sections);
  if (header->file_format_info.compression_type == XEX_COMPRESSION_BASIC) {
    xe_free(header->file_format_info.compression_info.basic.blocks);
  }
  for (size_t n = 0; n < header->import_library_count; n++) {
    xe_xex2_import_library_t *library = &header->import_libraries[n];
    xe_free(library->records);
  }

  xex->memory = NULL;
}

xe_xex2_ref xe_xex2_retain(xe_xex2_ref xex) {
  xe_ref_retain((xe_ref)xex);
  return xex;
}

void xe_xex2_release(xe_xex2_ref xex) {
  xe_ref_release((xe_ref)xex, (xe_ref_dealloc_t)xe_xex2_dealloc);
}

const xechar_t* xe_xex2_get_name(xe_xex2_ref xex) {
  // TODO(benvanik): get name.
  return NULL;
}

const xe_xex2_header_t* xe_xex2_get_header(xe_xex2_ref xex) {
  return &xex->header;
}

int xe_xex2_read_header(const uint8_t *addr, const size_t length,
                        xe_xex2_header_t *header) {
  const uint8_t *p = addr;
  const uint8_t *pc;
  const uint8_t *ps;
  xe_xex2_loader_info_t *ldr;

  header->xex2 = XEGETUINT32BE(p + 0x00);
  if (header->xex2 != 0x58455832) {
    return 1;
  }

  header->module_flags        = (xe_xex2_module_flags)XEGETUINT32BE(p + 0x04);
  header->exe_offset          = XEGETUINT32BE(p + 0x08);
  header->unknown0            = XEGETUINT32BE(p + 0x0C);
  header->certificate_offset  = XEGETUINT32BE(p + 0x10);
  header->header_count        = XEGETUINT32BE(p + 0x14);

  for (size_t n = 0; n < header->header_count; n++) {
    const uint8_t *ph = p + 0x18 + (n * 8);
    const uint32_t key = XEGETUINT32BE(ph + 0x00);
    const uint32_t data_offset = XEGETUINT32BE(ph + 0x04);

    xe_xex2_opt_header_t *opt_header = &header->headers[n];
    opt_header->key = key;
    switch (key & 0xFF) {
    case 0x01:
      // dataOffset = data
      opt_header->length  = 0;
      opt_header->value   = data_offset;
      break;
    case 0xFF:
      // dataOffset = offset (first dword in data is size)
      opt_header->length  = XEGETUINT32BE(p + data_offset);
      opt_header->offset  = data_offset;
      break;
    default:
      // dataOffset = size in dwords
      opt_header->length  = (key & 0xFF) * 4;
      opt_header->offset  = data_offset;
      break;
    }

    const uint8_t *pp = p + opt_header->offset;
    switch (opt_header->key) {
    case XEX_HEADER_SYSTEM_FLAGS:
      header->system_flags = (xe_xex2_system_flags)data_offset;
      break;
    case XEX_HEADER_RESOURCE_INFO:
      {
        xe_xex2_resource_info_t *res = &header->resource_info;
        XEEXPECTZERO(xe_copy_memory(res->title_id,
                                    sizeof(res->title_id), pp + 0x04, 8));
        res->address            = XEGETUINT32BE(pp + 0x0C);
        res->size               = XEGETUINT32BE(pp + 0x10);
        if ((opt_header->length - 4) / 16 > 1) {
          // Ignoring extra resources (not yet seen)
          XELOGW("ignoring extra XEX_HEADER_RESOURCE_INFO resources");
        }
      }
      break;
    case XEX_HEADER_EXECUTION_INFO:
      {
        xe_xex2_execution_info_t *ex = &header->execution_info;
        ex->media_id            = XEGETUINT32BE(pp + 0x00);
        ex->version.value       = XEGETUINT32BE(pp + 0x04);
        ex->base_version.value  = XEGETUINT32BE(pp + 0x08);
        ex->title_id            = XEGETUINT32BE(pp + 0x0C);
        ex->platform            = XEGETUINT8BE(pp + 0x10);
        ex->executable_table    = XEGETUINT8BE(pp + 0x11);
        ex->disc_number         = XEGETUINT8BE(pp + 0x12);
        ex->disc_count          = XEGETUINT8BE(pp + 0x13);
        ex->savegame_id         = XEGETUINT32BE(pp + 0x14);
      }
      break;
    case XEX_HEADER_GAME_RATINGS:
      {
        xe_xex2_game_ratings_t *ratings = &header->game_ratings;
        ratings->esrb   = (xe_xex2_rating_esrb_value)XEGETUINT8BE(pp + 0x00);
        ratings->pegi   = (xe_xex2_rating_pegi_value)XEGETUINT8BE(pp + 0x01);
        ratings->pegifi = (xe_xex2_rating_pegi_fi_value)XEGETUINT8BE(pp + 0x02);
        ratings->pegipt = (xe_xex2_rating_pegi_pt_value)XEGETUINT8BE(pp + 0x03);
        ratings->bbfc   = (xe_xex2_rating_bbfc_value)XEGETUINT8BE(pp + 0x04);
        ratings->cero   = (xe_xex2_rating_cero_value)XEGETUINT8BE(pp + 0x05);
        ratings->usk    = (xe_xex2_rating_usk_value)XEGETUINT8BE(pp + 0x06);
        ratings->oflcau = (xe_xex2_rating_oflc_au_value)XEGETUINT8BE(pp + 0x07);
        ratings->oflcnz = (xe_xex2_rating_oflc_nz_value)XEGETUINT8BE(pp + 0x08);
        ratings->kmrb   = (xe_xex2_rating_kmrb_value)XEGETUINT8BE(pp + 0x09);
        ratings->brazil = (xe_xex2_rating_brazil_value)XEGETUINT8BE(pp + 0x0A);
        ratings->fpb    = (xe_xex2_rating_fpb_value)XEGETUINT8BE(pp + 0x0B);
      }
      break;
    case XEX_HEADER_TLS_INFO:
      {
        xe_xex2_tls_info_t *tls = &header->tls_info;
        tls->slot_count           = XEGETUINT32BE(pp + 0x00);
        tls->raw_data_address     = XEGETUINT32BE(pp + 0x04);
        tls->data_size            = XEGETUINT32BE(pp + 0x08);
        tls->raw_data_size        = XEGETUINT32BE(pp + 0x0C);
      }
      break;
    case XEX_HEADER_IMAGE_BASE_ADDRESS:
      header->exe_address         = opt_header->value;
      break;
    case XEX_HEADER_ENTRY_POINT:
      header->exe_entry_point     = opt_header->value;
      break;
    case XEX_HEADER_DEFAULT_STACK_SIZE:
      header->exe_stack_size      = opt_header->value;
      break;
    case XEX_HEADER_DEFAULT_HEAP_SIZE:
      header->exe_heap_size       = opt_header->value;
      break;
    case XEX_HEADER_IMPORT_LIBRARIES:
      {
        const size_t max_count = XECOUNT(header->import_libraries);
        size_t count = XEGETUINT32BE(pp + 0x08);
        XEASSERT(count <= max_count);
        if (count > max_count) {
          XELOGW("ignoring %zu extra entries in XEX_HEADER_IMPORT_LIBRARIES",
                 (max_count - count));
          count = max_count;
        }
        header->import_library_count = count;

        uint32_t string_table_size = XEGETUINT32BE(pp + 0x04);
        const char *string_table = (const char*)(pp + 0x0C);

        pp += 12 + string_table_size;
        for (size_t m = 0; m < count; m++) {
          xe_xex2_import_library_t *library = &header->import_libraries[m];
          XEEXPECTZERO(xe_copy_memory(library->digest, sizeof(library->digest),
                                      pp + 0x04, 20));
          library->import_id          = XEGETUINT32BE(pp + 0x18);
          library->version.value      = XEGETUINT32BE(pp + 0x1C);
          library->min_version.value  = XEGETUINT32BE(pp + 0x20);

          const uint16_t name_index = XEGETUINT16BE(pp + 0x24);
          for (size_t i = 0, j = 0; i < string_table_size;) {
            if (j == name_index) {
              XEIGNORE(xestrcpya(library->name, XECOUNT(library->name),
                                 string_table + i));
              break;
            }
            if (string_table[i] == 0) {
              i++;
              if (i % 4) {
                i += 4 - (i % 4);
              }
              j++;
            } else {
              i++;
            }
          }

          library->record_count   = XEGETUINT16BE(pp + 0x26);
          library->records        = (uint32_t*)xe_calloc(
              library->record_count * sizeof(uint32_t));
          XEEXPECTNOTNULL(library->records);
          pp += 0x28;
          for (size_t i = 0; i < library->record_count; i++) {
            library->records[i] = XEGETUINT32BE(pp);
            pp += 4;
          }
        }
      }
      break;
    case XEX_HEADER_STATIC_LIBRARIES:
      {
        const size_t max_count = XECOUNT(header->static_libraries);
        size_t count = (opt_header->length - 4) / 16;
        XEASSERT(count <= max_count);
        if (count > max_count) {
          XELOGW("ignoring %zu extra entries in XEX_HEADER_STATIC_LIBRARIES",
                 (max_count - count));
          count = max_count;
        }
        header->static_library_count = count;
        pp += 4;
        for (size_t m = 0; m < count; m++) {
          xe_xex2_static_library_t *library = &header->static_libraries[m];
          XEEXPECTZERO(xe_copy_memory(library->name, sizeof(library->name),
                                      pp + 0x00, 8));
          library->name[8]      = 0;
          library->major        = XEGETUINT16BE(pp + 0x08);
          library->minor        = XEGETUINT16BE(pp + 0x0A);
          library->build        = XEGETUINT16BE(pp + 0x0C);
          uint16_t qfeapproval  = XEGETUINT16BE(pp + 0x0E);
          library->approval     = (xe_xex2_approval_type)(qfeapproval & 0x8000);
          library->qfe          = qfeapproval & ~0x8000;
          pp += 16;
        }
      }
      break;
    case XEX_HEADER_FILE_FORMAT_INFO:
      {
        xe_xex2_file_format_info_t *fmt = &header->file_format_info;
        fmt->encryption_type    =
            (xe_xex2_encryption_type)XEGETUINT16BE(pp + 0x04);
        fmt->compression_type   =
            (xe_xex2_compression_type)XEGETUINT16BE(pp + 0x06);
        switch (fmt->compression_type) {
        case XEX_COMPRESSION_NONE:
          // TODO: XEX_COMPRESSION_NONE
          XEASSERTALWAYS();
          break;
        case XEX_COMPRESSION_BASIC:
          {
            xe_xex2_file_basic_compression_info_t *comp_info =
                &fmt->compression_info.basic;
            uint32_t info_size = XEGETUINT32BE(pp + 0x00);
            comp_info->block_count  = (info_size - 8) / 8;
            comp_info->blocks       = (xe_xex2_file_basic_compression_block_t*)
                xe_calloc(comp_info->block_count *
                          sizeof(xe_xex2_file_basic_compression_block_t));
            XEEXPECTNOTNULL(comp_info->blocks);
            for (size_t m = 0; m < comp_info->block_count; m++) {
              xe_xex2_file_basic_compression_block_t *block =
                  &comp_info->blocks[m];
              block->data_size = XEGETUINT32BE(pp + 0x08 + (m * 8));
              block->zero_size = XEGETUINT32BE(pp + 0x0C + (m * 8));
            }
          }
          break;
        case XEX_COMPRESSION_NORMAL:
          {
            xe_xex2_file_normal_compression_info_t *comp_info =
                &fmt->compression_info.normal;
            uint32_t window_size = XEGETUINT32BE(pp + 0x08);
            uint32_t window_bits = 0;
            for (size_t m = 0; m < 32; m++, window_bits++) {
              window_size <<= 1;
              if (window_size == 0x80000000) {
                break;
              }
            }
            comp_info->window_size  = XEGETUINT32BE(pp + 0x08);
            comp_info->window_bits  = window_bits;
            comp_info->block_size   = XEGETUINT32BE(pp + 0x0C);
            XEEXPECTZERO(xe_copy_memory(comp_info->block_hash,
                                        sizeof(comp_info->block_hash),
                                        pp + 0x10, 20));
          }
          break;
        case XEX_COMPRESSION_DELTA:
          // TODO: XEX_COMPRESSION_DELTA
          XEASSERTALWAYS();
          break;
        }
      }
      break;
    }
  }

  // Loader info.
  pc = p + header->certificate_offset;
  ldr = &header->loader_info;
  ldr->header_size        = XEGETUINT32BE(pc + 0x000);
  ldr->image_size         = XEGETUINT32BE(pc + 0x004);
  XEEXPECTZERO(xe_copy_memory(ldr->rsa_signature, sizeof(ldr->rsa_signature),
                              pc + 0x008, 256));
  ldr->unklength          = XEGETUINT32BE(pc + 0x108);
  ldr->image_flags        = (xe_xex2_image_flags)XEGETUINT32BE(pc + 0x10C);
  ldr->load_address       = XEGETUINT32BE(pc + 0x110);
  XEEXPECTZERO(xe_copy_memory(ldr->section_digest, sizeof(ldr->section_digest),
                              pc + 0x114, 20));
  ldr->import_table_count = XEGETUINT32BE(pc + 0x128);
  XEEXPECTZERO(xe_copy_memory(ldr->import_table_digest,
                              sizeof(ldr->import_table_digest),
                              pc + 0x12C, 20));
  XEEXPECTZERO(xe_copy_memory(ldr->media_id, sizeof(ldr->media_id),
                              pc + 0x140, 16));
  XEEXPECTZERO(xe_copy_memory(ldr->file_key, sizeof(ldr->file_key),
                              pc + 0x150, 16));
  ldr->export_table       = XEGETUINT32BE(pc + 0x160);
  XEEXPECTZERO(xe_copy_memory(ldr->header_digest, sizeof(ldr->header_digest),
                              pc + 0x164, 20));
  ldr->game_regions       = (xe_xex2_region_flags)XEGETUINT32BE(pc + 0x178);
  ldr->media_flags        = (xe_xex2_media_flags)XEGETUINT32BE(pc + 0x17C);

  // Section info follows loader info.
  ps = p + header->certificate_offset + 0x180;
  header->section_count   = XEGETUINT32BE(ps + 0x000);
  ps += 4;
  header->sections        = (xe_xex2_section_t*)xe_calloc(
      header->section_count * sizeof(xe_xex2_section_t));
  XEEXPECTNOTNULL(header->sections);
  for (size_t n = 0; n < header->section_count; n++) {
    xe_xex2_section_t *section = &header->sections[n];
    section->info.value = XEGETUINT32BE(ps);
    ps += 4;
    XEEXPECTZERO(xe_copy_memory(section->digest, sizeof(section->digest), ps,
                                sizeof(section->digest)));
    ps += sizeof(section->digest);
  }

  return 0;

XECLEANUP:
  return 1;
}

int xe_xex2_decrypt_key(xe_xex2_header_t *header) {
  const static uint8_t xe_xex2_retail_key[16] = {
    0x20, 0xB1, 0x85, 0xA5, 0x9D, 0x28, 0xFD, 0xC3,
    0x40, 0x58, 0x3F, 0xBB, 0x08, 0x96, 0xBF, 0x91
  };
  const static uint8_t xe_xex2_devkit_key[16] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
  };

  // Guess key based on file info.
  // TODO: better way to finding out which key to use?
  const uint8_t *xexkey;
  if (header->execution_info.title_id) {
    xexkey = xe_xex2_retail_key;
  } else {
    xexkey = xe_xex2_devkit_key;
  }

  // Decrypt the header key.
  uint32_t rk[4 * (MAXNR + 1)];
  int32_t Nr = rijndaelKeySetupDec(rk, xexkey, 128);
  rijndaelDecrypt(rk, Nr,
                  header->loader_info.file_key, header->session_key);

  return 0;
}

typedef struct mspack_memory_file_t {
  struct mspack_system sys;
  void  *buffer;
  off_t buffer_size;
  off_t offset;
} mspack_memory_file;
mspack_memory_file *mspack_memory_open(struct mspack_system *sys,
                                       void* buffer, const size_t buffer_size) {
  XEASSERT(buffer_size < INT_MAX);
  if (buffer_size >= INT_MAX) {
    return NULL;
  }
  mspack_memory_file *memfile = (mspack_memory_file*)xe_calloc(
        sizeof(mspack_memory_file));
  if (!memfile) {
    return NULL;
  }
  memfile->buffer       = buffer;
  memfile->buffer_size  = (off_t)buffer_size;
  memfile->offset       = 0;
  return memfile;
}
void mspack_memory_close(mspack_memory_file *file) {
  mspack_memory_file *memfile = (mspack_memory_file*)file;
  xe_free(memfile);
}
int mspack_memory_read(struct mspack_file *file, void *buffer, int chars) {
  mspack_memory_file *memfile = (mspack_memory_file*)file;
  const off_t remaining = memfile->buffer_size - memfile->offset;
  const off_t total = MIN(chars, remaining);
  if (xe_copy_memory(buffer, total,
                     (uint8_t*)memfile->buffer + memfile->offset, total)) {
    return -1;
  }
  memfile->offset += total;
  return (int)total;
}
int mspack_memory_write(struct mspack_file *file, void *buffer, int chars) {
  mspack_memory_file *memfile = (mspack_memory_file*)file;
  const off_t remaining = memfile->buffer_size - memfile->offset;
  const off_t total = MIN(chars, remaining);
  if (xe_copy_memory((uint8_t*)memfile->buffer + memfile->offset,
                     memfile->buffer_size - memfile->offset, buffer, total)) {
    return -1;
  }
  memfile->offset += total;
  return (int)total;
}
void *mspack_memory_alloc(struct mspack_system *sys, size_t chars) {
  XEUNREFERENCED(sys);
  return xe_calloc(chars);
}
void mspack_memory_free(void *ptr) {
  xe_free(ptr);
}
void mspack_memory_copy(void *src, void *dest, size_t chars) {
  xe_copy_memory(dest, chars, src, chars);
}
struct mspack_system *mspack_memory_sys_create() {
  struct mspack_system *sys = (struct mspack_system *)xe_calloc(
      sizeof(struct mspack_system));
  if (!sys) {
    return NULL;
  }
  sys->read   = mspack_memory_read;
  sys->write  = mspack_memory_write;
  sys->alloc  = mspack_memory_alloc;
  sys->free   = mspack_memory_free;
  sys->copy   = mspack_memory_copy;
  return sys;
}
void mspack_memory_sys_destroy(struct mspack_system *sys) {
  xe_free(sys);
}

void xe_xex2_decrypt_buffer(const uint8_t *session_key,
                            const uint8_t *input_buffer,
                            const size_t input_size, uint8_t* output_buffer,
                            const size_t output_size) {
  uint32_t rk[4 * (MAXNR + 1)];
  uint8_t ivec[16] = {0};
  int32_t Nr = rijndaelKeySetupDec(rk, session_key, 128);
  const uint8_t *ct = input_buffer;
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

int xe_xex2_read_image_uncompressed(const xe_xex2_header_t *header,
                                    const uint8_t *xex_addr,
                                    const size_t xex_length,
                                    Memory* memory) {
  // Allocate in-place the XEX memory.
  const size_t exe_length = xex_length - header->exe_offset;
  size_t uncompressed_size = exe_length;
  uint32_t alloc_result = (uint32_t)memory->HeapAlloc(
      header->exe_address, uncompressed_size,
      MEMORY_FLAG_ZERO);
  if (!alloc_result) {
    XELOGE("Unable to allocate XEX memory at %.8X-%.8X.",
           header->exe_address, uncompressed_size);
    return 2;
  }
  uint8_t *buffer = memory->Translate(header->exe_address);

  const uint8_t *p = (const uint8_t*)xex_addr + header->exe_offset;

  switch (header->file_format_info.encryption_type) {
  case XEX_ENCRYPTION_NONE:
    return xe_copy_memory(buffer, uncompressed_size, p, exe_length);
  case XEX_ENCRYPTION_NORMAL:
    xe_xex2_decrypt_buffer(header->session_key, p, exe_length, buffer,
                           uncompressed_size);
    return 0;
  default:
    XEASSERTALWAYS();
    return 1;
  }

  return 0;
}

int xe_xex2_read_image_basic_compressed(const xe_xex2_header_t *header,
                                        const uint8_t *xex_addr,
                                        const size_t xex_length,
                                        Memory* memory) {
  const size_t exe_length = xex_length - header->exe_offset;
  const uint8_t* source_buffer = (const uint8_t*)xex_addr + header->exe_offset;
  const uint8_t *p = source_buffer;

  // Calculate uncompressed length.
  size_t uncompressed_size = 0;
  const xe_xex2_file_basic_compression_info_t* comp_info =
      &header->file_format_info.compression_info.basic;
  for (size_t n = 0; n < comp_info->block_count; n++) {
    const size_t data_size = comp_info->blocks[n].data_size;
    const size_t zero_size = comp_info->blocks[n].zero_size;
    uncompressed_size += data_size + zero_size;
  }

  // Allocate in-place the XEX memory.
  uint32_t alloc_result = (uint32_t)memory->HeapAlloc(
      header->exe_address, uncompressed_size,
      MEMORY_FLAG_ZERO);
  if (!alloc_result) {
    XELOGE("Unable to allocate XEX memory at %.8X-%.8X.",
           header->exe_address, uncompressed_size);
    XEFAIL();
  }
  uint8_t *buffer = memory->Translate(header->exe_address);
  uint8_t *d = buffer;

  uint32_t rk[4 * (MAXNR + 1)];
  uint8_t ivec[16] = {0};
  int32_t Nr = rijndaelKeySetupDec(rk, header->session_key, 128);

  for (size_t n = 0; n < comp_info->block_count; n++) {
    const size_t data_size = comp_info->blocks[n].data_size;
    const size_t zero_size = comp_info->blocks[n].zero_size;

    switch (header->file_format_info.encryption_type) {
    case XEX_ENCRYPTION_NONE:
      XEEXPECTZERO(xe_copy_memory(d, uncompressed_size - (d - buffer), p,
                                  exe_length - (p - source_buffer)));
      break;
    case XEX_ENCRYPTION_NORMAL:
      {
        const uint8_t *ct = p;
        uint8_t* pt = d;
        for (size_t n = 0; n < data_size; n += 16, ct += 16, pt += 16) {
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
      break;
    default:
      XEASSERTALWAYS();
      return 1;
    }

    p += data_size;
    d += data_size + zero_size;
  }

  return 0;

XECLEANUP:
  return 1;
}

int xe_xex2_read_image_compressed(const xe_xex2_header_t *header,
                                  const uint8_t *xex_addr,
                                  const size_t xex_length,
                                  Memory* memory) {
  const size_t exe_length = xex_length - header->exe_offset;
  const uint8_t *exe_buffer = (const uint8_t*)xex_addr + header->exe_offset;

  // src -> dest:
  // - decrypt (if encrypted)
  // - de-block:
  //    4b total size of next block in uint8_ts
  //   20b hash of entire next block (including size/hash)
  //    Nb block uint8_ts
  // - decompress block contents

  int result_code = 1;

  uint8_t *compress_buffer = NULL;
  const uint8_t *p = NULL;
  uint8_t *d = NULL;
  uint8_t *deblock_buffer = NULL;
  size_t block_size = 0;
  size_t uncompressed_size = 0;
  struct mspack_system *sys = NULL;
  mspack_memory_file *lzxsrc = NULL;
  mspack_memory_file *lzxdst = NULL;
  struct lzxd_stream *lzxd = NULL;

  // Decrypt (if needed).
  bool free_input = false;
  const uint8_t *input_buffer = exe_buffer;
  const size_t input_size = exe_length;
  switch (header->file_format_info.encryption_type) {
  case XEX_ENCRYPTION_NONE:
    // No-op.
    break;
  case XEX_ENCRYPTION_NORMAL:
    // TODO: a way to do without a copy/alloc?
    free_input = true;
    input_buffer = (const uint8_t*)xe_calloc(input_size);
    XEEXPECTNOTNULL(input_buffer);
    xe_xex2_decrypt_buffer(header->session_key, exe_buffer, exe_length,
                           (uint8_t*)input_buffer, input_size);
    break;
  default:
    XEASSERTALWAYS();
    return false;
  }

  compress_buffer = (uint8_t*)xe_calloc(exe_length);
  XEEXPECTNOTNULL(compress_buffer);

  p = input_buffer;
  d = compress_buffer;

  // De-block.
  deblock_buffer = (uint8_t*)xe_calloc(input_size);
  XEEXPECTNOTNULL(deblock_buffer);
  block_size = header->file_format_info.compression_info.normal.block_size;
  while (block_size) {
    const uint8_t *pnext = p + block_size;
    const size_t next_size = XEGETINT32BE(p);
    p += 4;
    p += 20; // skip 20b hash

    while(true) {
      const size_t chunk_size = (p[0] << 8) | p[1];
      p += 2;
      if (!chunk_size) {
        break;
      }
      xe_copy_memory(d, exe_length - (d - compress_buffer), p, chunk_size);
      p += chunk_size;
      d += chunk_size;

      uncompressed_size += 0x8000;
    }

    p = pnext;
    block_size = next_size;
  }

  // Allocate in-place the XEX memory.
  uint32_t alloc_result = (uint32_t)memory->HeapAlloc(
      header->exe_address, uncompressed_size,
      MEMORY_FLAG_ZERO);
  if (!alloc_result) {
    XELOGE("Unable to allocate XEX memory at %.8X-%.8X.",
           header->exe_address, uncompressed_size);
    result_code = 2;
    XEFAIL();
  }
  uint8_t *buffer = memory->Translate(header->exe_address);

  // Setup decompressor and decompress.
  sys = mspack_memory_sys_create();
  XEEXPECTNOTNULL(sys);
  lzxsrc = mspack_memory_open(sys, (void*)compress_buffer, d - compress_buffer);
  XEEXPECTNOTNULL(lzxsrc);
  lzxdst = mspack_memory_open(sys, buffer, uncompressed_size);
  XEEXPECTNOTNULL(lzxdst);
  lzxd = lzxd_init(
      sys,
      (struct mspack_file *)lzxsrc,
      (struct mspack_file *)lzxdst,
      header->file_format_info.compression_info.normal.window_bits,
      0,
      32768,
      (off_t)header->loader_info.image_size);
  XEEXPECTNOTNULL(lzxd);
  XEEXPECTZERO(lzxd_decompress(lzxd, (off_t)header->loader_info.image_size));

  result_code = 0;

XECLEANUP:
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
  xe_free(compress_buffer);
  xe_free(deblock_buffer);
  if (free_input) {
    xe_free((void*)input_buffer);
  }
  return result_code;
}

int xe_xex2_read_image(xe_xex2_ref xex, const uint8_t *xex_addr,
                       const size_t xex_length, Memory* memory) {
  const xe_xex2_header_t *header = &xex->header;
  switch (header->file_format_info.compression_type) {
  case XEX_COMPRESSION_NONE:
    return xe_xex2_read_image_uncompressed(
        header, xex_addr, xex_length, memory);
  case XEX_COMPRESSION_BASIC:
    return xe_xex2_read_image_basic_compressed(
        header, xex_addr, xex_length, memory);
  case XEX_COMPRESSION_NORMAL:
    return xe_xex2_read_image_compressed(
        header, xex_addr, xex_length, memory);
  default:
    XEASSERTALWAYS();
    return 1;
  }
}

int xe_xex2_load_pe(xe_xex2_ref xex) {
  const xe_xex2_header_t* header = &xex->header;
  const uint8_t* p = xex->memory->Translate(header->exe_address);

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
  //opthdr->MajorLinkerVersion; opthdr->MinorLinkerVersion;

  // Data directories of interest:
  // EXPORT           IMAGE_EXPORT_DIRECTORY
  // IMPORT           IMAGE_IMPORT_DESCRIPTOR[]
  // EXCEPTION        IMAGE_CE_RUNTIME_FUNCTION_ENTRY[]
  // BASERELOC
  // DEBUG            IMAGE_DEBUG_DIRECTORY[]
  // ARCHITECTURE     /IMAGE_ARCHITECTURE_HEADER/ ----- import thunks!
  // TLS              IMAGE_TLS_DIRECTORY
  // IAT              Import Address Table ptr
  //opthdr->DataDirectory[IMAGE_DIRECTORY_ENTRY_X].VirtualAddress / .Size

  // Quick scan to determine bounds of sections.
  size_t upper_address = 0;
  const IMAGE_SECTION_HEADER* sechdr = IMAGE_FIRST_SECTION(nthdr);
  for (size_t n = 0; n < filehdr->NumberOfSections; n++, sechdr++) {
    const size_t physical_address = opthdr->ImageBase + sechdr->VirtualAddress;
    upper_address =
        MAX(upper_address, physical_address + sechdr->Misc.VirtualSize);
  }

  // Setup/load sections.
  sechdr = IMAGE_FIRST_SECTION(nthdr);
  for (size_t n = 0; n < filehdr->NumberOfSections; n++, sechdr++) {
    PESection* section = (PESection*)xe_calloc(sizeof(PESection));
    xe_copy_memory(section->name, sizeof(section->name),
                   sechdr->Name, sizeof(sechdr->Name));
    section->name[8]      = 0;
    section->raw_address  = sechdr->PointerToRawData;
    section->raw_size     = sechdr->SizeOfRawData;
    section->address      = header->exe_address + sechdr->VirtualAddress;
    section->size         = sechdr->Misc.VirtualSize;
    section->flags        = sechdr->Characteristics;
    xex->sections->push_back(section);
  }

  //DumpTLSDirectory(pImageBase, pNTHeader, (PIMAGE_TLS_DIRECTORY32)0);
  //DumpExportsSection(pImageBase, pNTHeader);
  return 0;
}

const PESection* xe_xex2_get_pe_section(xe_xex2_ref xex, const char* name) {
  for (std::vector<PESection*>::iterator it = xex->sections->begin();
       it != xex->sections->end(); ++it) {
    if (!xestrcmpa((*it)->name, name)) {
      return *it;
    }
  }
  return NULL;
}

int xe_xex2_get_import_infos(xe_xex2_ref xex,
                             const xe_xex2_import_library_t *library,
                             xe_xex2_import_info_t **out_import_infos,
                             size_t *out_import_info_count) {
  uint8_t *mem = xex->memory->membase();
  const xe_xex2_header_t *header = xe_xex2_get_header(xex);

  // Find library index for verification.
  size_t library_index = -1;
  for (size_t n = 0; n < header->import_library_count; n++) {
    if (&header->import_libraries[n] == library) {
      library_index = n;
      break;
    }
  }
  XEASSERT(library_index != (size_t)-1);

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
    const uint32_t value = XEGETUINT32BE(mem + record);
    if (value & 0xFF000000) {
      // Thunk for previous record - ignore.
    } else {
      // Variable/thunk.
      info_count++;
    }
  }

  // Allocate storage.
  xe_xex2_import_info_t *infos = (xe_xex2_import_info_t*)xe_calloc(
      info_count * sizeof(xe_xex2_import_info_t));
  XEEXPECTNOTNULL(infos);

  // Construct infos.
  for (size_t n = 0, i = 0; n < library->record_count; n++) {
    const uint32_t record = library->records[n];
    const uint32_t value = XEGETUINT32BE(mem + record);
    const uint32_t type = (value & 0xFF000000) >> 24;

    // Verify library index matches given library.
    //XEASSERT(library_index == ((value >> 16) & 0xFF));

    switch (type) {
    case 0x00:
      {
        xe_xex2_import_info_t* info = &infos[i++];
        info->ordinal       = value & 0xFFFF;
        info->value_address = record;
      }
      break;
    case 0x01:
      {
        // Thunk for previous record.
        XEASSERT(i > 0);
        xe_xex2_import_info_t* info = &infos[i - 1];
        XEASSERT(info->ordinal == (value & 0xFFFF));
        info->thunk_address = record;
      }
      break;
    default:
      //XEASSERTALWAYS();
      break;
    }
  }

  *out_import_info_count  = info_count;
  *out_import_infos       = infos;
  return 0;

XECLEANUP:
  xe_free(infos);
  *out_import_info_count  = 0;
  *out_import_infos       = NULL;
  return 1;
}
