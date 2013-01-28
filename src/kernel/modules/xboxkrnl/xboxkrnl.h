/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_MODULES_XBOXKRNL_H_
#define XENIA_KERNEL_MODULES_XBOXKRNL_H_

#include <xenia/common.h>
#include <xenia/core.h>


namespace xe {
namespace kernel {
namespace xboxkrnl {


// NT_STATUS (STATUS_*)
// http://msdn.microsoft.com/en-us/library/cc704588.aspx
// Adding as needed.
#define X_STAUTS_SUCCESS                                ((uint32_t)0x00000000L)
#define X_STATUS_UNSUCCESSFUL                           ((uint32_t)0xC0000001L)
#define X_STATUS_NOT_IMPLEMENTED                        ((uint32_t)0xC0000002L)
#define X_STATUS_ACCESS_VIOLATION                       ((uint32_t)0xC0000005L)
#define X_STATUS_INVALID_HANDLE                         ((uint32_t)0xC0000008L)
#define X_STATUS_INVALID_PARAMETER                      ((uint32_t)0xC000000DL)
#define X_STATUS_NO_MEMORY                              ((uint32_t)0xC0000017L)
#define X_STATUS_ALREADY_COMMITTED                      ((uint32_t)0xC0000021L)
#define X_STATUS_ACCESS_DENIED                          ((uint32_t)0xC0000022L)
#define X_STATUS_BUFFER_TOO_SMALL                       ((uint32_t)0xC0000023L)
#define X_STATUS_OBJECT_TYPE_MISMATCH                   ((uint32_t)0xC0000024L)
#define X_STATUS_INVALID_PAGE_PROTECTION                ((uint32_t)0xC0000045L)


// MEM_*, used by NtAllocateVirtualMemory
#define X_MEM_COMMIT          0x00001000
#define X_MEM_RESERVE         0x00002000
#define X_MEM_DECOMMIT        0x00004000
#define X_MEM_RELEASE         0x00008000
#define X_MEM_FREE            0x00010000
#define X_MEM_PRIVATE         0x00020000
#define X_MEM_RESET           0x00080000
#define X_MEM_TOP_DOWN        0x00100000
#define X_MEM_NOZERO          0x00800000
#define X_MEM_LARGE_PAGES     0x20000000
#define X_MEM_HEAP            0x40000000
#define X_MEM_16MB_PAGES      0x80000000 // from Valve SDK


// PAGE_*, used by NtAllocateVirtualMemory
#define X_PAGE_NOACCESS       0x00000001
#define X_PAGE_READONLY       0x00000002
#define X_PAGE_READWRITE      0x00000004
#define X_PAGE_WRITECOPY      0x00000008
// *_EXECUTE_* bits omitted, as user code can't mark pages as executable.
#define X_PAGE_GUARD          0x00000100
#define X_PAGE_NOCACHE        0x00000200
#define X_PAGE_WRITECOMBINE   0x00000400


// (?), used by KeGetCurrentProcessType
#define X_PROCTYPE_IDLE   0
#define X_PROCTYPE_USER   1
#define X_PROCTYPE_SYSTEM 2


}  // namespace xboxkrnl
}  // namespace kernel
}  // namespace xe


#endif  // XENIA_KERNEL_MODULES_XBOXKRNL_H_
