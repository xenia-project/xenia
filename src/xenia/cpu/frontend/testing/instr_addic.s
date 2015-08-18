test_addic_1:
  #_ REGISTER_IN r4 1
  addic r4, r4, 1
  adde r6, r0, r0
  blr
  #_ REGISTER_OUT r4 2
  #_ REGISTER_OUT r6 0

test_addic_1_constant:
  li r4, 1
  addic r4, r4, 1
  adde r6, r0, r0
  blr
  #_ REGISTER_OUT r4 2
  #_ REGISTER_OUT r6 0

test_addic_2:
  #_ REGISTER_IN r4 0xFFFFFFFFFFFFFFFF
  addic r4, r4, 1
  adde r6, r0, r0
  blr
  #_ REGISTER_OUT r4 0
  #_ REGISTER_OUT r6 1

test_addic_2_constant:
  li r4, -1
  addic r4, r4, 1
  adde r6, r0, r0
  blr
  #_ REGISTER_OUT r4 0
  #_ REGISTER_OUT r6 1

test_addic_3:
  #_ REGISTER_IN r4 0xFFFFFFFF
  addic r4, r4, 1
  adde r6, r0, r0
  blr
  #_ REGISTER_OUT r4 0x0000000100000000
  #_ REGISTER_OUT r6 1

test_addic_3_constant:
  li r4, 0xFFFFFFFF
  srwi r4, r4, 0
  addic r4, r4, 1
  adde r6, r0, r0
  blr
  #_ REGISTER_OUT r4 0x0000000100000000
  #_ REGISTER_OUT r6 1

test_addic_cr_1:
  #_ REGISTER_IN r4 1
  addic. r4, r4, 1
  adde r6, r0, r0
  mfcr r12
  blr
  #_ REGISTER_OUT r4 2
  #_ REGISTER_OUT r6 0
  #_ REGISTER_OUT r12 0x40000000

test_addic_cr_1_constant:
  li r4, 1
  addic. r4, r4, 1
  adde r6, r0, r0
  mfcr r12
  blr
  #_ REGISTER_OUT r4 2
  #_ REGISTER_OUT r6 0
  #_ REGISTER_OUT r12 0x40000000

test_addic_cr_2:
  #_ REGISTER_IN r4 0xFFFFFFFFFFFFFFFF
  addic. r4, r4, 1
  adde r6, r0, r0
  mfcr r12
  blr
  #_ REGISTER_OUT r4 0
  #_ REGISTER_OUT r6 1
  #_ REGISTER_OUT r12 0x20000000

test_addic_cr_2_constant:
  li r4, -1
  addic. r4, r4, 1
  adde r6, r0, r0
  mfcr r12
  blr
  #_ REGISTER_OUT r4 0
  #_ REGISTER_OUT r6 1
  #_ REGISTER_OUT r12 0x20000000
