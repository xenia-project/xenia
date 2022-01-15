/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

// This is a partial file designed to be included by other files when
// constructing various tables.

#ifndef XE_XMA_REGISTER
#define XE_XMA_REGISTER(index, name)
#define __XE_XMA_REGISTER_UNSET
#endif

#ifndef XE_XMA_REGISTER_CONTEXT_GROUP
#define XE_XMA_REGISTER_CONTEXT_GROUP(index, suffix) \
  XE_XMA_REGISTER(index + 0, Context0##suffix)       \
  XE_XMA_REGISTER(index + 1, Context1##suffix)       \
  XE_XMA_REGISTER(index + 2, Context2##suffix)       \
  XE_XMA_REGISTER(index + 3, Context3##suffix)       \
  XE_XMA_REGISTER(index + 4, Context4##suffix)       \
  XE_XMA_REGISTER(index + 5, Context5##suffix)       \
  XE_XMA_REGISTER(index + 6, Context6##suffix)       \
  XE_XMA_REGISTER(index + 7, Context7##suffix)       \
  XE_XMA_REGISTER(index + 8, Context8##suffix)       \
  XE_XMA_REGISTER(index + 9, Context9##suffix)
#endif

// 0x0000..0x001F : ???
// 0x0020..0x03FF : all 0xFFs?
// 0x0400..0x043F : ???
// 0x0440..0x047F : all 0xFFs?
// 0x0480..0x048B : ???
// 0x048C..0x04C0 : all 0xFFs?
// 0x04C1..0x04CB : ???
// 0x04CC..0x04FF : all 0xFFs?
// 0x0500..0x051F : ???
// 0x0520..0x057F : all 0xFFs?
// 0x0580..0x058F : ???
// 0x0590..0x05FF : all 0xFFs?

// XMA stuff is probably only 0x0600..0x06FF
//---------------------------------------------------------------------------//

XE_XMA_REGISTER(0x0600, ContextArrayAddress)
// 0x0601..0x0605 : ???
XE_XMA_REGISTER(0x0606, CurrentContextIndex)
XE_XMA_REGISTER(0x0607, NextContextIndex)
// 0x0608         : ???
// 0x0609..0x060F : zero?
XE_XMA_REGISTER_CONTEXT_GROUP(0x0610, Unknown610)
// 0x061A..0x061F : zero?
XE_XMA_REGISTER_CONTEXT_GROUP(0x0620, Unknown620)
// 0x062A..0x0641 : zero?
// 0x0642..0x0644 : ???
// 0x0645..0x064F : zero?
XE_XMA_REGISTER_CONTEXT_GROUP(0x0650, Kick)
// 0x065A..0x065F : zero?
XE_XMA_REGISTER_CONTEXT_GROUP(0x0660, Unknown660)
// 0x066A..0x0681 : zero?
// 0x0682..0x0684 : ???
// 0x0685..0x068F : zero?
XE_XMA_REGISTER_CONTEXT_GROUP(0x0690, Lock)
// 0x069A..0x069F : zero?
XE_XMA_REGISTER_CONTEXT_GROUP(0x06A0, Clear)

//---------------------------------------------------------------------------//

// 0x0700..0x07FF : all 0xFFs?
// 0x0800..0x17FF : ???
// 0x1800..0x2FFF : all 0xFFs?
// 0x3000..0x30FF : ???
// 0x3100..0x3FFF : all 0xFFs?

#ifdef __XE_XMA_REGISTER_UNSET
#undef __XE_XMA_REGISTER_UNSET
#undef XE_XMA_REGISTER
#endif
