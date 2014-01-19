/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XEX2_INFO_H_
#define XENIA_KERNEL_XEX2_INFO_H_

#include <xenia/common.h>


typedef enum {
  XEX_HEADER_RESOURCE_INFO                        = 0x000002FF,
  XEX_HEADER_FILE_FORMAT_INFO                     = 0x000003FF,
  XEX_HEADER_DELTA_PATCH_DESCRIPTOR               = 0x000005FF,
  XEX_HEADER_BASE_REFERENCE                       = 0x00000405,
  XEX_HEADER_BOUNDING_PATH                        = 0x000080FF,
  XEX_HEADER_DEVICE_ID                            = 0x00008105,
  XEX_HEADER_ORIGINAL_BASE_ADDRESS                = 0x00010001,
  XEX_HEADER_ENTRY_POINT                          = 0x00010100,
  XEX_HEADER_IMAGE_BASE_ADDRESS                   = 0x00010201,
  XEX_HEADER_IMPORT_LIBRARIES                     = 0x000103FF,
  XEX_HEADER_CHECKSUM_TIMESTAMP                   = 0x00018002,
  XEX_HEADER_ENABLED_FOR_CALLCAP                  = 0x00018102,
  XEX_HEADER_ENABLED_FOR_FASTCAP                  = 0x00018200,
  XEX_HEADER_ORIGINAL_PE_NAME                     = 0x000183FF,
  XEX_HEADER_STATIC_LIBRARIES                     = 0x000200FF,
  XEX_HEADER_TLS_INFO                             = 0x00020104,
  XEX_HEADER_DEFAULT_STACK_SIZE                   = 0x00020200,
  XEX_HEADER_DEFAULT_FILESYSTEM_CACHE_SIZE        = 0x00020301,
  XEX_HEADER_DEFAULT_HEAP_SIZE                    = 0x00020401,
  XEX_HEADER_PAGE_HEAP_SIZE_AND_FLAGS             = 0x00028002,
  XEX_HEADER_SYSTEM_FLAGS                         = 0x00030000,
  XEX_HEADER_EXECUTION_INFO                       = 0x00040006,
  XEX_HEADER_TITLE_WORKSPACE_SIZE                 = 0x00040201,
  XEX_HEADER_GAME_RATINGS                         = 0x00040310,
  XEX_HEADER_LAN_KEY                              = 0x00040404,
  XEX_HEADER_XBOX360_LOGO                         = 0x000405FF,
  XEX_HEADER_MULTIDISC_MEDIA_IDS                  = 0x000406FF,
  XEX_HEADER_ALTERNATE_TITLE_IDS                  = 0x000407FF,
  XEX_HEADER_ADDITIONAL_TITLE_MEMORY              = 0x00040801,
  XEX_HEADER_EXPORTS_BY_NAME                      = 0x00E10402,
} xe_xex2_header_keys;

typedef enum {
  XEX_MODULE_TITLE                                = 0x00000001,
  XEX_MODULE_EXPORTS_TO_TITLE                     = 0x00000002,
  XEX_MODULE_SYSTEM_DEBUGGER                      = 0x00000004,
  XEX_MODULE_DLL_MODULE                           = 0x00000008,
  XEX_MODULE_MODULE_PATCH                         = 0x00000010,
  XEX_MODULE_PATCH_FULL                           = 0x00000020,
  XEX_MODULE_PATCH_DELTA                          = 0x00000040,
  XEX_MODULE_USER_MODE                            = 0x00000080,
} xe_xex2_module_flags;

typedef enum {
  XEX_SYSTEM_NO_FORCED_REBOOT                     = 0x00000001,
  XEX_SYSTEM_FOREGROUND_TASKS                     = 0x00000002,
  XEX_SYSTEM_NO_ODD_MAPPING                       = 0x00000004,
  XEX_SYSTEM_HANDLE_MCE_INPUT                     = 0x00000008,
  XEX_SYSTEM_RESTRICTED_HUD_FEATURES              = 0x00000010,
  XEX_SYSTEM_HANDLE_GAMEPAD_DISCONNECT            = 0x00000020,
  XEX_SYSTEM_INSECURE_SOCKETS                     = 0x00000040,
  XEX_SYSTEM_XBOX1_INTEROPERABILITY               = 0x00000080,
  XEX_SYSTEM_DASH_CONTEXT                         = 0x00000100,
  XEX_SYSTEM_USES_GAME_VOICE_CHANNEL              = 0x00000200,
  XEX_SYSTEM_PAL50_INCOMPATIBLE                   = 0x00000400,
  XEX_SYSTEM_INSECURE_UTILITY_DRIVE               = 0x00000800,
  XEX_SYSTEM_XAM_HOOKS                            = 0x00001000,
  XEX_SYSTEM_ACCESS_PII                           = 0x00002000,
  XEX_SYSTEM_CROSS_PLATFORM_SYSTEM_LINK           = 0x00004000,
  XEX_SYSTEM_MULTIDISC_SWAP                       = 0x00008000,
  XEX_SYSTEM_MULTIDISC_INSECURE_MEDIA             = 0x00010000,
  XEX_SYSTEM_AP25_MEDIA                           = 0x00020000,
  XEX_SYSTEM_NO_CONFIRM_EXIT                      = 0x00040000,
  XEX_SYSTEM_ALLOW_BACKGROUND_DOWNLOAD            = 0x00080000,
  XEX_SYSTEM_CREATE_PERSISTABLE_RAMDRIVE          = 0x00100000,
  XEX_SYSTEM_INHERIT_PERSISTENT_RAMDRIVE          = 0x00200000,
  XEX_SYSTEM_ALLOW_HUD_VIBRATION                  = 0x00400000,
  XEX_SYSTEM_ACCESS_UTILITY_PARTITIONS            = 0x00800000,
  XEX_SYSTEM_IPTV_INPUT_SUPPORTED                 = 0x01000000,
  XEX_SYSTEM_PREFER_BIG_BUTTON_INPUT              = 0x02000000,
  XEX_SYSTEM_ALLOW_EXTENDED_SYSTEM_RESERVATION    = 0x04000000,
  XEX_SYSTEM_MULTIDISC_CROSS_TITLE                = 0x08000000,
  XEX_SYSTEM_INSTALL_INCOMPATIBLE                 = 0x10000000,
  XEX_SYSTEM_ALLOW_AVATAR_GET_METADATA_BY_XUID    = 0x20000000,
  XEX_SYSTEM_ALLOW_CONTROLLER_SWAPPING            = 0x40000000,
  XEX_SYSTEM_DASH_EXTENSIBILITY_MODULE            = 0x80000000,
  // TODO: figure out how stored
  /*XEX_SYSTEM_ALLOW_NETWORK_READ_CANCEL            = 0x0,
  XEX_SYSTEM_UNINTERRUPTABLE_READS                = 0x0,
  XEX_SYSTEM_REQUIRE_FULL_EXPERIENCE              = 0x0,
  XEX_SYSTEM_GAME_VOICE_REQUIRED_UI               = 0x0,
  XEX_SYSTEM_CAMERA_ANGLE                         = 0x0,
  XEX_SYSTEM_SKELETAL_TRACKING_REQUIRED           = 0x0,
  XEX_SYSTEM_SKELETAL_TRACKING_SUPPORTED          = 0x0,*/
} xe_xex2_system_flags;

// ESRB (Entertainment Software Rating Board)
typedef enum {
  XEX_RATING_ESRB_eC                              = 0x00,
  XEX_RATING_ESRB_E                               = 0x02,
  XEX_RATING_ESRB_E10                             = 0x04,
  XEX_RATING_ESRB_T                               = 0x06,
  XEX_RATING_ESRB_M                               = 0x08,
  XEX_RATING_ESRB_AO                              = 0x0E,
  XEX_RATING_ESRB_UNRATED                         = 0xFF,
} xe_xex2_rating_esrb_value;
// PEGI (Pan European Game Information)
typedef enum {
  XEX_RATING_PEGI_3_PLUS                          = 0,
  XEX_RATING_PEGI_7_PLUS                          = 4,
  XEX_RATING_PEGI_12_PLUS                         = 9,
  XEX_RATING_PEGI_16_PLUS                         = 13,
  XEX_RATING_PEGI_18_PLUS                         = 14,
  XEX_RATING_PEGI_UNRATED                         = 0xFF,
} xe_xex2_rating_pegi_value;
// PEGI (Pan European Game Information) - Finland
typedef enum {
  XEX_RATING_PEGI_FI_3_PLUS                       = 0,
  XEX_RATING_PEGI_FI_7_PLUS                       = 4,
  XEX_RATING_PEGI_FI_11_PLUS                      = 8,
  XEX_RATING_PEGI_FI_15_PLUS                      = 12,
  XEX_RATING_PEGI_FI_18_PLUS                      = 14,
  XEX_RATING_PEGI_FI_UNRATED                      = 0xFF,
} xe_xex2_rating_pegi_fi_value;
// PEGI (Pan European Game Information) - Portugal
typedef enum {
  XEX_RATING_PEGI_PT_4_PLUS                       = 1,
  XEX_RATING_PEGI_PT_6_PLUS                       = 3,
  XEX_RATING_PEGI_PT_12_PLUS                      = 9,
  XEX_RATING_PEGI_PT_16_PLUS                      = 13,
  XEX_RATING_PEGI_PT_18_PLUS                      = 14,
  XEX_RATING_PEGI_PT_UNRATED                      = 0xFF,
} xe_xex2_rating_pegi_pt_value;
// BBFC (British Board of Film Classification) - UK/Ireland
typedef enum {
  XEX_RATING_BBFC_UNIVERSAL                       = 1,
  XEX_RATING_BBFC_PG                              = 5,
  XEX_RATING_BBFC_3_PLUS                          = 0,
  XEX_RATING_BBFC_7_PLUS                          = 4,
  XEX_RATING_BBFC_12_PLUS                         = 9,
  XEX_RATING_BBFC_15_PLUS                         = 12,
  XEX_RATING_BBFC_16_PLUS                         = 13,
  XEX_RATING_BBFC_18_PLUS                         = 14,
  XEX_RATING_BBFC_UNRATED                         = 0xFF,
} xe_xex2_rating_bbfc_value;
// CERO (Computer Entertainment Rating Organization)
typedef enum {
  XEX_RATING_CERO_A                               = 0,
  XEX_RATING_CERO_B                               = 2,
  XEX_RATING_CERO_C                               = 4,
  XEX_RATING_CERO_D                               = 6,
  XEX_RATING_CERO_Z                               = 8,
  XEX_RATING_CERO_UNRATED                         = 0xFF,
} xe_xex2_rating_cero_value;
// USK (Unterhaltungssoftware SelbstKontrolle)
typedef enum {
  XEX_RATING_USK_ALL                              = 0,
  XEX_RATING_USK_6_PLUS                           = 2,
  XEX_RATING_USK_12_PLUS                          = 4,
  XEX_RATING_USK_16_PLUS                          = 6,
  XEX_RATING_USK_18_PLUS                          = 8,
  XEX_RATING_USK_UNRATED                          = 0xFF,
} xe_xex2_rating_usk_value;
// OFLC (Office of Film and Literature Classification) - Australia
typedef enum {
  XEX_RATING_OFLC_AU_G                            = 0,
  XEX_RATING_OFLC_AU_PG                           = 2,
  XEX_RATING_OFLC_AU_M                            = 4,
  XEX_RATING_OFLC_AU_MA15_PLUS                    = 6,
  XEX_RATING_OFLC_AU_UNRATED                      = 0xFF,
} xe_xex2_rating_oflc_au_value;
// OFLC (Office of Film and Literature Classification) - New Zealand
typedef enum {
  XEX_RATING_OFLC_NZ_G                            = 0,
  XEX_RATING_OFLC_NZ_PG                           = 2,
  XEX_RATING_OFLC_NZ_M                            = 4,
  XEX_RATING_OFLC_NZ_MA15_PLUS                    = 6,
  XEX_RATING_OFLC_NZ_UNRATED                      = 0xFF,
} xe_xex2_rating_oflc_nz_value;
// KMRB (Korea Media Rating Board)
typedef enum {
  XEX_RATING_KMRB_ALL                             = 0,
  XEX_RATING_KMRB_12_PLUS                         = 2,
  XEX_RATING_KMRB_15_PLUS                         = 4,
  XEX_RATING_KMRB_18_PLUS                         = 6,
  XEX_RATING_KMRB_UNRATED                         = 0xFF,
} xe_xex2_rating_kmrb_value;
// Brazil
typedef enum {
  XEX_RATING_BRAZIL_ALL                           = 0,
  XEX_RATING_BRAZIL_12_PLUS                       = 2,
  XEX_RATING_BRAZIL_14_PLUS                       = 4,
  XEX_RATING_BRAZIL_16_PLUS                       = 5,
  XEX_RATING_BRAZIL_18_PLUS                       = 8,
  XEX_RATING_BRAZIL_UNRATED                       = 0xFF,
} xe_xex2_rating_brazil_value;
// FPB (Film and Publication Board)
typedef enum {
  XEX_RATING_FPB_ALL                              = 0,
  XEX_RATING_FPB_PG                               = 6,
  XEX_RATING_FPB_10_PLUS                          = 7,
  XEX_RATING_FPB_13_PLUS                          = 10,
  XEX_RATING_FPB_16_PLUS                          = 13,
  XEX_RATING_FPB_18_PLUS                          = 14,
  XEX_RATING_FPB_UNRATED                          = 0xFF,
} xe_xex2_rating_fpb_value;

typedef struct {
  xe_xex2_rating_esrb_value       esrb;
  xe_xex2_rating_pegi_value       pegi;
  xe_xex2_rating_pegi_fi_value    pegifi;
  xe_xex2_rating_pegi_pt_value    pegipt;
  xe_xex2_rating_bbfc_value       bbfc;
  xe_xex2_rating_cero_value       cero;
  xe_xex2_rating_usk_value        usk;
  xe_xex2_rating_oflc_au_value    oflcau;
  xe_xex2_rating_oflc_nz_value    oflcnz;
  xe_xex2_rating_kmrb_value       kmrb;
  xe_xex2_rating_brazil_value     brazil;
  xe_xex2_rating_fpb_value        fpb;
} xe_xex2_game_ratings_t;

typedef union {
  uint32_t                  value;
  struct {
    uint32_t                major   : 4;
    uint32_t                minor   : 4;
    uint32_t                build   : 16;
    uint32_t                qfe     : 8;
  };
} xe_xex2_version_t;

typedef struct {
  uint32_t                  key;
  uint32_t                  length;
  union {
    uint32_t                value;
    uint32_t                offset;
  };
} xe_xex2_opt_header_t;

typedef struct {
  char                      name[9];
  uint32_t                  address;
  uint32_t                  size;
} xe_xex2_resource_info_t;

typedef struct {
  uint32_t                  media_id;
  xe_xex2_version_t         version;
  xe_xex2_version_t         base_version;
  uint32_t                  title_id;
  uint8_t                   platform;
  uint8_t                   executable_table;
  uint8_t                   disc_number;
  uint8_t                   disc_count;
  uint32_t                  savegame_id;
} xe_xex2_execution_info_t;

typedef struct {
  uint32_t                  slot_count;
  uint32_t                  raw_data_address;
  uint32_t                  data_size;
  uint32_t                  raw_data_size;
} xe_xex2_tls_info_t;

typedef struct {
  char                      name[32];
  uint8_t                   digest[20];
  uint32_t                  import_id;
  xe_xex2_version_t         version;
  xe_xex2_version_t         min_version;
  size_t                    record_count;
  uint32_t                  *records;
} xe_xex2_import_library_t;

typedef enum {
  XEX_APPROVAL_UNAPPROVED     = 0,
  XEX_APPROVAL_POSSIBLE       = 1,
  XEX_APPROVAL_APPROVED       = 2,
  XEX_APPROVAL_EXPIRED        = 3,
} xe_xex2_approval_type;

typedef struct {
  char                      name[9];        // 8 + 1 for \0
  uint16_t                  major;
  uint16_t                  minor;
  uint16_t                  build;
  uint16_t                  qfe;
  xe_xex2_approval_type     approval;
} xe_xex2_static_library_t;

typedef enum {
  XEX_ENCRYPTION_NONE         = 0,
  XEX_ENCRYPTION_NORMAL       = 1,
} xe_xex2_encryption_type;

typedef enum {
  XEX_COMPRESSION_NONE        = 0,
  XEX_COMPRESSION_BASIC       = 1,
  XEX_COMPRESSION_NORMAL      = 2,
  XEX_COMPRESSION_DELTA       = 3,
} xe_xex2_compression_type;

typedef struct {
  uint32_t                  data_size;
  uint32_t                  zero_size;
} xe_xex2_file_basic_compression_block_t;

typedef struct {
  uint32_t                  block_count;
  xe_xex2_file_basic_compression_block_t *blocks;
} xe_xex2_file_basic_compression_info_t;

typedef struct {
  uint32_t                  window_size;
  uint32_t                  window_bits;
  uint32_t                  block_size;
  uint8_t                   block_hash[20];
} xe_xex2_file_normal_compression_info_t;

typedef struct {
  xe_xex2_encryption_type   encryption_type;
  xe_xex2_compression_type  compression_type;
  union {
    xe_xex2_file_basic_compression_info_t   basic;
    xe_xex2_file_normal_compression_info_t  normal;
  } compression_info;
} xe_xex2_file_format_info_t;

typedef enum {
  XEX_IMAGE_MANUFACTURING_UTILITY                 = 0x00000002,
  XEX_IMAGE_MANUFACTURING_SUPPORT_TOOLS           = 0x00000004,
  XEX_IMAGE_XGD2_MEDIA_ONLY                       = 0x00000008,
  XEX_IMAGE_CARDEA_KEY                            = 0x00000100,
  XEX_IMAGE_XEIKA_KEY                             = 0x00000200,
  XEX_IMAGE_USERMODE_TITLE                        = 0x00000400,
  XEX_IMAGE_USERMODE_SYSTEM                       = 0x00000800,
  XEX_IMAGE_ORANGE0                               = 0x00001000,
  XEX_IMAGE_ORANGE1                               = 0x00002000,
  XEX_IMAGE_ORANGE2                               = 0x00004000,
  XEX_IMAGE_IPTV_SIGNUP_APPLICATION               = 0x00010000,
  XEX_IMAGE_IPTV_TITLE_APPLICATION                = 0x00020000,
  XEX_IMAGE_KEYVAULT_PRIVILEGES_REQUIRED          = 0x04000000,
  XEX_IMAGE_ONLINE_ACTIVATION_REQUIRED            = 0x08000000,
  XEX_IMAGE_PAGE_SIZE_4KB                         = 0x10000000, // else 64KB
  XEX_IMAGE_REGION_FREE                           = 0x20000000,
  XEX_IMAGE_REVOCATION_CHECK_OPTIONAL             = 0x40000000,
  XEX_IMAGE_REVOCATION_CHECK_REQUIRED             = 0x80000000,
} xe_xex2_image_flags;

typedef enum {
  XEX_MEDIA_HARDDISK                              = 0x00000001,
  XEX_MEDIA_DVD_X2                                = 0x00000002,
  XEX_MEDIA_DVD_CD                                = 0x00000004,
  XEX_MEDIA_DVD_5                                 = 0x00000008,
  XEX_MEDIA_DVD_9                                 = 0x00000010,
  XEX_MEDIA_SYSTEM_FLASH                          = 0x00000020,
  XEX_MEDIA_MEMORY_UNIT                           = 0x00000080,
  XEX_MEDIA_USB_MASS_STORAGE_DEVICE               = 0x00000100,
  XEX_MEDIA_NETWORK                               = 0x00000200,
  XEX_MEDIA_DIRECT_FROM_MEMORY                    = 0x00000400,
  XEX_MEDIA_RAM_DRIVE                             = 0x00000800,
  XEX_MEDIA_SVOD                                  = 0x00001000,
  XEX_MEDIA_INSECURE_PACKAGE                      = 0x01000000,
  XEX_MEDIA_SAVEGAME_PACKAGE                      = 0x02000000,
  XEX_MEDIA_LOCALLY_SIGNED_PACKAGE                = 0x04000000,
  XEX_MEDIA_LIVE_SIGNED_PACKAGE                   = 0x08000000,
  XEX_MEDIA_XBOX_PACKAGE                          = 0x10000000,
} xe_xex2_media_flags;

typedef enum {
  XEX_REGION_NTSCU                                = 0x000000FF,
  XEX_REGION_NTSCJ                                = 0x0000FF00,
  XEX_REGION_NTSCJ_JAPAN                          = 0x00000100,
  XEX_REGION_NTSCJ_CHINA                          = 0x00000200,
  XEX_REGION_PAL                                  = 0x00FF0000,
  XEX_REGION_PAL_AU_NZ                            = 0x00010000,
  XEX_REGION_OTHER                                = 0xFF000000,
  XEX_REGION_ALL                                  = 0xFFFFFFFF,
} xe_xex2_region_flags;

typedef struct {
  uint32_t                  header_size;
  uint32_t                  image_size;
  uint8_t                   rsa_signature[256];
  uint32_t                  unklength;
  xe_xex2_image_flags       image_flags;
  uint32_t                  load_address;
  uint8_t                   section_digest[20];
  uint32_t                  import_table_count;
  uint8_t                   import_table_digest[20];
  uint8_t                   media_id[16];
  uint8_t                   file_key[16];
  uint32_t                  export_table;
  uint8_t                   header_digest[20];
  xe_xex2_region_flags      game_regions;
  xe_xex2_media_flags       media_flags;
} xe_xex2_loader_info_t;

typedef enum {
  XEX_SECTION_CODE            = 1,
  XEX_SECTION_DATA            = 2,
  XEX_SECTION_READONLY_DATA   = 3,
} xe_xex2_section_type;

typedef struct {
  union {
    struct {
      xe_xex2_section_type  type          : 4;
      uint32_t              page_count    : 28;   // # of 64kb pages
    };
    uint32_t  value;                              // To make uint8_t swapping easier
  } info;
  uint8_t digest[20];
} xe_xex2_section_t;

#define xe_xex2_section_length    0x00010000

typedef struct {
  uint32_t                    xex2;
  xe_xex2_module_flags        module_flags;
  uint32_t                    exe_offset;
  uint32_t                    unknown0;
  uint32_t                    certificate_offset;

  xe_xex2_system_flags        system_flags;
  xe_xex2_execution_info_t    execution_info;
  xe_xex2_game_ratings_t      game_ratings;
  xe_xex2_tls_info_t          tls_info;
  size_t                      import_library_count;
  xe_xex2_import_library_t    import_libraries[32];
  size_t                      static_library_count;
  xe_xex2_static_library_t    static_libraries[32];
  xe_xex2_file_format_info_t  file_format_info;
  xe_xex2_loader_info_t       loader_info;
  uint8_t                     session_key[16];

  uint32_t                    exe_address;
  uint32_t                    exe_entry_point;
  uint32_t                    exe_stack_size;
  uint32_t                    exe_heap_size;

  size_t                      header_count;
  xe_xex2_opt_header_t        headers[64];

  size_t                      resource_info_count;
  xe_xex2_resource_info_t*    resource_infos;
  size_t                      section_count;
  xe_xex2_section_t*          sections;
} xe_xex2_header_t;


#endif  // XENIA_KERNEL_XEX2_INFO_H_
