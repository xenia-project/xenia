test_eqv_1:
  #_ REGISTER_IN r4 0
  #_ REGISTER_IN r5 1
  eqv r3, r4, r5
  blr
  #_ REGISTER_OUT r3 0xfffffffffffffffe
  #_ REGISTER_OUT r4 0
  #_ REGISTER_OUT r5 1

test_eqv_1_constant:
  li r4, 0
  li r5, 1
  eqv r3, r4, r5
  blr
  #_ REGISTER_OUT r3 0xfffffffffffffffe
  #_ REGISTER_OUT r4 0
  #_ REGISTER_OUT r5 1

test_eqv_2:
  #_ REGISTER_IN r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_IN r5 0
  eqv r3, r4, r5
  blr
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r5 0

test_eqv_2_constant:
  li r4, -1
  li r5, 0
  eqv r3, r4, r5
  blr
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r5 0

test_eqv_3:
  #_ REGISTER_IN r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_IN r5 0xFFFFFFFFFFFFFFFF
  eqv r3, r4, r5
  blr
  #_ REGISTER_OUT r3 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r5 0xFFFFFFFFFFFFFFFF

test_eqv_3_constant:
  li r4, -1
  li r5, -1
  eqv r3, r4, r5
  blr
  #_ REGISTER_OUT r3 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r5 0xFFFFFFFFFFFFFFFF

test_eqv_4:
  #_ REGISTER_IN r4 0xDEADBEEFDEADBEEF
  #_ REGISTER_IN r5 0xDEADBEEFDEADBEEF
  eqv r3, r4, r5
  blr
  #_ REGISTER_OUT r3 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r4 0xDEADBEEFDEADBEEF
  #_ REGISTER_OUT r5 0xDEADBEEFDEADBEEF

test_eqv_4_constant:
  lis r4, 0xDEAD
  ori r4, r4, 0xBEEF
  sldi r5, r4, 32
  clrldi r4, r4, 32
  or r4, r5, r4
  mr r5, r4
  eqv r3, r4, r5
  blr
  #_ REGISTER_OUT r3 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r4 0xDEADBEEFDEADBEEF
  #_ REGISTER_OUT r5 0xDEADBEEFDEADBEEF

test_eqv_5:
  #_ REGISTER_IN r4 0xDEADBEEFDEADBEEF
  #_ REGISTER_IN r5 0xFFFFFFFFFFFFFFFF
  eqv r3, r4, r5
  blr
  #_ REGISTER_OUT r3 0xDEADBEEFDEADBEEF
  #_ REGISTER_OUT r4 0xDEADBEEFDEADBEEF
  #_ REGISTER_OUT r5 0xFFFFFFFFFFFFFFFF

test_eqv_5_constant:
  lis r4, 0xDEAD
  ori r4, r4, 0xBEEF
  sldi r5, r4, 32
  clrldi r4, r4, 32
  or r4, r5, r4
  li r5, -1
  eqv r3, r4, r5
  blr
  #_ REGISTER_OUT r3 0xDEADBEEFDEADBEEF
  #_ REGISTER_OUT r4 0xDEADBEEFDEADBEEF
  #_ REGISTER_OUT r5 0xFFFFFFFFFFFFFFFF

test_eqv_6:
  #_ REGISTER_IN r4 0xDEADBEEFDEADBEEF
  #_ REGISTER_IN r5 0
  eqv r3, r4, r5
  blr
  #_ REGISTER_OUT r3 0x2152411021524110
  #_ REGISTER_OUT r4 0xDEADBEEFDEADBEEF
  #_ REGISTER_OUT r5 0

test_eqv_6_constant:
  lis r4, 0xDEAD
  ori r4, r4, 0xBEEF
  sldi r5, r4, 32
  clrldi r4, r4, 32
  or r4, r5, r4
  li r5, 0
  eqv r3, r4, r5
  blr
  #_ REGISTER_OUT r3 0x2152411021524110
  #_ REGISTER_OUT r4 0xDEADBEEFDEADBEEF
  #_ REGISTER_OUT r5 0
