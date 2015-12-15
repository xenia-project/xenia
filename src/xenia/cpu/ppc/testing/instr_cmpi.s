test_cmpdi_1:
  #_ REGISTER_IN r3 0x0000000100000000
  cmpdi r3, 2
  mfcr r12
  blr
  #_ REGISTER_OUT r3 0x0000000100000000
  #_ REGISTER_OUT r12 0x40000000

test_cmpdi_1_constant:
  li r3, 1
  sldi r3, r3, 32
  cmpdi r3, 2
  mfcr r12
  blr
  #_ REGISTER_OUT r3 0x0000000100000000
  #_ REGISTER_OUT r12 0x40000000

test_cmpdi_2:
  #_ REGISTER_IN r3 1
  cmpdi r3, 2
  mfcr r12
  blr
  #_ REGISTER_OUT r3 1
  #_ REGISTER_OUT r12 0x80000000

test_cmpdi_2_constant:
  li r3, 1
  cmpdi r3, 2
  mfcr r12
  blr
  #_ REGISTER_OUT r3 1
  #_ REGISTER_OUT r12 0x80000000

test_cmpwi_1:
  #_ REGISTER_IN r3 0x0000000100000000
  cmpwi r3, 2
  mfcr r12
  blr
  #_ REGISTER_OUT r3 0x0000000100000000
  #_ REGISTER_OUT r12 0x80000000

test_cmpwi_1_constant:
  li r3, 1
  sldi r3, r3, 32
  cmpwi r3, 2
  mfcr r12
  blr
  #_ REGISTER_OUT r3 0x0000000100000000
  #_ REGISTER_OUT r12 0x80000000

test_cmpwi_2:
  #_ REGISTER_IN r3 2
  cmpwi r3, 1
  mfcr r12
  blr
  #_ REGISTER_OUT r3 2
  #_ REGISTER_OUT r12 0x40000000

test_cmpwi_2_constant:
  li r3, 2
  cmpwi r3, 1
  mfcr r12
  blr
  #_ REGISTER_OUT r3 2
  #_ REGISTER_OUT r12 0x40000000

test_cmpwi_5:
  #_ REGISTER_IN r3 0x0000000100000002
  cmpwi r3, 1
  mfcr r12
  blr
  #_ REGISTER_OUT r3 0x0000000100000002
  #_ REGISTER_OUT r12 0x40000000

test_cmpwi_5_constant:
  li r3, 1
  sldi r3, r3, 32
  sldi r4, r3, 1
  addi r3, r3, 2
  cmpwi r3, 1
  mfcr r12
  blr
  #_ REGISTER_OUT r3 0x0000000100000002
  #_ REGISTER_OUT r12 0x40000000

test_cmpi_1:
  #_ REGISTER_IN r3 1
  cmpi 5, 0, r3, 2
  mfcr r12
  blr
  #_ REGISTER_OUT r3 1
  #_ REGISTER_OUT r12 0x00000800

test_cmpi_1_constant:
  li r3, 1
  cmpi 5, 0, r3, 2
  mfcr r12
  blr
  #_ REGISTER_OUT r3 1
  #_ REGISTER_OUT r12 0x00000800

test_cmpi_2:
  #_ REGISTER_IN r3 2
  cmpi 3, 0, r3, 1
  mfcr r12
  blr
  #_ REGISTER_OUT r3 2
  #_ REGISTER_OUT r12 0x00040000

test_cmpi_2_constant:
  li r3, 2
  cmpi 3, 0, r3, 1
  mfcr r12
  blr
  #_ REGISTER_OUT r3 2
  #_ REGISTER_OUT r12 0x00040000
