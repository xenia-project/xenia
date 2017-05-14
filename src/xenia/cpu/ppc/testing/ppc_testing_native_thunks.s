/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2017 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

# r3 = context
# this does not touch r1, r3, r4, r13
.load_registers_ctx:
  lwz   r2, 0x400(r3) # CR
  mtcrf 0xFF, r2

  lfd   f0, 0x404(r3) # FPSCR
  mtfsf 0xFF, f0

  li  r2, 0
  mtxer r2

  # Altivec registers (up to 32)
  li    r2, 0x200
  lvx   v0, r3, r2
  addi  r2, r2, 16
  lvx   v1, r3, r2
  addi  r2, r2, 16
  lvx   v2, r3, r2
  addi  r2, r2, 16
  lvx   v3, r3, r2
  addi  r2, r2, 16
  lvx   v4, r3, r2
  addi  r2, r2, 16
  lvx   v5, r3, r2
  addi  r2, r2, 16
  lvx   v6, r3, r2
  addi  r2, r2, 16
  lvx   v7, r3, r2
  addi  r2, r2, 16
  lvx   v8, r3, r2
  addi  r2, r2, 16
  lvx   v9, r3, r2
  addi  r2, r2, 16
  lvx   v10, r3, r2
  addi  r2, r2, 16
  lvx   v11, r3, r2
  addi  r2, r2, 16
  lvx   v12, r3, r2
  addi  r2, r2, 16
  lvx   v13, r3, r2
  addi  r2, r2, 16
  lvx   v14, r3, r2
  addi  r2, r2, 16
  lvx   v15, r3, r2
  addi  r2, r2, 16
  lvx   v16, r3, r2
  addi  r2, r2, 16
  lvx   v17, r3, r2
  addi  r2, r2, 16
  lvx   v18, r3, r2
  addi  r2, r2, 16
  lvx   v19, r3, r2
  addi  r2, r2, 16
  lvx   v20, r3, r2
  addi  r2, r2, 16
  lvx   v21, r3, r2
  addi  r2, r2, 16
  lvx   v22, r3, r2
  addi  r2, r2, 16
  lvx   v23, r3, r2
  addi  r2, r2, 16
  lvx   v24, r3, r2
  addi  r2, r2, 16
  lvx   v25, r3, r2
  addi  r2, r2, 16
  lvx   v26, r3, r2
  addi  r2, r2, 16
  lvx   v27, r3, r2
  addi  r2, r2, 16
  lvx   v28, r3, r2
  addi  r2, r2, 16
  lvx   v29, r3, r2
  addi  r2, r2, 16
  lvx   v30, r3, r2
  addi  r2, r2, 16
  lvx   v31, r3, r2

  ld r0,  0x00(r3)
  # r1 cannot be used
  ld r2,  0x10(r3)
  # r3 will be loaded before the call
  # r4 will be loaded before the call
  ld r5,  0x28(r3)
  ld r6,  0x30(r3)
  ld r7,  0x38(r3)
  ld r8,  0x40(r3)
  ld r9,  0x48(r3)
  ld r10, 0x50(r3)
  ld r11, 0x58(r3)
  ld r12, 0x60(r3)
  # r13 cannot be used (OS use only)
  ld r14, 0x70(r3)
  ld r15, 0x78(r3)
  ld r16, 0x80(r3)
  ld r17, 0x88(r3)
  ld r18, 0x90(r3)
  ld r19, 0x98(r3)
  ld r20, 0xA0(r3)
  ld r21, 0xA8(r3)
  ld r22, 0xB0(r3)
  ld r23, 0xB8(r3)
  ld r24, 0xC0(r3)
  ld r25, 0xC8(r3)
  ld r26, 0xD0(r3)
  ld r27, 0xD8(r3)
  ld r28, 0xE0(r3)
  ld r29, 0xE8(r3)
  ld r30, 0xF0(r3)
  ld r31, 0xF8(r3)

  lfd f0,  0x100(r3)
  lfd f1,  0x108(r3)
  lfd f2,  0x110(r3)
  lfd f3,  0x118(r3)
  lfd f4,  0x120(r3)
  lfd f5,  0x128(r3)
  lfd f6,  0x130(r3)
  lfd f7,  0x138(r3)
  lfd f8,  0x140(r3)
  lfd f9,  0x148(r3)
  lfd f10, 0x150(r3)
  lfd f11, 0x158(r3)
  lfd f12, 0x160(r3)
  lfd f13, 0x168(r3)
  lfd f14, 0x170(r3)
  lfd f15, 0x178(r3)
  lfd f16, 0x180(r3)
  lfd f17, 0x188(r3)
  lfd f18, 0x190(r3)
  lfd f19, 0x198(r3)
  lfd f20, 0x1A0(r3)
  lfd f21, 0x1A8(r3)
  lfd f22, 0x1B0(r3)
  lfd f23, 0x1B8(r3)
  lfd f24, 0x1C0(r3)
  lfd f25, 0x1C8(r3)
  lfd f26, 0x1D0(r3)
  lfd f27, 0x1D8(r3)
  lfd f28, 0x1E0(r3)
  lfd f29, 0x1E8(r3)
  lfd f30, 0x1F0(r3)
  lfd f31, 0x1F8(r3)
  blr

# r3 = context
# this does not save r1, r3, r13
.save_registers_ctx:
  std r0,  0x00(r3)
  # r1 cannot be used
  std r2,  0x10(r3)
  # r3 will be saved later
  std r4,  0x20(r3)
  std r5,  0x28(r3)
  std r6,  0x30(r3)
  std r7,  0x38(r3)
  std r8,  0x40(r3)
  std r9,  0x48(r3)
  std r10, 0x50(r3)
  std r11, 0x58(r3)
  std r12, 0x60(r3)
  # r13 cannot be used (OS use only)
  std r14, 0x70(r3)
  std r15, 0x78(r3)
  std r16, 0x80(r3)
  std r17, 0x88(r3)
  std r18, 0x90(r3)
  std r19, 0x98(r3)
  std r20, 0xA0(r3)
  std r21, 0xA8(r3)
  std r22, 0xB0(r3)
  std r23, 0xB8(r3)
  std r24, 0xC0(r3)
  std r25, 0xC8(r3)
  std r26, 0xD0(r3)
  std r27, 0xD8(r3)
  std r28, 0xE0(r3)
  std r29, 0xE8(r3)
  std r30, 0xF0(r3)
  std r31, 0xF8(r3)

  stfd f0,  0x100(r3)
  stfd f1,  0x108(r3)
  stfd f2,  0x110(r3)
  stfd f3,  0x118(r3)
  stfd f4,  0x120(r3)
  stfd f5,  0x128(r3)
  stfd f6,  0x130(r3)
  stfd f7,  0x138(r3)
  stfd f8,  0x140(r3)
  stfd f9,  0x148(r3)
  stfd f10, 0x150(r3)
  stfd f11, 0x158(r3)
  stfd f12, 0x160(r3)
  stfd f13, 0x168(r3)
  stfd f14, 0x170(r3)
  stfd f15, 0x178(r3)
  stfd f16, 0x180(r3)
  stfd f17, 0x188(r3)
  stfd f18, 0x190(r3)
  stfd f19, 0x198(r3)
  stfd f20, 0x1A0(r3)
  stfd f21, 0x1A8(r3)
  stfd f22, 0x1B0(r3)
  stfd f23, 0x1B8(r3)
  stfd f24, 0x1C0(r3)
  stfd f25, 0x1C8(r3)
  stfd f26, 0x1D0(r3)
  stfd f27, 0x1D8(r3)
  stfd f28, 0x1E0(r3)
  stfd f29, 0x1E8(r3)
  stfd f30, 0x1F0(r3)
  stfd f31, 0x1F8(r3)

  # Altivec registers (up to 32)
  li    r2, 0x200
  stvx  v0, r3, r2
  addi  r2, r2, 16
  stvx  v1, r3, r2
  addi  r2, r2, 16
  stvx  v2, r3, r2
  addi  r2, r2, 16
  stvx  v3, r3, r2
  addi  r2, r2, 16
  stvx  v4, r3, r2
  addi  r2, r2, 16
  stvx  v5, r3, r2
  addi  r2, r2, 16
  stvx  v6, r3, r2
  addi  r2, r2, 16
  stvx  v7, r3, r2
  addi  r2, r2, 16
  stvx  v8, r3, r2
  addi  r2, r2, 16
  stvx  v9, r3, r2
  addi  r2, r2, 16
  stvx  v10, r3, r2
  addi  r2, r2, 16
  stvx  v11, r3, r2
  addi  r2, r2, 16
  stvx  v12, r3, r2
  addi  r2, r2, 16
  stvx  v13, r3, r2
  addi  r2, r2, 16
  stvx  v14, r3, r2
  addi  r2, r2, 16
  stvx  v15, r3, r2
  addi  r2, r2, 16
  stvx  v16, r3, r2
  addi  r2, r2, 16
  stvx  v17, r3, r2
  addi  r2, r2, 16
  stvx  v18, r3, r2
  addi  r2, r2, 16
  stvx  v19, r3, r2
  addi  r2, r2, 16
  stvx  v20, r3, r2
  addi  r2, r2, 16
  stvx  v21, r3, r2
  addi  r2, r2, 16
  stvx  v22, r3, r2
  addi  r2, r2, 16
  stvx  v23, r3, r2
  addi  r2, r2, 16
  stvx  v24, r3, r2
  addi  r2, r2, 16
  stvx  v25, r3, r2
  addi  r2, r2, 16
  stvx  v26, r3, r2
  addi  r2, r2, 16
  stvx  v27, r3, r2
  addi  r2, r2, 16
  stvx  v28, r3, r2
  addi  r2, r2, 16
  stvx  v29, r3, r2
  addi  r2, r2, 16
  stvx  v30, r3, r2
  addi  r2, r2, 16
  stvx  v31, r3, r2

  mfcr r2 # CR
  stw  r2,  0x400(r3)

  mffs f0 # FPSCR
  stfd f0,  0x404(r3)
  blr

# void xe_call_native(Context* ctx, void* func)
.globl xe_call_native
xe_call_native:
  mflr r12
  stw  r12, -0x8(r1)
  stwu r1, -0x380(r1) # 0x200(gpr + fp) + 0x200(vr)

  # Save nonvolatile registers on the stack.
  std r2, 0x110(r1)
  std r3, 0x118(r1)  # Store the context, this will be needed later.
  std r14, 0x170(r1)
  std r15, 0x178(r1)
  std r16, 0x180(r1)
  std r17, 0x188(r1)
  std r18, 0x190(r1)
  std r19, 0x198(r1)
  std r20, 0x1A0(r1)
  std r21, 0x1A8(r1)
  std r22, 0x1B0(r1)
  std r23, 0x1B8(r1)
  std r24, 0x1C0(r1)
  std r25, 0x1C8(r1)
  std r26, 0x1D0(r1)
  std r27, 0x1D8(r1)
  std r28, 0x1E0(r1)
  std r29, 0x1E8(r1)
  std r30, 0x1F0(r1)
  std r31, 0x1F8(r1)

  stfd f14, 0x270(r1)
  stfd f15, 0x278(r1)
  stfd f16, 0x280(r1)
  stfd f17, 0x288(r1)
  stfd f18, 0x290(r1)
  stfd f19, 0x298(r1)
  stfd f20, 0x2A0(r1)
  stfd f21, 0x2A8(r1)
  stfd f22, 0x2B0(r1)
  stfd f23, 0x2B8(r1)
  stfd f24, 0x2C0(r1)
  stfd f25, 0x2C8(r1)
  stfd f26, 0x2D0(r1)
  stfd f27, 0x2D8(r1)
  stfd f28, 0x2E0(r1)
  stfd f29, 0x2E8(r1)
  stfd f30, 0x2F0(r1)
  stfd f31, 0x2F8(r1)

  # Load registers from context (except r3/r4)
  bl .load_registers_ctx

  # Call the test routine
  mtctr r4
  ld r4, 0x20(r3)
  ld r3, 0x18(r3)
  bctrl

  # Temporarily store r3 into the stack (in the place of r0)
  std r3, 0x100(r1)

  # Store registers into context (except r3)
  ld r3, 0x118(r1)
  bl .save_registers_ctx

  # Now store r3
  ld r4, 0x100(r1)
  std r4, 0x18(r3)

  # Restore nonvolatile registers from the stack
  ld r2, 0x110(r1)
  ld r14, 0x170(r1)
  ld r15, 0x178(r1)
  ld r16, 0x180(r1)
  ld r17, 0x188(r1)
  ld r18, 0x190(r1)
  ld r19, 0x198(r1)
  ld r20, 0x1A0(r1)
  ld r21, 0x1A8(r1)
  ld r22, 0x1B0(r1)
  ld r23, 0x1B8(r1)
  ld r24, 0x1C0(r1)
  ld r25, 0x1C8(r1)
  ld r26, 0x1D0(r1)
  ld r27, 0x1D8(r1)
  ld r28, 0x1E0(r1)
  ld r29, 0x1E8(r1)
  ld r30, 0x1F0(r1)
  ld r31, 0x1F8(r1)

  lfd f14, 0x270(r1)
  lfd f15, 0x278(r1)
  lfd f16, 0x280(r1)
  lfd f17, 0x288(r1)
  lfd f18, 0x290(r1)
  lfd f19, 0x298(r1)
  lfd f20, 0x2A0(r1)
  lfd f21, 0x2A8(r1)
  lfd f22, 0x2B0(r1)
  lfd f23, 0x2B8(r1)
  lfd f24, 0x2C0(r1)
  lfd f25, 0x2C8(r1)
  lfd f26, 0x2D0(r1)
  lfd f27, 0x2D8(r1)
  lfd f28, 0x2E0(r1)
  lfd f29, 0x2E8(r1)
  lfd f30, 0x2F0(r1)
  lfd f31, 0x2F8(r1)

  addi r1, r1, 0x380
  lwz  r12, -0x8(r1)
  mtlr r12
  blr
