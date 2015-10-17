/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_UTIL_XEX2_INFO_H_
#define XENIA_KERNEL_UTIL_XEX2_INFO_H_

#include <cstdint>

#include "xenia/base/byte_order.h"

enum xe_xex2_header_keys : uint32_t {
  XEX_HEADER_RESOURCE_INFO = 0x000002FF,
  XEX_HEADER_FILE_FORMAT_INFO = 0x000003FF,
  XEX_HEADER_DELTA_PATCH_DESCRIPTOR = 0x000005FF,
  XEX_HEADER_BASE_REFERENCE = 0x00000405,
  XEX_HEADER_BOUNDING_PATH = 0x000080FF,
  XEX_HEADER_DEVICE_ID = 0x00008105,
  XEX_HEADER_ORIGINAL_BASE_ADDRESS = 0x00010001,
  XEX_HEADER_ENTRY_POINT = 0x00010100,
  XEX_HEADER_IMAGE_BASE_ADDRESS = 0x00010201,
  XEX_HEADER_IMPORT_LIBRARIES = 0x000103FF,
  XEX_HEADER_CHECKSUM_TIMESTAMP = 0x00018002,
  XEX_HEADER_ENABLED_FOR_CALLCAP = 0x00018102,
  XEX_HEADER_ENABLED_FOR_FASTCAP = 0x00018200,
  XEX_HEADER_ORIGINAL_PE_NAME = 0x000183FF,
  XEX_HEADER_STATIC_LIBRARIES = 0x000200FF,
  XEX_HEADER_TLS_INFO = 0x00020104,
  XEX_HEADER_DEFAULT_STACK_SIZE = 0x00020200,
  XEX_HEADER_DEFAULT_FILESYSTEM_CACHE_SIZE = 0x00020301,
  XEX_HEADER_DEFAULT_HEAP_SIZE = 0x00020401,
  XEX_HEADER_PAGE_HEAP_SIZE_AND_FLAGS = 0x00028002,
  XEX_HEADER_SYSTEM_FLAGS = 0x00030000,
  XEX_HEADER_EXECUTION_INFO = 0x00040006,
  XEX_HEADER_TITLE_WORKSPACE_SIZE = 0x00040201,
  XEX_HEADER_GAME_RATINGS = 0x00040310,
  XEX_HEADER_LAN_KEY = 0x00040404,
  XEX_HEADER_XBOX360_LOGO = 0x000405FF,
  XEX_HEADER_MULTIDISC_MEDIA_IDS = 0x000406FF,
  XEX_HEADER_ALTERNATE_TITLE_IDS = 0x000407FF,
  XEX_HEADER_ADDITIONAL_TITLE_MEMORY = 0x00040801,
  XEX_HEADER_EXPORTS_BY_NAME = 0x00E10402,
};

enum xe_xex2_module_flags : uint32_t {
  XEX_MODULE_TITLE = 0x00000001,
  XEX_MODULE_EXPORTS_TO_TITLE = 0x00000002,
  XEX_MODULE_SYSTEM_DEBUGGER = 0x00000004,
  XEX_MODULE_DLL_MODULE = 0x00000008,
  XEX_MODULE_MODULE_PATCH = 0x00000010,
  XEX_MODULE_PATCH_FULL = 0x00000020,
  XEX_MODULE_PATCH_DELTA = 0x00000040,
  XEX_MODULE_USER_MODE = 0x00000080,
};

enum xe_xex2_system_flags : uint32_t {
  XEX_SYSTEM_NO_FORCED_REBOOT = 0x00000001,
  XEX_SYSTEM_FOREGROUND_TASKS = 0x00000002,
  XEX_SYSTEM_NO_ODD_MAPPING = 0x00000004,
  XEX_SYSTEM_HANDLE_MCE_INPUT = 0x00000008,
  XEX_SYSTEM_RESTRICTED_HUD_FEATURES = 0x00000010,
  XEX_SYSTEM_HANDLE_GAMEPAD_DISCONNECT = 0x00000020,
  XEX_SYSTEM_INSECURE_SOCKETS = 0x00000040,
  XEX_SYSTEM_XBOX1_INTEROPERABILITY = 0x00000080,
  XEX_SYSTEM_DASH_CONTEXT = 0x00000100,
  XEX_SYSTEM_USES_GAME_VOICE_CHANNEL = 0x00000200,
  XEX_SYSTEM_PAL50_INCOMPATIBLE = 0x00000400,
  XEX_SYSTEM_INSECURE_UTILITY_DRIVE = 0x00000800,
  XEX_SYSTEM_XAM_HOOKS = 0x00001000,
  XEX_SYSTEM_ACCESS_PII = 0x00002000,
  XEX_SYSTEM_CROSS_PLATFORM_SYSTEM_LINK = 0x00004000,
  XEX_SYSTEM_MULTIDISC_SWAP = 0x00008000,
  XEX_SYSTEM_MULTIDISC_INSECURE_MEDIA = 0x00010000,
  XEX_SYSTEM_AP25_MEDIA = 0x00020000,
  XEX_SYSTEM_NO_CONFIRM_EXIT = 0x00040000,
  XEX_SYSTEM_ALLOW_BACKGROUND_DOWNLOAD = 0x00080000,
  XEX_SYSTEM_CREATE_PERSISTABLE_RAMDRIVE = 0x00100000,
  XEX_SYSTEM_INHERIT_PERSISTENT_RAMDRIVE = 0x00200000,
  XEX_SYSTEM_ALLOW_HUD_VIBRATION = 0x00400000,
  XEX_SYSTEM_ACCESS_UTILITY_PARTITIONS = 0x00800000,
  XEX_SYSTEM_IPTV_INPUT_SUPPORTED = 0x01000000,
  XEX_SYSTEM_PREFER_BIG_BUTTON_INPUT = 0x02000000,
  XEX_SYSTEM_ALLOW_EXTENDED_SYSTEM_RESERVATION = 0x04000000,
  XEX_SYSTEM_MULTIDISC_CROSS_TITLE = 0x08000000,
  XEX_SYSTEM_INSTALL_INCOMPATIBLE = 0x10000000,
  XEX_SYSTEM_ALLOW_AVATAR_GET_METADATA_BY_XUID = 0x20000000,
  XEX_SYSTEM_ALLOW_CONTROLLER_SWAPPING = 0x40000000,
  XEX_SYSTEM_DASH_EXTENSIBILITY_MODULE = 0x80000000,
  // TODO(benvanik): figure out how stored.
  /*XEX_SYSTEM_ALLOW_NETWORK_READ_CANCEL            = 0x0,
  XEX_SYSTEM_UNINTERRUPTABLE_READS                = 0x0,
  XEX_SYSTEM_REQUIRE_FULL_EXPERIENCE              = 0x0,
  XEX_SYSTEM_GAME_VOICE_REQUIRED_UI               = 0x0,
  XEX_SYSTEM_CAMERA_ANGLE                         = 0x0,
  XEX_SYSTEM_SKELETAL_TRACKING_REQUIRED           = 0x0,
  XEX_SYSTEM_SKELETAL_TRACKING_SUPPORTED          = 0x0,*/
};

// ESRB (Entertainment Software Rating Board)
enum xe_xex2_rating_esrb_value : uint32_t {
  XEX_RATING_ESRB_eC = 0x00,
  XEX_RATING_ESRB_E = 0x02,
  XEX_RATING_ESRB_E10 = 0x04,
  XEX_RATING_ESRB_T = 0x06,
  XEX_RATING_ESRB_M = 0x08,
  XEX_RATING_ESRB_AO = 0x0E,
  XEX_RATING_ESRB_UNRATED = 0xFF,
};
// PEGI (Pan European Game Information)
enum xe_xex2_rating_pegi_value : uint32_t {
  XEX_RATING_PEGI_3_PLUS = 0,
  XEX_RATING_PEGI_7_PLUS = 4,
  XEX_RATING_PEGI_12_PLUS = 9,
  XEX_RATING_PEGI_16_PLUS = 13,
  XEX_RATING_PEGI_18_PLUS = 14,
  XEX_RATING_PEGI_UNRATED = 0xFF,
};
// PEGI (Pan European Game Information) - Finland
enum xe_xex2_rating_pegi_fi_value : uint32_t {
  XEX_RATING_PEGI_FI_3_PLUS = 0,
  XEX_RATING_PEGI_FI_7_PLUS = 4,
  XEX_RATING_PEGI_FI_11_PLUS = 8,
  XEX_RATING_PEGI_FI_15_PLUS = 12,
  XEX_RATING_PEGI_FI_18_PLUS = 14,
  XEX_RATING_PEGI_FI_UNRATED = 0xFF,
};
// PEGI (Pan European Game Information) - Portugal
enum xe_xex2_rating_pegi_pt_value : uint32_t {
  XEX_RATING_PEGI_PT_4_PLUS = 1,
  XEX_RATING_PEGI_PT_6_PLUS = 3,
  XEX_RATING_PEGI_PT_12_PLUS = 9,
  XEX_RATING_PEGI_PT_16_PLUS = 13,
  XEX_RATING_PEGI_PT_18_PLUS = 14,
  XEX_RATING_PEGI_PT_UNRATED = 0xFF,
};
// BBFC (British Board of Film Classification) - UK/Ireland
enum xe_xex2_rating_bbfc_value : uint32_t {
  XEX_RATING_BBFC_UNIVERSAL = 1,
  XEX_RATING_BBFC_PG = 5,
  XEX_RATING_BBFC_3_PLUS = 0,
  XEX_RATING_BBFC_7_PLUS = 4,
  XEX_RATING_BBFC_12_PLUS = 9,
  XEX_RATING_BBFC_15_PLUS = 12,
  XEX_RATING_BBFC_16_PLUS = 13,
  XEX_RATING_BBFC_18_PLUS = 14,
  XEX_RATING_BBFC_UNRATED = 0xFF,
};
// CERO (Computer Entertainment Rating Organization)
enum xe_xex2_rating_cero_value : uint32_t {
  XEX_RATING_CERO_A = 0,
  XEX_RATING_CERO_B = 2,
  XEX_RATING_CERO_C = 4,
  XEX_RATING_CERO_D = 6,
  XEX_RATING_CERO_Z = 8,
  XEX_RATING_CERO_UNRATED = 0xFF,
};
// USK (Unterhaltungssoftware SelbstKontrolle)
enum xe_xex2_rating_usk_value : uint32_t {
  XEX_RATING_USK_ALL = 0,
  XEX_RATING_USK_6_PLUS = 2,
  XEX_RATING_USK_12_PLUS = 4,
  XEX_RATING_USK_16_PLUS = 6,
  XEX_RATING_USK_18_PLUS = 8,
  XEX_RATING_USK_UNRATED = 0xFF,
};
// OFLC (Office of Film and Literature Classification) - Australia
enum xe_xex2_rating_oflc_au_value : uint32_t {
  XEX_RATING_OFLC_AU_G = 0,
  XEX_RATING_OFLC_AU_PG = 2,
  XEX_RATING_OFLC_AU_M = 4,
  XEX_RATING_OFLC_AU_MA15_PLUS = 6,
  XEX_RATING_OFLC_AU_UNRATED = 0xFF,
};
// OFLC (Office of Film and Literature Classification) - New Zealand
enum xe_xex2_rating_oflc_nz_value : uint32_t {
  XEX_RATING_OFLC_NZ_G = 0,
  XEX_RATING_OFLC_NZ_PG = 2,
  XEX_RATING_OFLC_NZ_M = 4,
  XEX_RATING_OFLC_NZ_MA15_PLUS = 6,
  XEX_RATING_OFLC_NZ_UNRATED = 0xFF,
};
// KMRB (Korea Media Rating Board)
enum xe_xex2_rating_kmrb_value : uint32_t {
  XEX_RATING_KMRB_ALL = 0,
  XEX_RATING_KMRB_12_PLUS = 2,
  XEX_RATING_KMRB_15_PLUS = 4,
  XEX_RATING_KMRB_18_PLUS = 6,
  XEX_RATING_KMRB_UNRATED = 0xFF,
};
// Brazil
enum xe_xex2_rating_brazil_value : uint32_t {
  XEX_RATING_BRAZIL_ALL = 0,
  XEX_RATING_BRAZIL_12_PLUS = 2,
  XEX_RATING_BRAZIL_14_PLUS = 4,
  XEX_RATING_BRAZIL_16_PLUS = 5,
  XEX_RATING_BRAZIL_18_PLUS = 8,
  XEX_RATING_BRAZIL_UNRATED = 0xFF,
};
// FPB (Film and Publication Board)
enum xe_xex2_rating_fpb_value : uint32_t {
  XEX_RATING_FPB_ALL = 0,
  XEX_RATING_FPB_PG = 6,
  XEX_RATING_FPB_10_PLUS = 7,
  XEX_RATING_FPB_13_PLUS = 10,
  XEX_RATING_FPB_16_PLUS = 13,
  XEX_RATING_FPB_18_PLUS = 14,
  XEX_RATING_FPB_UNRATED = 0xFF,
};

struct xe_xex2_game_ratings_t {
  xe_xex2_rating_esrb_value esrb;
  xe_xex2_rating_pegi_value pegi;
  xe_xex2_rating_pegi_fi_value pegifi;
  xe_xex2_rating_pegi_pt_value pegipt;
  xe_xex2_rating_bbfc_value bbfc;
  xe_xex2_rating_cero_value cero;
  xe_xex2_rating_usk_value usk;
  xe_xex2_rating_oflc_au_value oflcau;
  xe_xex2_rating_oflc_nz_value oflcnz;
  xe_xex2_rating_kmrb_value kmrb;
  xe_xex2_rating_brazil_value brazil;
  xe_xex2_rating_fpb_value fpb;
};

union xe_xex2_version_t {
  uint32_t value;
  struct {
    uint32_t major : 4;
    uint32_t minor : 4;
    uint32_t build : 16;
    uint32_t qfe : 8;
  };
};

struct xe_xex2_opt_header_t {
  uint32_t key;
  uint32_t length;
  union {
    uint32_t value;
    uint32_t offset;
  };
};

struct xe_xex2_resource_info_t {
  char name[9];
  uint32_t address;
  uint32_t size;
};

struct xe_xex2_execution_info_t {
  uint32_t media_id;
  xe_xex2_version_t version;
  xe_xex2_version_t base_version;
  uint32_t title_id;
  uint8_t platform;
  uint8_t executable_table;
  uint8_t disc_number;
  uint8_t disc_count;
  uint32_t savegame_id;
};

struct xe_xex2_tls_info_t {
  uint32_t slot_count;
  uint32_t raw_data_address;
  uint32_t data_size;
  uint32_t raw_data_size;
};

struct xe_xex2_import_library_t {
  char name[32];
  uint8_t digest[20];
  uint32_t import_id;
  xe_xex2_version_t version;
  xe_xex2_version_t min_version;
  size_t record_count;
  uint32_t* records;
};

enum xe_xex2_approval_type : uint32_t {
  XEX_APPROVAL_UNAPPROVED = 0,
  XEX_APPROVAL_POSSIBLE = 1,
  XEX_APPROVAL_APPROVED = 2,
  XEX_APPROVAL_EXPIRED = 3,
};

struct xe_xex2_static_library_t {
  char name[9];  // 8 + 1 for \0
  uint16_t major;
  uint16_t minor;
  uint16_t build;
  uint16_t qfe;
  xe_xex2_approval_type approval;
};

enum xe_xex2_encryption_type : uint32_t {
  XEX_ENCRYPTION_NONE = 0,
  XEX_ENCRYPTION_NORMAL = 1,
};

enum xe_xex2_compression_type : uint32_t {
  XEX_COMPRESSION_NONE = 0,
  XEX_COMPRESSION_BASIC = 1,
  XEX_COMPRESSION_NORMAL = 2,
  XEX_COMPRESSION_DELTA = 3,
};

struct xe_xex2_file_basic_compression_block_t {
  uint32_t data_size;
  uint32_t zero_size;
};

struct xe_xex2_file_basic_compression_info_t {
  uint32_t block_count;
  xe_xex2_file_basic_compression_block_t* blocks;
};

struct xe_xex2_file_normal_compression_info_t {
  uint32_t window_size;
  uint32_t window_bits;
  uint32_t block_size;
  uint8_t block_hash[20];
};

struct xe_xex2_file_format_info_t {
  xe_xex2_encryption_type encryption_type;
  xe_xex2_compression_type compression_type;
  union {
    xe_xex2_file_basic_compression_info_t basic;
    xe_xex2_file_normal_compression_info_t normal;
  } compression_info;
};

enum xe_xex2_image_flags : uint32_t {
  XEX_IMAGE_MANUFACTURING_UTILITY = 0x00000002,
  XEX_IMAGE_MANUFACTURING_SUPPORT_TOOLS = 0x00000004,
  XEX_IMAGE_XGD2_MEDIA_ONLY = 0x00000008,
  XEX_IMAGE_CARDEA_KEY = 0x00000100,
  XEX_IMAGE_XEIKA_KEY = 0x00000200,
  XEX_IMAGE_USERMODE_TITLE = 0x00000400,
  XEX_IMAGE_USERMODE_SYSTEM = 0x00000800,
  XEX_IMAGE_ORANGE0 = 0x00001000,
  XEX_IMAGE_ORANGE1 = 0x00002000,
  XEX_IMAGE_ORANGE2 = 0x00004000,
  XEX_IMAGE_IPTV_SIGNUP_APPLICATION = 0x00010000,
  XEX_IMAGE_IPTV_TITLE_APPLICATION = 0x00020000,
  XEX_IMAGE_KEYVAULT_PRIVILEGES_REQUIRED = 0x04000000,
  XEX_IMAGE_ONLINE_ACTIVATION_REQUIRED = 0x08000000,
  XEX_IMAGE_PAGE_SIZE_4KB = 0x10000000,  // else 64KB
  XEX_IMAGE_REGION_FREE = 0x20000000,
  XEX_IMAGE_REVOCATION_CHECK_OPTIONAL = 0x40000000,
  XEX_IMAGE_REVOCATION_CHECK_REQUIRED = 0x80000000,
};

enum xe_xex2_media_flags : uint32_t {
  XEX_MEDIA_HARDDISK = 0x00000001,
  XEX_MEDIA_DVD_X2 = 0x00000002,
  XEX_MEDIA_DVD_CD = 0x00000004,
  XEX_MEDIA_DVD_5 = 0x00000008,
  XEX_MEDIA_DVD_9 = 0x00000010,
  XEX_MEDIA_SYSTEM_FLASH = 0x00000020,
  XEX_MEDIA_MEMORY_UNIT = 0x00000080,
  XEX_MEDIA_USB_MASS_STORAGE_DEVICE = 0x00000100,
  XEX_MEDIA_NETWORK = 0x00000200,
  XEX_MEDIA_DIRECT_FROM_MEMORY = 0x00000400,
  XEX_MEDIA_RAM_DRIVE = 0x00000800,
  XEX_MEDIA_SVOD = 0x00001000,
  XEX_MEDIA_INSECURE_PACKAGE = 0x01000000,
  XEX_MEDIA_SAVEGAME_PACKAGE = 0x02000000,
  XEX_MEDIA_LOCALLY_SIGNED_PACKAGE = 0x04000000,
  XEX_MEDIA_LIVE_SIGNED_PACKAGE = 0x08000000,
  XEX_MEDIA_XBOX_PACKAGE = 0x10000000,
};

enum xe_xex2_region_flags : uint32_t {
  XEX_REGION_NTSCU = 0x000000FF,
  XEX_REGION_NTSCJ = 0x0000FF00,
  XEX_REGION_NTSCJ_JAPAN = 0x00000100,
  XEX_REGION_NTSCJ_CHINA = 0x00000200,
  XEX_REGION_PAL = 0x00FF0000,
  XEX_REGION_PAL_AU_NZ = 0x00010000,
  XEX_REGION_OTHER = 0xFF000000,
  XEX_REGION_ALL = 0xFFFFFFFF,
};

struct xe_xex2_loader_info_t {
  uint32_t header_size;
  uint32_t image_size;
  uint8_t rsa_signature[256];
  uint32_t unklength;
  xe_xex2_image_flags image_flags;
  uint32_t load_address;
  uint8_t section_digest[20];
  uint32_t import_table_count;
  uint8_t import_table_digest[20];
  uint8_t media_id[16];
  uint8_t file_key[16];
  uint32_t export_table;  // address of the export table
  uint8_t header_digest[20];
  xe_xex2_region_flags game_regions;
  xe_xex2_media_flags media_flags;
};

enum xe_xex2_section_type {
  XEX_SECTION_CODE = 1,
  XEX_SECTION_DATA = 2,
  XEX_SECTION_READONLY_DATA = 3,
};

struct xe_xex2_section_t {
  uint32_t page_size;
  union {
    struct {
      xe_xex2_section_type type : 4;
      uint32_t page_count : 28;  // # of pages
    };
    uint32_t value;  // To make uint8_t swapping easier
  } info;
  uint8_t digest[20];
};

struct xe_xex2_header_t {
  uint32_t xex2;
  xe_xex2_module_flags module_flags;
  uint32_t exe_offset;
  uint32_t unknown0;
  uint32_t certificate_offset;

  xe_xex2_system_flags system_flags;
  xe_xex2_execution_info_t execution_info;
  xe_xex2_game_ratings_t game_ratings;
  xe_xex2_tls_info_t tls_info;
  size_t import_library_count;
  xe_xex2_import_library_t import_libraries[32];
  uint32_t pe_export_table_offset;  // PE Export Directory
  size_t static_library_count;
  xe_xex2_static_library_t static_libraries[32];
  xe_xex2_file_format_info_t file_format_info;
  xe_xex2_loader_info_t loader_info;
  uint8_t session_key[16];

  uint32_t exe_address;
  uint32_t exe_entry_point;
  uint32_t exe_stack_size;
  uint32_t exe_heap_size;

  size_t header_count;
  xe_xex2_opt_header_t headers[64];

  size_t resource_info_count;
  xe_xex2_resource_info_t* resource_infos;
  size_t section_count;
  xe_xex2_section_t* sections;
};

namespace xe {
union xex2_version {
  xe::be<uint32_t> value;
  struct {
    uint32_t major : 4;
    uint32_t minor : 4;
    uint32_t build : 16;
    uint32_t qfe : 8;
  };
};

struct xex2_opt_lan_key {
  uint8_t key[0x10];
};

struct xex2_opt_bound_path {
  xe::be<uint32_t> size;
  char path[1];
};

struct xex2_opt_static_library {
  char name[8];                    // 0x0
  xe::be<uint16_t> version_major;  // 0x8
  xe::be<uint16_t> version_minor;  // 0xA
  xe::be<uint16_t> version_build;  // 0xC
  xe::be<uint8_t> approval_type;   // 0xE
  xe::be<uint8_t> version_qfe;     // 0xF
};
static_assert_size(xex2_opt_static_library, 0x10);

struct xex2_opt_static_libraries {
  xe::be<uint32_t> size;                 // 0x0
  xex2_opt_static_library libraries[1];  // 0x4
};

struct xex2_opt_original_pe_name {
  xe::be<uint32_t> size;
  char name[1];
};

struct xex2_opt_data_directory {
  xe::be<uint32_t> offset;  // 0x0
  xe::be<uint32_t> size;    // 0x4
};
static_assert_size(xex2_opt_data_directory, 0x8);

struct xex2_opt_tls_info {
  xe::be<uint32_t> slot_count;        // 0x0
  xe::be<uint32_t> raw_data_address;  // 0x4
  xe::be<uint32_t> data_size;         // 0x8
  xe::be<uint32_t> raw_data_size;     // 0xC
};
static_assert_size(xex2_opt_tls_info, 0x10);

struct xex2_resource {
  char name[8];              // 0x0
  xe::be<uint32_t> address;  // 0x8
  xe::be<uint32_t> size;     // 0xC
};
static_assert_size(xex2_resource, 0x10);

struct xex2_opt_resource_info {
  xe::be<uint32_t> size;       // 0x0 Resource count is (size - 4) / 16
  xex2_resource resources[1];  // 0x4
};

struct xex2_opt_delta_patch_descriptor {
  xe::be<uint32_t> size;                         // 0x0
  xex2_version target_version;                   // 0x4
  xex2_version source_version;                   // 0x8
  char digest_source[0x14];                      // 0xC
  char image_key_source[0x10];                   // 0x20
  xe::be<uint32_t> size_of_target_headers;       // 0x30
  xe::be<uint32_t> delta_headers_source_offset;  // 0x34
  xe::be<uint32_t> delta_headers_source_size;    // 0x38
  xe::be<uint32_t> delta_headers_target_offset;  // 0x3C
  xe::be<uint32_t> delta_image_source_offset;    // 0x40
  xe::be<uint32_t> delta_image_source_size;      // 0x44
  xe::be<uint32_t> delta_image_target_offset;    // 0x48
};
static_assert_size(xex2_opt_delta_patch_descriptor, 0x4C);

struct xex2_opt_execution_info {
  xe::be<uint32_t> media_id;          // 0x0
  xe::be<xex2_version> version;       // 0x4
  xe::be<xex2_version> base_version;  // 0x8
  xe::be<uint32_t> title_id;          // 0xC
  uint8_t platform;                   // 0x10
  uint8_t executable_table;           // 0x11
  uint8_t disc_number;                // 0x12
  uint8_t disc_count;                 // 0x13
  xe::be<uint32_t> savegame_id;       // 0x14
};
static_assert_size(xex2_opt_execution_info, 0x18);

struct xex2_opt_import_libraries {
  xe::be<uint32_t> section_size;       // 0x0
  xe::be<uint32_t> string_table_size;  // 0x4
  xe::be<uint32_t> library_count;      // 0x8
  char string_table[1];                // 0xC string_table_size bytes
};

struct xex2_import_library {
  xe::be<uint32_t> size;             // 0x0
  char next_import_digest[0x14];     // 0x4
  xe::be<uint32_t> id;               // 0x18
  xex2_version version;              // 0x1C
  xex2_version version_min;          // 0x20
  xe::be<uint16_t> name_index;       // 0x24
  xe::be<uint16_t> count;            // 0x26
  xe::be<uint32_t> import_table[1];  // 0x28
};

struct xex2_opt_header {
  xe::be<uint32_t> key;  // 0x0

  union {
    xe::be<uint32_t> value;   // 0x4
    xe::be<uint32_t> offset;  // 0x4
  };
};

struct xex2_header {
  xe::be<uint32_t> magic;                     // 0x0 'XEX2'
  xe::be<xe_xex2_module_flags> module_flags;  // 0x4
  xe::be<uint32_t> header_size;               // 0x8
  xe::be<uint32_t> reserved;                  // 0xC
  xe::be<uint32_t> security_offset;           // 0x10
  xe::be<uint32_t> header_count;              // 0x14

  xex2_opt_header headers[1];  // 0x18
};

struct xex2_page_descriptor {
  union {
    struct {
      uint32_t info : 4;
      uint32_t size : 28;
    };
    xe::be<uint32_t> value;  // 0x0
  };
  char data_digest[0x14];  // 0x4
};

struct xex2_security_info {
  xe::be<uint32_t> header_size;              // 0x0
  xe::be<uint32_t> image_size;               // 0x4
  char rsa_signature[0x100];                 // 0x8
  xe::be<uint32_t> unk_108;                  // 0x108 unk length
  xe::be<uint32_t> image_flags;              // 0x10C
  xe::be<uint32_t> load_address;             // 0x110
  char section_digest[0x14];                 // 0x114
  xe::be<uint32_t> import_table_count;       // 0x128
  char import_table_digest[0x14];            // 0x12C
  char xgd2_media_id[0x10];                  // 0x140
  char aes_key[0x10];                        // 0x150
  xe::be<uint32_t> export_table;             // 0x160
  char header_digest[0x14];                  // 0x164
  xe::be<uint32_t> region;                   // 0x178
  xe::be<uint32_t> allowed_media_types;      // 0x17C
  xe::be<uint32_t> page_descriptor_count;    // 0x180
  xex2_page_descriptor page_descriptors[1];  // 0x184
};

struct xex2_export_table {
  xe::be<uint32_t> magic[3];         // 0x0
  xe::be<uint32_t> modulenumber[2];  // 0xC
  xe::be<uint32_t> version[3];       // 0x14
  xe::be<uint32_t> imagebaseaddr;    // 0x20 must be <<16 to be accurate
  xe::be<uint32_t> count;            // 0x24
  xe::be<uint32_t> base;             // 0x28
  xe::be<uint32_t>
      ordOffset[1];  // 0x2C ordOffset[0] + (imagebaseaddr << 16) = function
};

// Little endian PE export directory (from winnt.h)
struct X_IMAGE_EXPORT_DIRECTORY {
  uint32_t Characteristics;
  uint32_t TimeDateStamp;
  uint16_t MajorVersion;
  uint16_t MinorVersion;
  uint32_t Name;
  uint32_t Base;
  uint32_t NumberOfFunctions;
  uint32_t NumberOfNames;
  uint32_t AddressOfFunctions;     // RVA from base of image
  uint32_t AddressOfNames;         // RVA from base of image
  uint32_t AddressOfNameOrdinals;  // RVA from base of image
};

}  // namespace xe

#endif  // XENIA_KERNEL_UTIL_XEX2_INFO_H_
