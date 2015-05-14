test_cmpld_1:
  #_ REGISTER_IN r3 0x0000000100000000
  #_ REGISTER_IN r4 0x0000000200000000
  cmpld r3, r4
  mfcr r12
  blr
  #_ REGISTER_OUT r3 0x0000000100000000
  #_ REGISTER_OUT r4 0x0000000200000000
  #_ REGISTER_OUT r12 0x80000000

test_cmpld_1_constant:
  li r3, 1
  sldi r3, r3, 32
  sldi r4, r3, 1
  cmpld r3, r4
  mfcr r12
  blr
  #_ REGISTER_OUT r3 0x0000000100000000
  #_ REGISTER_OUT r4 0x0000000200000000
  #_ REGISTER_OUT r12 0x80000000

test_cmpld_2:
  #_ REGISTER_IN r3 0x0000000200000000
  #_ REGISTER_IN r4 0x0000000100000000
  cmpld r3, r4
  mfcr r12
  blr
  #_ REGISTER_OUT r3 0x0000000200000000
  #_ REGISTER_OUT r4 0x0000000100000000
  #_ REGISTER_OUT r12 0x40000000

test_cmpld_2_constant:
  li r4, 1
  sldi r4, r4, 32
  sldi r3, r4, 1
  cmpld r3, r4
  mfcr r12
  blr
  #_ REGISTER_OUT r3 0x0000000200000000
  #_ REGISTER_OUT r4 0x0000000100000000
  #_ REGISTER_OUT r12 0x40000000

test_cmplw_1:
  #_ REGISTER_IN r3 0x0000000100000000
  #_ REGISTER_IN r4 0x0000000200000000
  cmplw r3, r4
  mfcr r12
  blr
  #_ REGISTER_OUT r3 0x0000000100000000
  #_ REGISTER_OUT r4 0x0000000200000000
  #_ REGISTER_OUT r12 0x20000000

test_cmplw_1_constant:
  li r3, 1
  sldi r3, r3, 32
  sldi r4, r3, 1
  cmplw r3, r4
  mfcr r12
  blr
  #_ REGISTER_OUT r3 0x0000000100000000
  #_ REGISTER_OUT r4 0x0000000200000000
  #_ REGISTER_OUT r12 0x20000000

test_cmplw_2:
  #_ REGISTER_IN r3 0x0000000200000000
  #_ REGISTER_IN r4 0x0000000100000000
  cmplw r3, r4
  mfcr r12
  blr
  #_ REGISTER_OUT r3 0x0000000200000000
  #_ REGISTER_OUT r4 0x0000000100000000
  #_ REGISTER_OUT r12 0x20000000

test_cmplw_2_constant:
  li r4, 1
  sldi r4, r4, 32
  sldi r3, r4, 1
  cmplw r3, r4
  mfcr r12
  blr
  #_ REGISTER_OUT r3 0x0000000200000000
  #_ REGISTER_OUT r4 0x0000000100000000
  #_ REGISTER_OUT r12 0x20000000

test_cmplw_3:
  #_ REGISTER_IN r3 1
  #_ REGISTER_IN r4 2
  cmplw r3, r4
  mfcr r12
  blr
  #_ REGISTER_OUT r3 1
  #_ REGISTER_OUT r4 2
  #_ REGISTER_OUT r12 0x80000000

test_cmplw_3_constant:
  li r3, 1
  li r4, 2
  cmplw r3, r4
  mfcr r12
  blr
  #_ REGISTER_OUT r3 1
  #_ REGISTER_OUT r4 2
  #_ REGISTER_OUT r12 0x80000000

test_cmplw_4:
  #_ REGISTER_IN r3 2
  #_ REGISTER_IN r4 1
  cmplw r3, r4
  mfcr r12
  blr
  #_ REGISTER_OUT r3 2
  #_ REGISTER_OUT r4 1
  #_ REGISTER_OUT r12 0x40000000

test_cmplw_4_constant:
  li r3, 2
  li r4, 1
  cmplw r3, r4
  mfcr r12
  blr
  #_ REGISTER_OUT r3 2
  #_ REGISTER_OUT r4 1
  #_ REGISTER_OUT r12 0x40000000

test_cmplw_5:
  #_ REGISTER_IN r3 0x0000000100000002
  #_ REGISTER_IN r4 0x0000000200000001
  cmplw r3, r4
  mfcr r12
  blr
  #_ REGISTER_OUT r3 0x0000000100000002
  #_ REGISTER_OUT r4 0x0000000200000001
  #_ REGISTER_OUT r12 0x40000000

test_cmplw_5_constant:
  li r3, 1
  sldi r3, r3, 32
  sldi r4, r3, 1
  addi r3, r3, 2
  addi r4, r4, 1
  cmplw r3, r4
  mfcr r12
  blr
  #_ REGISTER_OUT r3 0x0000000100000002
  #_ REGISTER_OUT r4 0x0000000200000001
  #_ REGISTER_OUT r12 0x40000000

test_cmplw_6:
  #_ REGISTER_IN r3 0xFFFFFFFF80000000
  #_ REGISTER_IN r4 0x0000000080000000
  cmplw r3, r4
  mfcr r12
  blr
  #_ REGISTER_OUT r3 0xFFFFFFFF80000000
  #_ REGISTER_OUT r4 0x0000000080000000
  #_ REGISTER_OUT r12 0x20000000

test_cmplw_6_constant:
  lis r3, 0x7FFF
  ori r3, r3, 0xFFFF
  not r3, r3
  li r4, 1
  sldi r4, r4, 31
  cmplw r3, r4
  mfcr r12
  blr
  #_ REGISTER_OUT r3 0xFFFFFFFF80000000
  #_ REGISTER_OUT r4 0x0000000080000000
  #_ REGISTER_OUT r12 0x20000000

test_cmpl_1:
  #_ REGISTER_IN r3 1
  #_ REGISTER_IN r4 2
  cmpl 5, 0, r3, r4
  mfcr r12
  blr
  #_ REGISTER_OUT r3 1
  #_ REGISTER_OUT r4 2
  #_ REGISTER_OUT r12 0x00000800

test_cmpl_1_constant:
  li r3, 1
  li r4, 2
  cmpl 5, 0, r3, r4
  mfcr r12
  blr
  #_ REGISTER_OUT r3 1
  #_ REGISTER_OUT r4 2
  #_ REGISTER_OUT r12 0x00000800

test_cmpl_2:
  #_ REGISTER_IN r3 2
  #_ REGISTER_IN r4 1
  cmpl 3, 0, r3, r4
  mfcr r12
  blr
  #_ REGISTER_OUT r3 2
  #_ REGISTER_OUT r4 1
  #_ REGISTER_OUT r12 0x00040000

test_cmpl_2_constant:
  li r3, 2
  li r4, 1
  cmpl 3, 0, r3, r4
  mfcr r12
  blr
  #_ REGISTER_OUT r3 2
  #_ REGISTER_OUT r4 1
  #_ REGISTER_OUT r12 0x00040000
