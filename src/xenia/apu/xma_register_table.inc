/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

// This is a partial file designed to be included by other files when
// constructing various tables.

//#define XE_XMA_REGISTER(index, type, name)

XE_XMA_REGISTER(0x0600, kDword, CONTEXT_ARRAY_ADDRESS)

XE_XMA_REGISTER(0x0606, kDword, CURRENT_CONTEXT_INDEX)
XE_XMA_REGISTER(0x0607, kDword, NEXT_CONTEXT_INDEX)

XE_XMA_REGISTER(0x0650, kDword, CONTEXT_KICK_0)
XE_XMA_REGISTER(0x0651, kDword, CONTEXT_KICK_1)
XE_XMA_REGISTER(0x0652, kDword, CONTEXT_KICK_2)
XE_XMA_REGISTER(0x0653, kDword, CONTEXT_KICK_3)
XE_XMA_REGISTER(0x0654, kDword, CONTEXT_KICK_4)
XE_XMA_REGISTER(0x0655, kDword, CONTEXT_KICK_5)
XE_XMA_REGISTER(0x0656, kDword, CONTEXT_KICK_6)
XE_XMA_REGISTER(0x0657, kDword, CONTEXT_KICK_7)
XE_XMA_REGISTER(0x0658, kDword, CONTEXT_KICK_8)
XE_XMA_REGISTER(0x0659, kDword, CONTEXT_KICK_9)

XE_XMA_REGISTER(0x0690, kDword, CONTEXT_LOCK_0)
XE_XMA_REGISTER(0x0691, kDword, CONTEXT_LOCK_1)
XE_XMA_REGISTER(0x0692, kDword, CONTEXT_LOCK_2)
XE_XMA_REGISTER(0x0693, kDword, CONTEXT_LOCK_3)
XE_XMA_REGISTER(0x0694, kDword, CONTEXT_LOCK_4)
XE_XMA_REGISTER(0x0695, kDword, CONTEXT_LOCK_5)
XE_XMA_REGISTER(0x0696, kDword, CONTEXT_LOCK_6)
XE_XMA_REGISTER(0x0697, kDword, CONTEXT_LOCK_7)
XE_XMA_REGISTER(0x0698, kDword, CONTEXT_LOCK_8)
XE_XMA_REGISTER(0x0699, kDword, CONTEXT_LOCK_9)

XE_XMA_REGISTER(0x06A0, kDword, CONTEXT_CLEAR_0)
XE_XMA_REGISTER(0x06A1, kDword, CONTEXT_CLEAR_1)
XE_XMA_REGISTER(0x06A2, kDword, CONTEXT_CLEAR_2)
XE_XMA_REGISTER(0x06A3, kDword, CONTEXT_CLEAR_3)
XE_XMA_REGISTER(0x06A4, kDword, CONTEXT_CLEAR_4)
XE_XMA_REGISTER(0x06A5, kDword, CONTEXT_CLEAR_5)
XE_XMA_REGISTER(0x06A6, kDword, CONTEXT_CLEAR_6)
XE_XMA_REGISTER(0x06A7, kDword, CONTEXT_CLEAR_7)
XE_XMA_REGISTER(0x06A8, kDword, CONTEXT_CLEAR_8)
XE_XMA_REGISTER(0x06A9, kDword, CONTEXT_CLEAR_9)
