/* Xenia: minor tweaks to bring up to date with winnt.h */
#include <cstdint>

/* Copyright (c) Microsoft Corporation. All rights reserved. */
/* NT image format (to be used when the Win32 SDK version of WINNT.H cannot) */

#define XIMAGE_DOS_SIGNATURE 0x5A4D    /* MZ */
#define XIMAGE_OS2_SIGNATURE 0x454E    /* NE */
#define XIMAGE_OS2_SIGNATURE_LE 0x454C /* LE */
#define XIMAGE_NT_SIGNATURE 0x00004550 /* PE00 */

typedef struct _XIMAGE_DOS_HEADER { /* DOS .EXE header */
  uint16_t e_magic;                 /* Magic number */
  uint16_t e_cblp;                  /* Bytes on last page of file */
  uint16_t e_cp;                    /* Pages in file */
  uint16_t e_crlc;                  /* Relocations */
  uint16_t e_cparhdr;               /* Size of header in paragraphs */
  uint16_t e_minalloc;              /* Minimum extra paragraphs needed */
  uint16_t e_maxalloc;              /* Maximum extra paragraphs needed */
  uint16_t e_ss;                    /* Initial (relative) SS value */
  uint16_t e_sp;                    /* Initial SP value */
  uint16_t e_csum;                  /* Checksum */
  uint16_t e_ip;                    /* Initial IP value */
  uint16_t e_cs;                    /* Initial (relative) CS value */
  uint16_t e_lfarlc;                /* File address of relocation table */
  uint16_t e_ovno;                  /* Overlay number */
  uint16_t e_res[4];                /* Reserved words */
  uint16_t e_oemid;                 /* OEM identifier (for e_oeminfo) */
  uint16_t e_oeminfo;               /* OEM information; e_oemid specific */
  uint16_t e_res2[10];              /* Reserved words */
  int32_t e_lfanew;                 /* File address of new exe header */
} XIMAGE_DOS_HEADER, *PXIMAGE_DOS_HEADER;

typedef struct _XIMAGE_OS2_HEADER { /* OS/2 .EXE header */
  uint16_t ne_magic;                /* Magic number */
  int8_t ne_ver;                    /* Version number */
  int8_t ne_rev;                    /* Revision number */
  uint16_t ne_enttab;               /* Offset of Entry Table */
  uint16_t ne_cbenttab;             /* Number of bytes in Entry Table */
  int32_t ne_crc;                   /* Checksum of whole file */
  uint16_t ne_flags;                /* Flag word */
  uint16_t ne_autodata;             /* Automatic data segment number */
  uint16_t ne_heap;                 /* Initial heap allocation */
  uint16_t ne_stack;                /* Initial stack allocation */
  int32_t ne_csip;                  /* Initial CS:IP setting */
  int32_t ne_sssp;                  /* Initial SS:SP setting */
  uint16_t ne_cseg;                 /* Count of file segments */
  uint16_t ne_cmod;                 /* Entries in Module Reference Table */
  uint16_t ne_cbnrestab;            /* Size of non-resident name table */
  uint16_t ne_segtab;               /* Offset of Segment Table */
  uint16_t ne_rsrctab;              /* Offset of Resource Table */
  uint16_t ne_restab;               /* Offset of resident name table */
  uint16_t ne_modtab;               /* Offset of Module Reference Table */
  uint16_t ne_imptab;               /* Offset of Imported Names Table */
  int32_t ne_nrestab;               /* Offset of Non-resident Names Table */
  uint16_t ne_cmovent;              /* Count of movable entries */
  uint16_t ne_align;                /* Segment alignment shift count */
  uint16_t ne_cres;                 /* Count of resource segments */
  uint8_t ne_exetyp;                /* Target Operating system */
  uint8_t ne_flagsothers;           /* Other .EXE flags */
  uint16_t ne_pretthunks;           /* offset to return thunks */
  uint16_t ne_psegrefbytes;         /* offset to segment ref. bytes */
  uint16_t ne_swaparea;             /* Minimum code swap area size */
  uint16_t ne_expver;               /* Expected Windows version number */
} XIMAGE_OS2_HEADER, *PXIMAGE_OS2_HEADER;

typedef struct _XIMAGE_FILE_HEADER {
  uint16_t Machine;
  uint16_t NumberOfSections;
  uint32_t TimeDateStamp;
  uint32_t PointerToSymbolTable;
  uint32_t NumberOfSymbols;
  uint16_t SizeOfOptionalHeader;
  uint16_t Characteristics;
} XIMAGE_FILE_HEADER, *PXIMAGE_FILE_HEADER;

#define XIMAGE_FILE_32BIT_MACHINE 0x0100 /* 32 bit word machine. */

#define XIMAGE_FILE_MACHINE_POWERPCBE 0x01F2  // IBM PowerPC Big-Endian

typedef struct _XIMAGE_DATA_DIRECTORY {
  uint32_t VirtualAddress;
  uint32_t Size;
} XIMAGE_DATA_DIRECTORY, *PXIMAGE_DATA_DIRECTORY;

#define XIMAGE_NUMBEROF_DIRECTORY_ENTRIES 16

/*
 * Optional header format.
 */

typedef struct _XIMAGE_OPTIONAL_HEADER {
  /*
   * Standard fields.
   */

  uint16_t Magic;
  uint8_t MajorLinkerVersion;
  uint8_t MinorLinkerVersion;
  uint32_t SizeOfCode;
  uint32_t SizeOfInitializedData;
  uint32_t SizeOfUninitializedData;
  uint32_t AddressOfEntryPoint;
  uint32_t BaseOfCode;
  uint32_t BaseOfData;

  /*
   * NT additional fields.
   */

  uint32_t ImageBase;
  uint32_t SectionAlignment;
  uint32_t FileAlignment;
  uint16_t MajorOperatingSystemVersion;
  uint16_t MinorOperatingSystemVersion;
  uint16_t MajorImageVersion;
  uint16_t MinorImageVersion;
  uint16_t MajorSubsystemVersion;
  uint16_t MinorSubsystemVersion;
  uint32_t Reserved1;
  uint32_t SizeOfImage;
  uint32_t SizeOfHeaders;
  uint32_t CheckSum;
  uint16_t Subsystem;
  uint16_t DllCharacteristics;
  uint32_t SizeOfStackReserve;
  uint32_t SizeOfStackCommit;
  uint32_t SizeOfHeapReserve;
  uint32_t SizeOfHeapCommit;
  uint32_t LoaderFlags;
  uint32_t NumberOfRvaAndSizes;
  XIMAGE_DATA_DIRECTORY DataDirectory[XIMAGE_NUMBEROF_DIRECTORY_ENTRIES];
} XIMAGE_OPTIONAL_HEADER, *PXIMAGE_OPTIONAL_HEADER;

typedef XIMAGE_OPTIONAL_HEADER XIMAGE_OPTIONAL_HEADER32;

#define XIMAGE_SIZEOF_NT_OPTIONAL_HEADER 224

#define XIMAGE_NT_OPTIONAL_HDR32_MAGIC 0x10b

typedef struct _XIMAGE_NT_HEADERS {
  uint32_t Signature;
  XIMAGE_FILE_HEADER FileHeader;
  XIMAGE_OPTIONAL_HEADER OptionalHeader;
} XIMAGE_NT_HEADERS, *PXIMAGE_NT_HEADERS;

typedef XIMAGE_NT_HEADERS XIMAGE_NT_HEADERS32;

#define XIMAGE_FIRST_SECTION(ntheader)                                    \
  ((PXIMAGE_SECTION_HEADER)((uint8_t*)ntheader +                          \
                            offsetof(XIMAGE_NT_HEADERS, OptionalHeader) + \
                            ((PXIMAGE_NT_HEADERS)(ntheader))              \
                                ->FileHeader.SizeOfOptionalHeader))

#define XIMAGE_SUBSYSTEM_XBOX 14

/*
 * Section header format.
 */

#define XIMAGE_SIZEOF_SHORT_NAME 8

typedef struct _XIMAGE_SECTION_HEADER {
  uint8_t Name[XIMAGE_SIZEOF_SHORT_NAME];
  union {
    uint32_t PhysicalAddress;
    uint32_t VirtualSize;
  } Misc;
  uint32_t VirtualAddress;
  uint32_t SizeOfRawData;
  uint32_t PointerToRawData;
  uint32_t PointerToRelocations;
  uint32_t PointerToLinenumbers;
  uint16_t NumberOfRelocations;
  uint16_t NumberOfLinenumbers;
  uint32_t Characteristics;
} XIMAGE_SECTION_HEADER, *PXIMAGE_SECTION_HEADER;

#define XIMAGE_SIZEOF_SECTION_HEADER 40

typedef struct _XIMAGE_EXPORT_DIRECTORY {
  uint32_t Characteristics;
  uint32_t TimeDateStamp;
  uint16_t MajorVersion;
  uint16_t MinorVersion;
  uint32_t Name;
  uint32_t Base;
  uint32_t NumberOfFunctions;
  uint32_t NumberOfNames;
  uint32_t** AddressOfFunctions;
  uint32_t** AddressOfNames;
  uint16_t** AddressOfNameOrdinals;
} XIMAGE_EXPORT_DIRECTORY, *PXIMAGE_EXPORT_DIRECTORY;
