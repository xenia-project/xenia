test_addc_1:
  #_ REGISTER_IN r4 1
  #_ REGISTER_IN r5 2
  addc r3, r4, r5
  adde r6, r0, r0
  blr
  #_ REGISTER_OUT r3 3
  #_ REGISTER_OUT r4 1
  #_ REGISTER_OUT r5 2
  #_ REGISTER_OUT r6 0

test_addc_1_constant:
  li r4, 1
  li r5, 2
  addc r3, r4, r5
  adde r6, r0, r0
  blr
  #_ REGISTER_OUT r3 3
  #_ REGISTER_OUT r4 1
  #_ REGISTER_OUT r5 2
  #_ REGISTER_OUT r6 0

test_addc_2:
  #_ REGISTER_IN r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_IN r5 0
  addc r3, r4, r5
  adde r6, r0, r0
  blr
  #_ REGISTER_OUT r3 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r5 0
  #_ REGISTER_OUT r6 0

test_addc_2_constant:
  li r4, -1
  li r5, 0
  addc r3, r4, r5
  adde r6, r0, r0
  blr
  #_ REGISTER_OUT r3 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r5 0
  #_ REGISTER_OUT r6 0

test_addc_3:
  #_ REGISTER_IN r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_IN r5 1
  addc r3, r4, r5
  adde r6, r0, r0
  blr
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r5 1
  #_ REGISTER_OUT r6 1

test_addc_3_constant:
  li r4, -1
  li r5, 1
  addc r3, r4, r5
  adde r6, r0, r0
  blr
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r5 1
  #_ REGISTER_OUT r6 1

test_addc_4:
  #_ REGISTER_IN r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_IN r5 123
  addc r3, r4, r5
  adde r6, r0, r0
  blr
  #_ REGISTER_OUT r3 0x000000000000007A
  #_ REGISTER_OUT r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r5 123
  #_ REGISTER_OUT r6 1

test_addc_4_constant:
  li r4, -1
  li r5, 123
  addc r3, r4, r5
  adde r6, r0, r0
  blr
  #_ REGISTER_OUT r3 0x000000000000007A
  #_ REGISTER_OUT r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r5 123
  #_ REGISTER_OUT r6 1

test_addc_5:
  #_ REGISTER_IN r4 0x7FFFFFFFFFFFFFFF
  #_ REGISTER_IN r5 0xFFFFFFFFFFFFFFFF
  addc r3, r4, r5
  adde r6, r0, r0
  blr
  #_ REGISTER_OUT r3 0x7FFFFFFFFFFFFFFE
  #_ REGISTER_OUT r4 0x7FFFFFFFFFFFFFFF
  #_ REGISTER_OUT r5 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r6 1

test_addc_5_constant:
  li r5, -1
  srdi r4, r5, 1
  addc r3, r4, r5
  adde r6, r0, r0
  blr
  #_ REGISTER_OUT r3 0x7FFFFFFFFFFFFFFE
  #_ REGISTER_OUT r4 0x7FFFFFFFFFFFFFFF
  #_ REGISTER_OUT r5 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r6 1

test_addc_cr_1:
  #_ REGISTER_IN r4 1
  #_ REGISTER_IN r5 2
  addc. r3, r4, r5
  adde r6, r0, r0
  mfcr r12
  blr
  #_ REGISTER_OUT r3 3
  #_ REGISTER_OUT r4 1
  #_ REGISTER_OUT r5 2
  #_ REGISTER_OUT r6 0
  #_ REGISTER_OUT r12 0x40000000

test_addc_cr_1_constant:
  li r4, 1
  li r5, 2
  addc. r3, r4, r5
  adde r6, r0, r0
  mfcr r12
  blr
  #_ REGISTER_OUT r3 3
  #_ REGISTER_OUT r4 1
  #_ REGISTER_OUT r5 2
  #_ REGISTER_OUT r6 0
  #_ REGISTER_OUT r12 0x40000000

test_addc_cr_2:
  #_ REGISTER_IN r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_IN r5 0
  addc. r3, r4, r5
  adde r6, r0, r0
  mfcr r12
  blr
  #_ REGISTER_OUT r3 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r5 0
  #_ REGISTER_OUT r6 0
  #_ REGISTER_OUT r12 0x80000000

test_addc_cr_2_constant:
  li r4, -1
  li r5, 0
  addc. r3, r4, r5
  adde r6, r0, r0
  mfcr r12
  blr
  #_ REGISTER_OUT r3 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r5 0
  #_ REGISTER_OUT r6 0
  #_ REGISTER_OUT r12 0x80000000

test_addc_cr_3:
  #_ REGISTER_IN r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_IN r5 1
  addc. r3, r4, r5
  adde r6, r0, r0
  mfcr r12
  blr
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r5 1
  #_ REGISTER_OUT r6 1
  #_ REGISTER_OUT r12 0x20000000

test_addc_cr_3_constant:
  li r4, -1
  li r5, 1
  addc. r3, r4, r5
  adde r6, r0, r0
  mfcr r12
  blr
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r5 1
  #_ REGISTER_OUT r6 1
  #_ REGISTER_OUT r12 0x20000000

test_addc_cr_4:
  #_ REGISTER_IN r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_IN r5 123
  addc. r3, r4, r5
  adde r6, r0, r0
  mfcr r12
  blr
  #_ REGISTER_OUT r3 0x000000000000007A
  #_ REGISTER_OUT r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r5 123
  #_ REGISTER_OUT r6 1
  #_ REGISTER_OUT r12 0x40000000

test_addc_cr_4_constant:
  li r4, -1
  li r5, 123
  addc. r3, r4, r5
  adde r6, r0, r0
  mfcr r12
  blr
  #_ REGISTER_OUT r3 0x000000000000007A
  #_ REGISTER_OUT r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r5 123
  #_ REGISTER_OUT r6 1
  #_ REGISTER_OUT r12 0x40000000

test_addc_cr_5:
  #_ REGISTER_IN r4 0x7FFFFFFFFFFFFFFF
  #_ REGISTER_IN r5 0xFFFFFFFFFFFFFFFF
  addc. r3, r4, r5
  adde r6, r0, r0
  mfcr r12
  blr
  #_ REGISTER_OUT r3 0x7FFFFFFFFFFFFFFE
  #_ REGISTER_OUT r4 0x7FFFFFFFFFFFFFFF
  #_ REGISTER_OUT r5 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r6 1
  #_ REGISTER_OUT r12 0x80000000

test_addc_cr_5_constant:
  li r5, -1
  srdi r4, r5, 1
  addc. r3, r4, r5
  adde r6, r0, r0
  mfcr r12
  blr
  #_ REGISTER_OUT r3 0x7FFFFFFFFFFFFFFE
  #_ REGISTER_OUT r4 0x7FFFFFFFFFFFFFFF
  #_ REGISTER_OUT r5 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r6 1
  #_ REGISTER_OUT r12 0x80000000

test_addc_cr_6:
  #_ REGISTER_IN r4 0x7FFFFFFFFFFFFFFF
  #_ REGISTER_IN r5 2
  addc. r3, r4, r5
  adde r6, r0, r0
  mfcr r12
  blr
  #_ REGISTER_OUT r3 0x8000000000000001
  #_ REGISTER_OUT r4 0x7FFFFFFFFFFFFFFF
  #_ REGISTER_OUT r5 2
  #_ REGISTER_OUT r6 1
  #_ REGISTER_OUT r12 0x40000000

test_addc_cr_6_constant:
  li r4, -1
  srdi r4, r4, 1
  li r5, 2
  addc. r3, r4, r5
  adde r6, r0, r0
  mfcr r12
  blr
  #_ REGISTER_OUT r3 0x8000000000000001
  #_ REGISTER_OUT r4 0x7FFFFFFFFFFFFFFF
  #_ REGISTER_OUT r5 2
  #_ REGISTER_OUT r6 1
  #_ REGISTER_OUT r12 0x40000000
