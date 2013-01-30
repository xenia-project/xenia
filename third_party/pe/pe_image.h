/* Xenia: minor tweaks to bring up to date with winnt.h */

/* Copyright (c) Microsoft Corporation. All rights reserved. */
/* NT image format (to be used when the Win32 SDK version of WINNT.H cannot) */

#ifndef UNALIGNED
/* BUGBUG fixme */
#define UNALIGNED
#endif

#ifdef _MSC_VER
#pragma pack (1)
#endif

// MODIFIED: on Windows a few definitions aren't in WINNT
#ifdef IMAGE_NT_SIGNATURE
#define IMAGE_SIZEOF_NT_OPTIONAL_HEADER      224
#define IMAGE_FILE_MACHINE_POWERPCBE         0x01F2  // IBM PowerPC Big-Endian
#endif

/*  Skip if WINNT.H already included.  We check IMAGE_NT_SIGNATURE instead of _WINNT_,
 *  because WinCE's version of WINNT.H defines _WINNT_ but it doesn't include everything here
 */
#ifndef IMAGE_NT_SIGNATURE

#define IMAGE_DOS_SIGNATURE                 0x5A4D      /* MZ */
#define IMAGE_OS2_SIGNATURE                 0x454E      /* NE */
#define IMAGE_OS2_SIGNATURE_LE              0x454C      /* LE */
#define IMAGE_NT_SIGNATURE                  0x00004550  /* PE00 */

typedef struct _IMAGE_DOS_HEADER {      /* DOS .EXE header */
    uint16_t   e_magic;                     /* Magic number */
    uint16_t   e_cblp;                      /* Bytes on last page of file */
    uint16_t   e_cp;                        /* Pages in file */
    uint16_t   e_crlc;                      /* Relocations */
    uint16_t   e_cparhdr;                   /* Size of header in paragraphs */
    uint16_t   e_minalloc;                  /* Minimum extra paragraphs needed */
    uint16_t   e_maxalloc;                  /* Maximum extra paragraphs needed */
    uint16_t   e_ss;                        /* Initial (relative) SS value */
    uint16_t   e_sp;                        /* Initial SP value */
    uint16_t   e_csum;                      /* Checksum */
    uint16_t   e_ip;                        /* Initial IP value */
    uint16_t   e_cs;                        /* Initial (relative) CS value */
    uint16_t   e_lfarlc;                    /* File address of relocation table */
    uint16_t   e_ovno;                      /* Overlay number */
    uint16_t   e_res[4];                    /* Reserved words */
    uint16_t   e_oemid;                     /* OEM identifier (for e_oeminfo) */
    uint16_t   e_oeminfo;                   /* OEM information; e_oemid specific */
    uint16_t   e_res2[10];                  /* Reserved words */
    int32_t   e_lfanew;                    /* File address of new exe header */
  } IMAGE_DOS_HEADER, *PIMAGE_DOS_HEADER;

typedef struct _IMAGE_OS2_HEADER {      /* OS/2 .EXE header */
    uint16_t   ne_magic;                    /* Magic number */
    int8_t    ne_ver;                      /* Version number */
    int8_t    ne_rev;                      /* Revision number */
    uint16_t   ne_enttab;                   /* Offset of Entry Table */
    uint16_t   ne_cbenttab;                 /* Number of bytes in Entry Table */
    int32_t   ne_crc;                      /* Checksum of whole file */
    uint16_t   ne_flags;                    /* Flag word */
    uint16_t   ne_autodata;                 /* Automatic data segment number */
    uint16_t   ne_heap;                     /* Initial heap allocation */
    uint16_t   ne_stack;                    /* Initial stack allocation */
    int32_t   ne_csip;                     /* Initial CS:IP setting */
    int32_t   ne_sssp;                     /* Initial SS:SP setting */
    uint16_t   ne_cseg;                     /* Count of file segments */
    uint16_t   ne_cmod;                     /* Entries in Module Reference Table */
    uint16_t   ne_cbnrestab;                /* Size of non-resident name table */
    uint16_t   ne_segtab;                   /* Offset of Segment Table */
    uint16_t   ne_rsrctab;                  /* Offset of Resource Table */
    uint16_t   ne_restab;                   /* Offset of resident name table */
    uint16_t   ne_modtab;                   /* Offset of Module Reference Table */
    uint16_t   ne_imptab;                   /* Offset of Imported Names Table */
    int32_t   ne_nrestab;                  /* Offset of Non-resident Names Table */
    uint16_t   ne_cmovent;                  /* Count of movable entries */
    uint16_t   ne_align;                    /* Segment alignment shift count */
    uint16_t   ne_cres;                     /* Count of resource segments */
    uint8_t   ne_exetyp;                   /* Target Operating system */
    uint8_t   ne_flagsothers;              /* Other .EXE flags */
    uint16_t   ne_pretthunks;               /* offset to return thunks */
    uint16_t   ne_psegrefbytes;             /* offset to segment ref. bytes */
    uint16_t   ne_swaparea;                 /* Minimum code swap area size */
    uint16_t   ne_expver;                   /* Expected Windows version number */
  } IMAGE_OS2_HEADER, *PIMAGE_OS2_HEADER;

/*
 * File header format.
 */

typedef struct _IMAGE_FILE_HEADER {
    uint16_t    Machine;
    uint16_t    NumberOfSections;
    uint32_t   TimeDateStamp;
    uint32_t   PointerToSymbolTable;
    uint32_t   NumberOfSymbols;
    uint16_t    SizeOfOptionalHeader;
    uint16_t    Characteristics;
} IMAGE_FILE_HEADER, *PIMAGE_FILE_HEADER;

#define IMAGE_SIZEOF_FILE_HEADER             20

#define IMAGE_FILE_RELOCS_STRIPPED           0x0001  /* Relocation info stripped from file. */
#define IMAGE_FILE_EXECUTABLE_IMAGE          0x0002  /* File is executable  (i.e. no unresolved externel references). */
#define IMAGE_FILE_LINE_NUMS_STRIPPED        0x0004  /* Line nunbers stripped from file. */
#define IMAGE_FILE_LOCAL_SYMS_STRIPPED       0x0008  /* Local symbols stripped from file. */
#define IMAGE_FILE_MINIMAL_OBJECT            0x0010  /* Reserved. */
#define IMAGE_FILE_UPDATE_OBJECT             0x0020  /* Reserved. */
#define IMAGE_FILE_16BIT_MACHINE             0x0040  /* 16 bit word machine. */
#define IMAGE_FILE_BYTES_REVERSED_LO         0x0080  /* Bytes of machine word are reversed. */
#define IMAGE_FILE_32BIT_MACHINE             0x0100  /* 32 bit word machine. */
#define IMAGE_FILE_DEBUG_STRIPPED            0x0200  /* Debugging info stripped from file in .DBG file */
#define IMAGE_FILE_PATCH                     0x0400  /* Reserved. */
#define IMAGE_FILE_SYSTEM                    0x1000  /* System File. */
#define IMAGE_FILE_DLL                       0x2000  /* File is a DLL. */
#define IMAGE_FILE_BYTES_REVERSED_HI         0x8000  /* Bytes of machine word are reversed. */

#define IMAGE_FILE_MACHINE_UNKNOWN           0
#define IMAGE_FILE_MACHINE_I860              0x14d   /* Intel 860. */
#define IMAGE_FILE_MACHINE_I386              0x14c   /* Intel 386. */
#define IMAGE_FILE_MACHINE_R3000             0x162   /* MIPS little-endian, 0540 big-endian */
#define IMAGE_FILE_MACHINE_R4000             0x166   /* MIPS little-endian */
#define IMAGE_FILE_MACHINE_R10000            0x0168  // MIPS little-endian
#define IMAGE_FILE_MACHINE_ALPHA             0x184   /* Alpha_AXP */
#define IMAGE_FILE_MACHINE_POWERPC           0x01F0  // IBM PowerPC Little-Endian
#define IMAGE_FILE_MACHINE_POWERPCBE         0x01F2  // IBM PowerPC Big-Endian
#define IMAGE_FILE_MACHINE_SH3               0x01a2  // SH3 little-endian
#define IMAGE_FILE_MACHINE_SH3E              0x01a4  // SH3E little-endian
#define IMAGE_FILE_MACHINE_SH4               0x01a6  // SH4 little-endian
#define IMAGE_FILE_MACHINE_ARM               0x01c0  // ARM Little-Endian
#define IMAGE_FILE_MACHINE_THUMB             0x01c2
#define IMAGE_FILE_MACHINE_IA64              0x0200  // Intel 64
#define IMAGE_FILE_MACHINE_MIPS16            0x0266  // MIPS
#define IMAGE_FILE_MACHINE_MIPSFPU           0x0366  // MIPS
#define IMAGE_FILE_MACHINE_MIPSFPU16         0x0466  // MIPS
#define IMAGE_FILE_MACHINE_ALPHA64           0x0284  // ALPHA64
#define IMAGE_FILE_MACHINE_AXP64             IMAGE_FILE_MACHINE_ALPHA64
#define IMAGE_FILE_MACHINE_CEF               0xC0EF

/*
 * Directory format.
 */

typedef struct _IMAGE_DATA_DIRECTORY {
    uint32_t   VirtualAddress;
    uint32_t   Size;
} IMAGE_DATA_DIRECTORY, *PIMAGE_DATA_DIRECTORY;

#define IMAGE_NUMBEROF_DIRECTORY_ENTRIES    16

/*
 * Optional header format.
 */

typedef struct _IMAGE_OPTIONAL_HEADER {
    /*
     * Standard fields.
     */

    uint16_t    Magic;
    uint8_t    MajorLinkerVersion;
    uint8_t    MinorLinkerVersion;
    uint32_t   SizeOfCode;
    uint32_t   SizeOfInitializedData;
    uint32_t   SizeOfUninitializedData;
    uint32_t   AddressOfEntryPoint;
    uint32_t   BaseOfCode;
    uint32_t   BaseOfData;

    /*
     * NT additional fields.
     */

    uint32_t   ImageBase;
    uint32_t   SectionAlignment;
    uint32_t   FileAlignment;
    uint16_t    MajorOperatingSystemVersion;
    uint16_t    MinorOperatingSystemVersion;
    uint16_t    MajorImageVersion;
    uint16_t    MinorImageVersion;
    uint16_t    MajorSubsystemVersion;
    uint16_t    MinorSubsystemVersion;
    uint32_t   Reserved1;
    uint32_t   SizeOfImage;
    uint32_t   SizeOfHeaders;
    uint32_t   CheckSum;
    uint16_t    Subsystem;
    uint16_t    DllCharacteristics;
    uint32_t   SizeOfStackReserve;
    uint32_t   SizeOfStackCommit;
    uint32_t   SizeOfHeapReserve;
    uint32_t   SizeOfHeapCommit;
    uint32_t   LoaderFlags;
    uint32_t   NumberOfRvaAndSizes;
    IMAGE_DATA_DIRECTORY DataDirectory[IMAGE_NUMBEROF_DIRECTORY_ENTRIES];
} IMAGE_OPTIONAL_HEADER, *PIMAGE_OPTIONAL_HEADER;

typedef IMAGE_OPTIONAL_HEADER IMAGE_OPTIONAL_HEADER32;

#define IMAGE_SIZEOF_STD_OPTIONAL_HEADER      28
#define IMAGE_SIZEOF_NT_OPTIONAL_HEADER      224

#define IMAGE_NT_OPTIONAL_HDR32_MAGIC       0x10b

#ifdef _WIN64
#define IMAGE_NT_OPTIONAL_HDR_MAGIC         IMAGE_NT_OPTIONAL_HDR64_MAGIC
#else
#define IMAGE_NT_OPTIONAL_HDR_MAGIC         IMAGE_NT_OPTIONAL_HDR32_MAGIC
#endif

typedef struct _IMAGE_NT_HEADERS {
    uint32_t Signature;
    IMAGE_FILE_HEADER FileHeader;
    IMAGE_OPTIONAL_HEADER OptionalHeader;
} IMAGE_NT_HEADERS, *PIMAGE_NT_HEADERS;

typedef IMAGE_NT_HEADERS IMAGE_NT_HEADERS32;

#define IMAGE_FIRST_SECTION( ntheader ) ((PIMAGE_SECTION_HEADER)        \
    ((uint8_t*)ntheader +                                               \
     offsetof( IMAGE_NT_HEADERS, OptionalHeader ) +                     \
     ((PIMAGE_NT_HEADERS)(ntheader))->FileHeader.SizeOfOptionalHeader   \
    ))


/* Subsystem Values */

#define IMAGE_SUBSYSTEM_UNKNOWN              0   /* Unknown subsystem. */
#define IMAGE_SUBSYSTEM_NATIVE               1   /* Image doesn't require a subsystem. */
#define IMAGE_SUBSYSTEM_WINDOWS_GUI          2   /* Image runs in the Windows GUI subsystem. */
#define IMAGE_SUBSYSTEM_WINDOWS_CUI          3   /* Image runs in the Windows character subsystem. */
#define IMAGE_SUBSYSTEM_OS2_CUI              5   /* image runs in the OS/2 character subsystem. */
#define IMAGE_SUBSYSTEM_POSIX_CUI            7   /* image run  in the Posix character subsystem. */
#define IMAGE_SUBSYSTEM_XBOX                14

/* Dll Characteristics */

#define IMAGE_LIBRARY_PROCESS_INIT           1   /* Dll has a process initialization routine. */
#define IMAGE_LIBRARY_PROCESS_TERM           2   /* Dll has a thread termination routine. */
#define IMAGE_LIBRARY_THREAD_INIT            4   /* Dll has a thread initialization routine. */
#define IMAGE_LIBRARY_THREAD_TERM            8   /* Dll has a thread termination routine. */

/*
 * Loader Flags
 */

#define IMAGE_LOADER_FLAGS_BREAK_ON_LOAD    0x00000001
#define IMAGE_LOADER_FLAGS_DEBUG_ON_LOAD    0x00000002


/* Directory Entries */

#define IMAGE_DIRECTORY_ENTRY_EXPORT         0   /* Export Directory */
#define IMAGE_DIRECTORY_ENTRY_IMPORT         1   /* Import Directory */
#define IMAGE_DIRECTORY_ENTRY_RESOURCE       2   /* Resource Directory */
#define IMAGE_DIRECTORY_ENTRY_EXCEPTION      3   /* Exception Directory */
#define IMAGE_DIRECTORY_ENTRY_SECURITY       4   /* Security Directory */
#define IMAGE_DIRECTORY_ENTRY_BASERELOC      5   /* Base Relocation Table */
#define IMAGE_DIRECTORY_ENTRY_DEBUG          6   /* Debug Directory */
#define IMAGE_DIRECTORY_ENTRY_COPYRIGHT      7   /* Description String */
#define IMAGE_DIRECTORY_ENTRY_GLOBALPTR      8   /* Machine Value (MIPS GP) */
#define IMAGE_DIRECTORY_ENTRY_TLS            9   /* TLS Directory */
#define IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG   10   /* Load Configuration Directory */

#ifdef USES_COMPLUS20
#define IMAGE_DIRECTORY_ENTRY_COMHEADER     14   /* COM+ data */
#endif


/*
 * Section header format.
 */

#define IMAGE_SIZEOF_SHORT_NAME              8

typedef struct _IMAGE_SECTION_HEADER {
    uint8_t    Name[IMAGE_SIZEOF_SHORT_NAME];
    union {
            uint32_t   PhysicalAddress;
            uint32_t   VirtualSize;
    } Misc;
    uint32_t   VirtualAddress;
    uint32_t   SizeOfRawData;
    uint32_t   PointerToRawData;
    uint32_t   PointerToRelocations;
    uint32_t   PointerToLinenumbers;
    uint16_t    NumberOfRelocations;
    uint16_t    NumberOfLinenumbers;
    uint32_t   Characteristics;
} IMAGE_SECTION_HEADER, *PIMAGE_SECTION_HEADER;

#define IMAGE_SIZEOF_SECTION_HEADER          40

#define IMAGE_SCN_TYPE_REGULAR               0x00000000  /*
#define IMAGE_SCN_TYPE_DUMMY                 0x00000001  // Reserved. */
#define IMAGE_SCN_TYPE_NO_LOAD               0x00000002  /* Reserved. */
#define IMAGE_SCN_TYPE_GROUPED               0x00000004  /* Used for 16-bit offset code. */
#define IMAGE_SCN_TYPE_NO_PAD                0x00000008  /* Reserved. */
#define IMAGE_SCN_TYPE_COPY                  0x00000010  /* Reserved. */

#define IMAGE_SCN_CNT_CODE                   0x00000020  /* Section contains code. */
#define IMAGE_SCN_CNT_INITIALIZED_DATA       0x00000040  /* Section contains initialized data. */
#define IMAGE_SCN_CNT_UNINITIALIZED_DATA     0x00000080  /* Section contains uninitialized data. */

#define IMAGE_SCN_LNK_OTHER                  0x00000100  /* Reserved. */
#define IMAGE_SCN_LNK_INFO                   0x00000200  /* Section contains comments or some other type of information. */
#define IMAGE_SCN_LNK_OVERLAY                0x00000400  /* Section contains an overlay. */
#define IMAGE_SCN_LNK_REMOVE                 0x00000800  /* Section contents will not become part of image. */
#define IMAGE_SCN_LNK_COMDAT                 0x00001000  /* Section contents comdat. */

#define IMAGE_SCN_ALIGN_1BYTES               0x00100000
#define IMAGE_SCN_ALIGN_2BYTES               0x00200000
#define IMAGE_SCN_ALIGN_4BYTES               0x00300000
#define IMAGE_SCN_ALIGN_8BYTES               0x00400000
#define IMAGE_SCN_ALIGN_16BYTES              0x00500000  /* Default alignment if no others are specified. */
#define IMAGE_SCN_ALIGN_32BYTES              0x00600000
#define IMAGE_SCN_ALIGN_64BYTES              0x00700000

#define IMAGE_SCN_MEM_DISCARDABLE            0x02000000  /* Section can be discarded. */
#define IMAGE_SCN_MEM_NOT_CACHED             0x04000000  /* Section is not cachable. */
#define IMAGE_SCN_MEM_NOT_PAGED              0x08000000  /* Section is not pageable. */
#define IMAGE_SCN_MEM_SHARED                 0x10000000  /* Section is shareable. */
#define IMAGE_SCN_MEM_EXECUTE                0x20000000  /* Section is executable. */
#define IMAGE_SCN_MEM_READ                   0x40000000  /* Section is readable. */
#define IMAGE_SCN_MEM_WRITE                  0x80000000  /* Section is writeable. */

/*
 * Symbol format.
 */

typedef struct _IMAGE_SYMBOL {
    union {
        uint8_t    ShortName[8];
        struct {
            uint32_t   Short;     /* if 0, use LongName */
            uint32_t   Long;      /* offset into string table */
        } Name;
        uint8_t*   LongName[2];
    } N;
    uint32_t    Value;
    int16_t     SectionNumber;
    uint16_t    Type;
    uint8_t     StorageClass;
    uint8_t     NumberOfAuxSymbols;
} IMAGE_SYMBOL;
typedef IMAGE_SYMBOL UNALIGNED *PIMAGE_SYMBOL;

#define IMAGE_SIZEOF_SYMBOL                  18

/*
 * Section values.
 *
 * Symbols have a section number of the section in which they are
 * defined. Otherwise, section numbers have the following meanings:
 */

#define IMAGE_SYM_UNDEFINED           (int16 )0           /* Symbol is undefined or is common. */
#define IMAGE_SYM_ABSOLUTE            (int16 )-1          /* Symbol is an absolute value. */
#define IMAGE_SYM_DEBUG               (int16 )-2          /* Symbol is a special debug item. */

/*
 * Type (fundamental) values.
 */

#define IMAGE_SYM_TYPE_NULL                  0           /* no type. */
#define IMAGE_SYM_TYPE_VOID                  1
#define IMAGE_SYM_TYPE_CHAR                  2           /* type character. */
#define IMAGE_SYM_TYPE_SHORT                 3           /* type short integer. */
#define IMAGE_SYM_TYPE_INT                   4
#define IMAGE_SYM_TYPE_LONG                  5
#define IMAGE_SYM_TYPE_FLOAT                 6
#define IMAGE_SYM_TYPE_DOUBLE                7
#define IMAGE_SYM_TYPE_STRUCT                8
#define IMAGE_SYM_TYPE_UNION                 9
#define IMAGE_SYM_TYPE_ENUM                  10          /* enumeration. */
#define IMAGE_SYM_TYPE_MOE                   11          /* member of enumeration. */
#define IMAGE_SYM_TYPE_UCHAR                 12
#define IMAGE_SYM_TYPE_USHORT                13
#define IMAGE_SYM_TYPE_UINT                  14
#define IMAGE_SYM_TYPE_ULONG                 15

/*
 * Type (derived) values.
 */

#define IMAGE_SYM_DTYPE_NULL                 0           /* no derived type. */
#define IMAGE_SYM_DTYPE_POINTER              1           /* pointer. */
#define IMAGE_SYM_DTYPE_FUNCTION             2           /* function. */
#define IMAGE_SYM_DTYPE_ARRAY                3           /* array. */

/*
 * Storage classes.
 */

#define IMAGE_SYM_CLASS_END_OF_FUNCTION      (uint8_t )-1
#define IMAGE_SYM_CLASS_NULL                 0
#define IMAGE_SYM_CLASS_AUTOMATIC            1
#define IMAGE_SYM_CLASS_EXTERNAL             2
#define IMAGE_SYM_CLASS_STATIC               3
#define IMAGE_SYM_CLASS_REGISTER             4
#define IMAGE_SYM_CLASS_EXTERNAL_DEF         5
#define IMAGE_SYM_CLASS_LABEL                6
#define IMAGE_SYM_CLASS_UNDEFINED_LABEL      7
#define IMAGE_SYM_CLASS_MEMBER_OF_STRUCT     8
#define IMAGE_SYM_CLASS_ARGUMENT             9
#define IMAGE_SYM_CLASS_STRUCT_TAG           10
#define IMAGE_SYM_CLASS_MEMBER_OF_UNION      11
#define IMAGE_SYM_CLASS_UNION_TAG            12
#define IMAGE_SYM_CLASS_TYPE_DEFINITION      13
#define IMAGE_SYM_CLASS_UNDEFINED_STATIC     14
#define IMAGE_SYM_CLASS_ENUM_TAG             15
#define IMAGE_SYM_CLASS_MEMBER_OF_ENUM       16
#define IMAGE_SYM_CLASS_REGISTER_PARAM       17
#define IMAGE_SYM_CLASS_BIT_FIELD            18
#define IMAGE_SYM_CLASS_BLOCK                100
#define IMAGE_SYM_CLASS_FUNCTION             101
#define IMAGE_SYM_CLASS_END_OF_STRUCT        102
#define IMAGE_SYM_CLASS_FILE                 103
#define IMAGE_SYM_CLASS_SECTION              104
#define IMAGE_SYM_CLASS_WEAK_EXTERNAL        105

/* type packing constants */

#define N_BTMASK                            017
#define N_TMASK                             060
#define N_TMASK1                            0300
#define N_TMASK2                            0360
#define N_BTSHFT                            4
#define N_TSHIFT                            2

/* MACROS */

/* Basic Type of  x */
#define BTYPE(x) ((x) & N_BTMASK)

/* Is x a pointer? */
#ifndef ISPTR
#define ISPTR(x) (((x) & N_TMASK) == (IMAGE_SYM_DTYPE_POINTER << N_BTSHFT))
#endif

/* Is x a function? */
#ifndef ISFCN
#define ISFCN(x) (((x) & N_TMASK) == (IMAGE_SYM_DTYPE_FUNCTION << N_BTSHFT))
#endif

/* Is x an array? */

#ifndef ISARY
#define ISARY(x) (((x) & N_TMASK) == (IMAGE_SYM_DTYPE_ARRAY << N_BTSHFT))
#endif

/* Is x a structure, union, or enumeration TAG? */
#ifndef ISTAG
#define ISTAG(x) ((x)==IMAGE_SYM_CLASS_STRUCT_TAG || (x)==IMAGE_SYM_CLASS_UNION_TAG || (x)==IMAGE_SYM_CLASS_ENUM_TAG)
#endif

#ifndef INCREF
#define INCREF(x) ((((x)&~N_BTMASK)<<N_TSHIFT)|(IMAGE_SYM_DTYPE_POINTER<<N_BTSHFT)|((x)&N_BTMASK))
#endif
#ifndef DECREF
#define DECREF(x) ((((x)>>N_TSHIFT)&~N_BTMASK)|((x)&N_BTMASK))
#endif

/*
 * Auxiliary entry format.
 */

typedef union _IMAGE_AUX_SYMBOL {
    struct {
        uint32_t    TagIndex;                      /* struct, union, or enum tag index */
        union {
            struct {
                uint16_t    Linenumber;             /* declaration line number */
                uint16_t    Size;                   /* size of struct, union, or enum */
            } LnSz;
           uint32_t    TotalSize;
        } Misc;
        union {
            struct {                            /* if ISFCN, tag, or .bb */
                uint32_t    PointerToLinenumber;
                uint32_t    PointerToNextFunction;
            } Function;
            struct {                            /* if ISARY, up to 4 dimen. */
                uint16_t     Dimension[4];
            } Array;
        } FcnAry;
        uint16_t    TvIndex;                        /* tv index */
    } Sym;
    struct {
        uint8_t    Name[IMAGE_SIZEOF_SYMBOL];
    } File;
    struct {
        uint32_t   Length;                         /* section length */
        uint16_t    NumberOfRelocations;            /* number of relocation entries */
        uint16_t    NumberOfLinenumbers;            /* number of line numbers */
        uint32_t   CheckSum;                       /* checksum for communal */
        int16_t    Number;                         /* section number to associate with */
        uint8_t    Selection;                      /* communal selection type */
    } Section;
} IMAGE_AUX_SYMBOL;
typedef IMAGE_AUX_SYMBOL UNALIGNED *PIMAGE_AUX_SYMBOL;

#define IMAGE_SIZEOF_AUX_SYMBOL              18

/*
 * Communal selection types.
 */

#define IMAGE_COMDAT_SELECT_UNKNOWN        0
#define IMAGE_COMDAT_SELECT_NODUPLICATES   1
#define IMAGE_COMDAT_SELECT_ANY            2
#define IMAGE_COMDAT_SELECT_SAME_SIZE      3
#define IMAGE_COMDAT_SELECT_EXACT_MATCH    4
#define IMAGE_COMDAT_SELECT_ASSOCIATIVE    5

#define IMAGE_WEAK_EXTERN_SEARCH_UNKNOWN   0
#define IMAGE_WEAK_EXTERN_SEARCH_NOLIBRARY 1
#define IMAGE_WEAK_EXTERN_SEARCH_LIBRARY   2


/*
 * Relocation format.
 */

typedef struct _IMAGE_RELOCATION {
    uint32_t   VirtualAddress;
    uint32_t   SymbolTableIndex;
    uint16_t    Type;
} IMAGE_RELOCATION;
typedef IMAGE_RELOCATION UNALIGNED *PIMAGE_RELOCATION;

#define IMAGE_SIZEOF_RELOCATION              10

/*
 * I860 relocation types.
 */

#define IMAGE_REL_I860_ABSOLUTE              0           /* Reference is absolute, no relocation is necessary */
#define IMAGE_REL_I860_DIR32                 06          /* Direct 32-bit reference to the symbols virtual address */
#define IMAGE_REL_I860_DIR32NB               07
#define IMAGE_REL_I860_SECTION               012
#define IMAGE_REL_I860_SECREL                013
#define IMAGE_REL_I860_PAIR                  034
#define IMAGE_REL_I860_HIGH                  036
#define IMAGE_REL_I860_LOW0                  037
#define IMAGE_REL_I860_LOW1                  040
#define IMAGE_REL_I860_LOW2                  041
#define IMAGE_REL_I860_LOW3                  042
#define IMAGE_REL_I860_LOW4                  043
#define IMAGE_REL_I860_SPLIT0                044
#define IMAGE_REL_I860_SPLIT1                045
#define IMAGE_REL_I860_SPLIT2                046
#define IMAGE_REL_I860_HIGHADJ               047
#define IMAGE_REL_I860_BRADDR                050

/*
 * I386 relocation types.
 */

#define IMAGE_REL_I386_ABSOLUTE              0           /* Reference is absolute, no relocation is necessary */
#define IMAGE_REL_I386_DIR16                 01          /* Direct 16-bit reference to the symbols virtual address */
#define IMAGE_REL_I386_REL16                 02          /* PC-relative 16-bit reference to the symbols virtual address */
#define IMAGE_REL_I386_DIR32                 06          /* Direct 32-bit reference to the symbols virtual address */
#define IMAGE_REL_I386_DIR32NB               07          /* Direct 32-bit reference to the symbols virtual address, base not included */
#define IMAGE_REL_I386_SEG12                 011         /* Direct 16-bit reference to the segment-selector bits of a 32-bit virtual address */
#define IMAGE_REL_I386_SECTION               012
#define IMAGE_REL_I386_SECREL                013
#define IMAGE_REL_I386_REL32                 024         /* PC-relative 32-bit reference to the symbols virtual address */

/*
 * MIPS relocation types.
 */

#define IMAGE_REL_MIPS_ABSOLUTE              0           /* Reference is absolute, no relocation is necessary */
#define IMAGE_REL_MIPS_REFHALF               01
#define IMAGE_REL_MIPS_REFWORD               02
#define IMAGE_REL_MIPS_JMPADDR               03
#define IMAGE_REL_MIPS_REFHI                 04
#define IMAGE_REL_MIPS_REFLO                 05
#define IMAGE_REL_MIPS_GPREL                 06
#define IMAGE_REL_MIPS_LITERAL               07
#define IMAGE_REL_MIPS_SECTION               012
#define IMAGE_REL_MIPS_SECREL                013
#define IMAGE_REL_MIPS_REFWORDNB             042
#define IMAGE_REL_MIPS_PAIR                  045

/*
 * Alpha Relocation types.
 */

#define IMAGE_REL_ALPHA_ABSOLUTE             0x0
#define IMAGE_REL_ALPHA_REFLONG              0x1
#define IMAGE_REL_ALPHA_REFQUAD              0x2
#define IMAGE_REL_ALPHA_GPREL32              0x3
#define IMAGE_REL_ALPHA_LITERAL              0x4
#define IMAGE_REL_ALPHA_LITUSE               0x5
#define IMAGE_REL_ALPHA_GPDISP               0x6
#define IMAGE_REL_ALPHA_BRADDR               0x7
#define IMAGE_REL_ALPHA_HINT                 0x8
#define IMAGE_REL_ALPHA_INLINE_REFLONG       0x9
#define IMAGE_REL_ALPHA_REFHI                0xA
#define IMAGE_REL_ALPHA_REFLO                0xB
#define IMAGE_REL_ALPHA_PAIR                 0xC
#define IMAGE_REL_ALPHA_MATCH                0xD
#define IMAGE_REL_ALPHA_SECTION              0xE
#define IMAGE_REL_ALPHA_SECREL               0xF
#define IMAGE_REL_ALPHA_REFLONGNB            0x10

/*
 * Based relocation format.
 */

typedef struct _IMAGE_BASE_RELOCATION {
    uint32_t   VirtualAddress;
    uint32_t   SizeOfBlock;
/*  uint16_t    TypeOffset[1]; */
} IMAGE_BASE_RELOCATION, *PIMAGE_BASE_RELOCATION;

#define IMAGE_SIZEOF_BASE_RELOCATION         8

/*
 * Based relocation types.
 */

#define IMAGE_REL_BASED_ABSOLUTE              0
#define IMAGE_REL_BASED_HIGH                  1
#define IMAGE_REL_BASED_LOW                   2
#define IMAGE_REL_BASED_HIGHLOW               3
#define IMAGE_REL_BASED_HIGHADJ               4
#define IMAGE_REL_BASED_MIPS_JMPADDR          5
#define IMAGE_REL_BASED_I860_BRADDR           6
#define IMAGE_REL_BASED_I860_SPLIT            7

/*
 * Line number format.
 */

typedef struct _IMAGE_LINENUMBER {
    union {
        uint32_t   SymbolTableIndex;                       /* Symbol table index of function name if Linenumber is 0. */
        uint32_t   VirtualAddress;                         /* Virtual address of line number. */
    } Type;
    uint16_t    Linenumber;                                 /* Line number. */
} IMAGE_LINENUMBER;
typedef IMAGE_LINENUMBER UNALIGNED *PIMAGE_LINENUMBER;

#define IMAGE_SIZEOF_LINENUMBER              6

/*
 * Archive format.
 */

#define IMAGE_ARCHIVE_START_SIZE             8
#define IMAGE_ARCHIVE_START                  "!<arch>\n"
#define IMAGE_ARCHIVE_END                    "`\n"
#define IMAGE_ARCHIVE_PAD                    "\n"
#define IMAGE_ARCHIVE_LINKER_MEMBER          "/               "
#define IMAGE_ARCHIVE_LONGNAMES_MEMBER       "//              "

typedef struct _IMAGE_ARCHIVE_MEMBER_HEADER {
    uint8_t     Name[16];                                  /* File member name - `/' terminated. */
    uint8_t     Date[12];                                  /* File member date - decimal. */
    uint8_t     UserID[6];                                 /* File member user id - decimal. */
    uint8_t     GroupID[6];                                /* File member group id - decimal. */
    uint8_t     Mode[8];                                   /* File member mode - octal. */
    uint8_t     Size[10];                                  /* File member size - decimal. */
    uint8_t     EndHeader[2];                              /* String to end header. */
} IMAGE_ARCHIVE_MEMBER_HEADER, *PIMAGE_ARCHIVE_MEMBER_HEADER;

#define IMAGE_SIZEOF_ARCHIVE_MEMBER_HDR      60

/*
 * DLL support.
 */

/*
 * Export Format
 */

typedef struct _IMAGE_EXPORT_DIRECTORY {
    uint32_t   Characteristics;
    uint32_t   TimeDateStamp;
    uint16_t   MajorVersion;
    uint16_t   MinorVersion;
    uint32_t   Name;
    uint32_t   Base;
    uint32_t   NumberOfFunctions;
    uint32_t   NumberOfNames;
    uint32_t   **AddressOfFunctions;
    uint32_t   **AddressOfNames;
    uint16_t   **AddressOfNameOrdinals;
} IMAGE_EXPORT_DIRECTORY, *PIMAGE_EXPORT_DIRECTORY;

/*
 * Import Format
 */

typedef struct _IMAGE_IMPORT_BY_NAME {
    uint16_t    Hint;
    uint8_t    Name[1];
} IMAGE_IMPORT_BY_NAME, *PIMAGE_IMPORT_BY_NAME;

typedef struct _IMAGE_THUNK_DATA {
    union {
        uint32_t *Function;
        uint32_t Ordinal;
        PIMAGE_IMPORT_BY_NAME AddressOfData;
    } u1;
} IMAGE_THUNK_DATA, *PIMAGE_THUNK_DATA;

#define IMAGE_ORDINAL_FLAG 0x80000000
#define IMAGE_SNAP_BY_ORDINAL(Ordinal) ((Ordinal & IMAGE_ORDINAL_FLAG) != 0)
#define IMAGE_ORDINAL(Ordinal) (Ordinal & 0xffff)

typedef struct _IMAGE_IMPORT_DESCRIPTOR {
    uint32_t   Characteristics;
    uint32_t   TimeDateStamp;
    uint32_t   ForwarderChain;
    uint32_t   Name;
    PIMAGE_THUNK_DATA FirstThunk;
} IMAGE_IMPORT_DESCRIPTOR, *PIMAGE_IMPORT_DESCRIPTOR;

/*
 * Thread Local Storage
 */

typedef void (*PIMAGE_TLS_CALLBACK) (
    void* DllHandle,
    uint32_t Reason,
    void* Reserved
    );

typedef struct _IMAGE_TLS_DIRECTORY {
    uint32_t   StartAddressOfRawData;
    uint32_t   EndAddressOfRawData;
    uint32_t   *AddressOfIndex;
    PIMAGE_TLS_CALLBACK *AddressOfCallBacks;
    uint32_t   SizeOfZeroFill;
    uint32_t   Characteristics;
} IMAGE_TLS_DIRECTORY, *PIMAGE_TLS_DIRECTORY;


/*
 * Resource Format.
 */

/*
 * Resource directory consists of two counts, following by a variable length
 * array of directory entries.  The first count is the number of entries at
 * beginning of the array that have actual names associated with each entry.
 * The entries are in ascending order, case insensitive strings.  The second
 * count is the number of entries that immediately follow the named entries.
 * This second count identifies the number of entries that have 31-bit integer
 * Ids as their name.  These entries are also sorted in ascending order.
 *
 * This structure allows fast lookup by either name or number, but for any
 * given resource entry only one form of lookup is supported, not both.
 * This is consistant with the syntax of the .RC file and the .RES file.
 */

typedef struct _IMAGE_RESOURCE_DIRECTORY {
    uint32_t   Characteristics;
    uint32_t   TimeDateStamp;
    uint16_t    MajorVersion;
    uint16_t    MinorVersion;
    uint16_t    NumberOfNamedEntries;
    uint16_t    NumberOfIdEntries;
/*  IMAGE_RESOURCE_DIRECTORY_ENTRY DirectoryEntries[]; */
} IMAGE_RESOURCE_DIRECTORY, *PIMAGE_RESOURCE_DIRECTORY;

#define IMAGE_RESOURCE_NAME_IS_STRING        0x80000000
#define IMAGE_RESOURCE_DATA_IS_DIRECTORY     0x80000000

/*
 * Each directory contains the 32-bit Name of the entry and an offset,
 * relative to the beginning of the resource directory of the data associated
 * with this directory entry.  If the name of the entry is an actual text
 * string instead of an integer Id, then the high order bit of the name field
 * is set to one and the low order 31-bits are an offset, relative to the
 * beginning of the resource directory of the string, which is of type
 * IMAGE_RESOURCE_DIRECTORY_STRING.  Otherwise the high bit is clear and the
 * low-order 31-bits are the integer Id that identify this resource directory
 * entry. If the directory entry is yet another resource directory (i.e. a
 * subdirectory), then the high order bit of the offset field will be
 * set to indicate this.  Otherwise the high bit is clear and the offset
 * field points to a resource data entry.
 */

typedef struct _IMAGE_RESOURCE_DIRECTORY_ENTRY {
    uint32_t   Name;
    uint32_t   OffsetToData;
} IMAGE_RESOURCE_DIRECTORY_ENTRY, *PIMAGE_RESOURCE_DIRECTORY_ENTRY;

/*
 * For resource directory entries that have actual string names, the Name
 * field of the directory entry points to an object of the following type.
 * All of these string objects are stored together after the last resource
 * directory entry and before the first resource data object.  This minimizes
 * the impact of these variable length objects on the alignment of the fixed
 * size directory entry objects.
 */

typedef struct _IMAGE_RESOURCE_DIRECTORY_STRING {
    uint16_t    Length;
    char    NameString[ 1 ];
} IMAGE_RESOURCE_DIRECTORY_STRING, *PIMAGE_RESOURCE_DIRECTORY_STRING;


typedef struct _IMAGE_RESOURCE_DIR_STRING_U {
    uint16_t      Length;
    wchar_t NameString[ 1 ];
} IMAGE_RESOURCE_DIR_STRING_U, *PIMAGE_RESOURCE_DIR_STRING_U;

/*
 * Each resource data entry describes a leaf node in the resource directory
 * tree.  It contains an offset, relative to the beginning of the resource
 * directory of the data for the resource, a size field that gives the number
 * of bytes of data at that offset, a CodePage that should be used when
 * decoding code point values within the resource data.  Typically for new
 * applications the code page would be the unicode code page.
 */

typedef struct _IMAGE_RESOURCE_DATA_ENTRY {
    uint32_t   OffsetToData;
    uint32_t   Size;
    uint32_t   CodePage;
    uint32_t   Reserved;
} IMAGE_RESOURCE_DATA_ENTRY, *PIMAGE_RESOURCE_DATA_ENTRY;

/*
 * Load Configuration Directory Entry
 */

typedef struct _IMAGE_LOAD_CONFIG_DIRECTORY {
    uint32_t   Characteristics;
    uint32_t   TimeDateStamp;
    uint16_t    MajorVersion;
    uint16_t    MinorVersion;
    uint32_t   GlobalFlagsClear;
    uint32_t   GlobalFlagsSet;
    uint32_t   CriticalSectionDefaultTimeout;
    uint32_t   DeCommitFreeBlockThreshold;
    uint32_t   DeCommitTotalFreeThreshold;
    uint32_t   Reserved[ 8 ];
} IMAGE_LOAD_CONFIG_DIRECTORY, *PIMAGE_LOAD_CONFIG_DIRECTORY;


/*
 * Function table entry format for MIPS/ALPHA images.  Function table is
 * pointed to by the IMAGE_DIRECTORY_ENTRY_EXCEPTION directory entry.
 * This definition duplicates ones in ntmips.h and ntalpha.h for use
 * by portable image file mungers.
 */

typedef struct _IMAGE_RUNTIME_FUNCTION_ENTRY {
    uint32_t BeginAddress;
    uint32_t EndAddress;
    void*  ExceptionHandler;
    void*  HandlerData;
    uint32_t PrologEndAddress;
} IMAGE_RUNTIME_FUNCTION_ENTRY, *PIMAGE_RUNTIME_FUNCTION_ENTRY;

/*
 * Debug Format
 */

typedef struct _IMAGE_DEBUG_DIRECTORY {
    uint32_t   Characteristics;
    uint32_t   TimeDateStamp;
    uint16_t    MajorVersion;
    uint16_t    MinorVersion;
    uint32_t   Type;
    uint32_t   SizeOfData;
    uint32_t   AddressOfRawData;
    uint32_t   PointerToRawData;
} IMAGE_DEBUG_DIRECTORY, *PIMAGE_DEBUG_DIRECTORY;

#define IMAGE_DEBUG_TYPE_UNKNOWN          0
#define IMAGE_DEBUG_TYPE_COFF             1
#define IMAGE_DEBUG_TYPE_CODEVIEW         2
#define IMAGE_DEBUG_TYPE_FPO              3
#define IMAGE_DEBUG_TYPE_MISC             4
#define IMAGE_DEBUG_TYPE_EXCEPTION        5
#define IMAGE_DEBUG_TYPE_FIXUP            6
#define IMAGE_DEBUG_TYPE_RESERVED6        7
#define IMAGE_DEBUG_TYPE_RESERVED7        8

typedef struct _IMAGE_COFF_SYMBOLS_HEADER {
    uint32_t   NumberOfSymbols;
    uint32_t   LvaToFirstSymbol;
    uint32_t   NumberOfLinenumbers;
    uint32_t   LvaToFirstLinenumber;
    uint32_t   RvaToFirstByteOfCode;
    uint32_t   RvaToLastByteOfCode;
    uint32_t   RvaToFirstByteOfData;
    uint32_t   RvaToLastByteOfData;
} IMAGE_COFF_SYMBOLS_HEADER, *PIMAGE_COFF_SYMBOLS_HEADER;

#define FRAME_FPO   0
#define FRAME_TRAP  1
#define FRAME_TSS   2

#ifdef _MSC_VER
#pragma warning(disable:4214)
#endif
typedef struct _FPO_DATA {
    uint32_t       ulOffStart;             /* offset 1st byte of function code */
    uint32_t       cbProcSize;             /* # bytes in function */
    uint32_t       cdwLocals;              /* # bytes in locals/4 */
    uint16_t        cdwParams;              /* # bytes in params/4 */
    uint16_t        cbProlog : 8;           /* # bytes in prolog */
    uint16_t        cbRegs   : 3;           /* # regs saved */
    uint16_t        fHasSEH  : 1;           /* TRUE if SEH in func */
    uint16_t        fUseBP   : 1;           /* TRUE if EBP has been allocated */
    uint16_t        reserved : 1;           /* reserved for future use */
    uint16_t        cbFrame  : 2;           /* frame type */
} FPO_DATA, *PFPO_DATA;
#define SIZEOF_RFPO_DATA 16
#ifdef _MSC_VER
#pragma warning(default:4214)
#endif

#define IMAGE_DEBUG_MISC_EXENAME    1

typedef struct _IMAGE_DEBUG_MISC {
    uint32_t       DataType;               /* type of misc data, see defines */
    uint32_t       Length;                 /* total length of record, rounded to four */
                                        /* byte multiple. */
    uint8_t        Unicode;                /* TRUE if data is unicode string */
    uint8_t        Reserved[ 3 ];
    uint8_t        Data[ 1 ];              /* Actual data */
} IMAGE_DEBUG_MISC, *PIMAGE_DEBUG_MISC;


/*
 * Debugging information can be stripped from an image file and placed
 * in a separate .DBG file, whose file name part is the same as the
 * image file name part (e.g. symbols for CMD.EXE could be stripped
 * and placed in CMD.DBG).  This is indicated by the IMAGE_FILE_DEBUG_STRIPPED
 * flag in the Characteristics field of the file header.  The beginning of
 * the .DBG file contains the following structure which captures certain
 * information from the image file.  This allows a debug to proceed even if
 * the original image file is not accessable.  This header is followed by
 * zero of more IMAGE_SECTION_HEADER structures, followed by zero or more
 * IMAGE_DEBUG_DIRECTORY structures.  The latter structures and those in
 * the image file contain file offsets relative to the beginning of the
 * .DBG file.
 *
 * If symbols have been stripped from an image, the IMAGE_DEBUG_MISC structure
 * is left in the image file, but not mapped.  This allows a debugger to
 * compute the name of the .DBG file, from the name of the image in the
 * IMAGE_DEBUG_MISC structure.
 */

typedef struct _IMAGE_SEPARATE_DEBUG_HEADER {
    uint16_t       Signature;
    uint16_t       Flags;
    uint16_t       Machine;
    uint16_t       Characteristics;
    uint32_t       TimeDateStamp;
    uint32_t       CheckSum;
    uint32_t       ImageBase;
    uint32_t       SizeOfImage;
    uint32_t       NumberOfSections;
    uint32_t       ExportedNamesSize;
    uint32_t       DebugDirectorySize;
    uint32_t       Reserved[ 3 ];
} IMAGE_SEPARATE_DEBUG_HEADER, *PIMAGE_SEPARATE_DEBUG_HEADER;

#define IMAGE_SEPARATE_DEBUG_SIGNATURE 0x4944

#endif /* IMAGE_NT_SIGNATURE */


#ifdef USES_COMPLUS20
/*
 * COM+ 2.0 header structure.
 */
typedef struct IMAGE_COR20_HEADER
{
    /* Header versioning */
    uint32_t                  cb;
    uint16_t                  MajorRuntimeVersion;
    uint16_t                  MinorRuntimeVersion;

    /* Symbol table and startup information */
    IMAGE_DATA_DIRECTORY    MetaData;
    uint32_t                  Flags;
    uint32_t                  EntryPointToken;

    /* Binding information */
    IMAGE_DATA_DIRECTORY    Resources;
    IMAGE_DATA_DIRECTORY    StrongNameSignature;

    /* Regular fixup and binding information */
    IMAGE_DATA_DIRECTORY    CodeManagerTable;
    IMAGE_DATA_DIRECTORY    VTableFixups;
    IMAGE_DATA_DIRECTORY    ExportAddressTableJumps;

    /* Managed Native Code */
    IMAGE_DATA_DIRECTORY    EEInfoTable;
    IMAGE_DATA_DIRECTORY    HelperTable;
    IMAGE_DATA_DIRECTORY    DynamicInfo;
    IMAGE_DATA_DIRECTORY    DelayLoadInfo;
    IMAGE_DATA_DIRECTORY    ModuleImage;
    IMAGE_DATA_DIRECTORY    ExternalFixups;
    IMAGE_DATA_DIRECTORY    RidMap;
    IMAGE_DATA_DIRECTORY    DebugMap;

    /*@Todo: this is obsolete, being replaced by pdata in the PE header.*/
    IMAGE_DATA_DIRECTORY    IPMap;

} IMAGE_COR20_HEADER;

#endif  /* USES_COMPLUS20 */

#ifdef _MSC_VER
#pragma pack ()
#endif

/*
 * End Image Format
 */
