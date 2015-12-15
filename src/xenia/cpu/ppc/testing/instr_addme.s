test_addme_1:
  #_ REGISTER_IN r4 1
  addme r3, r4
  adde r6, r0, r0
  blr
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 1
  #_ REGISTER_OUT r6 1

test_addme_1_constant:
  li r4, 1
  addme r3, r4
  adde r6, r0, r0
  blr
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 1
  #_ REGISTER_OUT r6 1

test_addme_2:
  #_ REGISTER_IN r4 1
  xor r3, r3, r3
  not r3, r3
  addic r3, r3, 1 # CA=1
  addme r3, r4
  adde r6, r0, r0
  blr
  #_ REGISTER_OUT r3 1
  #_ REGISTER_OUT r4 1
  #_ REGISTER_OUT r6 1

test_addme_2_constant:
  li r4, 1
  xor r3, r3, r3
  not r3, r3
  addic r3, r3, 1 # CA=1
  addme r3, r4
  adde r6, r0, r0
  blr
  #_ REGISTER_OUT r3 1
  #_ REGISTER_OUT r4 1
  #_ REGISTER_OUT r6 1

test_addme_3:
  #_ REGISTER_IN r4 12
  addme r3, r4
  adde r6, r0, r0
  blr
  #_ REGISTER_OUT r3 11
  #_ REGISTER_OUT r4 12
  #_ REGISTER_OUT r6 1

test_addme_3_constant:
  li r4, 12
  addme r3, r4
  adde r6, r0, r0
  blr
  #_ REGISTER_OUT r3 11
  #_ REGISTER_OUT r4 12
  #_ REGISTER_OUT r6 1

test_addme_4:
  #_ REGISTER_IN r4 12
  xor r3, r3, r3
  not r3, r3
  addic r3, r3, 1 # CA=1
  addme r3, r4
  adde r6, r0, r0
  blr
  #_ REGISTER_OUT r3 12
  #_ REGISTER_OUT r4 12
  #_ REGISTER_OUT r6 1

test_addme_4_constant:
  li r4, 12
  xor r3, r3, r3
  not r3, r3
  addic r3, r3, 1 # CA=1
  addme r3, r4
  adde r6, r0, r0
  blr
  #_ REGISTER_OUT r3 12
  #_ REGISTER_OUT r4 12
  #_ REGISTER_OUT r6 1

test_addme_5:
  #_ REGISTER_IN r4 0xFFFFFFFFFFFFFFFF
  addme r3, r4
  adde r6, r0, r0
  blr
  #_ REGISTER_OUT r3 0xFFFFFFFFFFFFFFFE
  #_ REGISTER_OUT r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r6 1

test_addme_5_constant:
  li r4, -1
  addme r3, r4
  adde r6, r0, r0
  blr
  #_ REGISTER_OUT r3 0xFFFFFFFFFFFFFFFE
  #_ REGISTER_OUT r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r6 1

test_addme_6:
  #_ REGISTER_IN r4 0xFFFFFFFFFFFFFFFF
  xor r3, r3, r3
  not r3, r3
  addic r3, r3, 1 # CA=1
  addme r3, r4
  adde r6, r0, r0
  blr
  #_ REGISTER_OUT r3 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r6 1

test_addme_6_constant:
  li r4, -1
  xor r3, r3, r3
  not r3, r3
  addic r3, r3, 1 # CA=1
  addme r3, r4
  adde r6, r0, r0
  blr
  #_ REGISTER_OUT r3 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r6 1

test_addme_7:
  #_ REGISTER_IN r4 0
  addme r3, r4
  adde r6, r0, r0
  blr
  #_ REGISTER_OUT r3 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r4 0
  #_ REGISTER_OUT r6 0

test_addme_7_constant:
  li r4, 0
  addme r3, r4
  adde r6, r0, r0
  blr
  #_ REGISTER_OUT r3 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r4 0
  #_ REGISTER_OUT r6 0

test_addme_8:
  #_ REGISTER_IN r4 0
  xor r3, r3, r3
  not r3, r3
  addic r3, r3, 1 # CA=1
  addme r3, r4
  adde r6, r0, r0
  blr
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 0
  #_ REGISTER_OUT r6 1

test_addme_8_constant:
  li r4, 0
  xor r3, r3, r3
  not r3, r3
  addic r3, r3, 1 # CA=1
  addme r3, r4
  adde r6, r0, r0
  blr
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 0
  #_ REGISTER_OUT r6 1

test_addme_cr_1:
  #_ REGISTER_IN r4 1
  addme. r3, r4
  adde r6, r0, r0
  mfcr r12
  blr
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 1
  #_ REGISTER_OUT r6 1
  #_ REGISTER_OUT r12 0x20000000

test_addme_cr_1_constant:
  li r4, 1
  addme. r3, r4
  adde r6, r0, r0
  mfcr r12
  blr
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 1
  #_ REGISTER_OUT r6 1
  #_ REGISTER_OUT r12 0x20000000

test_addme_cr_2:
  #_ REGISTER_IN r4 1
  xor r3, r3, r3
  not r3, r3
  addic r3, r3, 1 # CA=1
  addme. r3, r4
  adde r6, r0, r0
  mfcr r12
  blr
  #_ REGISTER_OUT r3 1
  #_ REGISTER_OUT r4 1
  #_ REGISTER_OUT r6 1
  #_ REGISTER_OUT r12 0x40000000

test_addme_cr_2_constant:
  li r4, 1
  xor r3, r3, r3
  not r3, r3
  addic r3, r3, 1 # CA=1
  addme. r3, r4
  adde r6, r0, r0
  mfcr r12
  blr
  #_ REGISTER_OUT r3 1
  #_ REGISTER_OUT r4 1
  #_ REGISTER_OUT r6 1
  #_ REGISTER_OUT r12 0x40000000

test_addme_cr_3:
  #_ REGISTER_IN r4 12
  addme. r3, r4
  adde r6, r0, r0
  mfcr r12
  blr
  #_ REGISTER_OUT r3 11
  #_ REGISTER_OUT r4 12
  #_ REGISTER_OUT r6 1
  #_ REGISTER_OUT r12 0x40000000

test_addme_cr_3_constant:
  li r4, 12
  addme. r3, r4
  adde r6, r0, r0
  mfcr r12
  blr
  #_ REGISTER_OUT r3 11
  #_ REGISTER_OUT r4 12
  #_ REGISTER_OUT r6 1
  #_ REGISTER_OUT r12 0x40000000

test_addme_cr_4:
  #_ REGISTER_IN r4 12
  xor r3, r3, r3
  not r3, r3
  addic r3, r3, 1 # CA=1
  addme. r3, r4
  adde r6, r0, r0
  mfcr r12
  blr
  #_ REGISTER_OUT r3 12
  #_ REGISTER_OUT r4 12
  #_ REGISTER_OUT r6 1
  #_ REGISTER_OUT r12 0x40000000

test_addme_cr_4_constant:
  li r4, 12
  xor r3, r3, r3
  not r3, r3
  addic r3, r3, 1 # CA=1
  addme. r3, r4
  adde r6, r0, r0
  mfcr r12
  blr
  #_ REGISTER_OUT r3 12
  #_ REGISTER_OUT r4 12
  #_ REGISTER_OUT r6 1
  #_ REGISTER_OUT r12 0x40000000

test_addme_cr_5:
  #_ REGISTER_IN r4 0xFFFFFFFFFFFFFFFF
  addme. r3, r4
  adde r6, r0, r0
  mfcr r12
  blr
  #_ REGISTER_OUT r3 0xFFFFFFFFFFFFFFFE
  #_ REGISTER_OUT r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r6 1
  #_ REGISTER_OUT r12 0x80000000

test_addme_cr_5_constant:
  li r4, -1
  addme. r3, r4
  adde r6, r0, r0
  mfcr r12
  blr
  #_ REGISTER_OUT r3 0xFFFFFFFFFFFFFFFE
  #_ REGISTER_OUT r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r6 1
  #_ REGISTER_OUT r12 0x80000000

test_addme_cr_6:
  #_ REGISTER_IN r4 0xFFFFFFFFFFFFFFFF
  xor r3, r3, r3
  not r3, r3
  addic r3, r3, 1 # CA=1
  addme. r3, r4
  adde r6, r0, r0
  mfcr r12
  blr
  #_ REGISTER_OUT r3 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r6 1
  #_ REGISTER_OUT r12 0x80000000

test_addme_cr_6_constant:
  li r4, -1
  xor r3, r3, r3
  not r3, r3
  addic r3, r3, 1 # CA=1
  addme. r3, r4
  adde r6, r0, r0
  mfcr r12
  blr
  #_ REGISTER_OUT r3 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r6 1
  #_ REGISTER_OUT r12 0x80000000

test_addme_cr_7:
  #_ REGISTER_IN r4 0
  addme. r3, r4
  adde r6, r0, r0
  mfcr r12
  blr
  #_ REGISTER_OUT r3 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r4 0
  #_ REGISTER_OUT r6 0
  #_ REGISTER_OUT r12 0x80000000

test_addme_cr_7_constant:
  li r4, 0
  addme. r3, r4
  adde r6, r0, r0
  mfcr r12
  blr
  #_ REGISTER_OUT r3 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r4 0
  #_ REGISTER_OUT r6 0
  #_ REGISTER_OUT r12 0x80000000

test_addme_cr_8:
  #_ REGISTER_IN r4 0
  xor r3, r3, r3
  not r3, r3
  addic r3, r3, 1 # CA=1
  addme. r3, r4
  adde r6, r0, r0
  mfcr r12
  blr
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 0
  #_ REGISTER_OUT r6 1
  #_ REGISTER_OUT r12 0x20000000

test_addme_cr_8_constant:
  li r4, 0
  xor r3, r3, r3
  not r3, r3
  addic r3, r3, 1 # CA=1
  addme. r3, r4
  adde r6, r0, r0
  mfcr r12
  blr
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 0
  #_ REGISTER_OUT r6 1
  #_ REGISTER_OUT r12 0x20000000
