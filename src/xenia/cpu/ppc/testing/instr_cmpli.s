test_cmpldi_1:
  #_ REGISTER_IN r3 0x0000000100000000
  cmpldi r3, 2
  mfcr r12
  blr
  #_ REGISTER_OUT r3 0x0000000100000000
  #_ REGISTER_OUT r12 0x40000000

test_cmpldi_1_constant:
  li r3, 1
  sldi r3, r3, 32
  cmpldi r3, 2
  mfcr r12
  blr
  #_ REGISTER_OUT r3 0x0000000100000000
  #_ REGISTER_OUT r12 0x40000000

test_cmpldi_2:
  #_ REGISTER_IN r3 1
  cmpldi r3, 2
  mfcr r12
  blr
  #_ REGISTER_OUT r3 1
  #_ REGISTER_OUT r12 0x80000000

test_cmpldi_2_constant:
  li r3, 1
  cmpldi r3, 2
  mfcr r12
  blr
  #_ REGISTER_OUT r3 1
  #_ REGISTER_OUT r12 0x80000000

test_cmplwi_1:
  #_ REGISTER_IN r3 0x0000000100000000
  cmplwi r3, 2
  mfcr r12
  blr
  #_ REGISTER_OUT r3 0x0000000100000000
  #_ REGISTER_OUT r12 0x80000000

test_cmplwi_1_constant:
  li r3, 1
  sldi r3, r3, 32
  cmplwi r3, 2
  mfcr r12
  blr
  #_ REGISTER_OUT r3 0x0000000100000000
  #_ REGISTER_OUT r12 0x80000000

test_cmplwi_2:
  #_ REGISTER_IN r3 2
  cmplwi r3, 1
  mfcr r12
  blr
  #_ REGISTER_OUT r3 2
  #_ REGISTER_OUT r12 0x40000000

test_cmplwi_2_constant:
  li r3, 2
  cmplwi r3, 1
  mfcr r12
  blr
  #_ REGISTER_OUT r3 2
  #_ REGISTER_OUT r12 0x40000000

test_cmplwi_5:
  #_ REGISTER_IN r3 0x0000000100000002
  cmplwi r3, 1
  mfcr r12
  blr
  #_ REGISTER_OUT r3 0x0000000100000002
  #_ REGISTER_OUT r12 0x40000000

test_cmplwi_5_constant:
  li r3, 1
  sldi r3, r3, 32
  sldi r4, r3, 1
  addi r3, r3, 2
  cmplwi r3, 1
  mfcr r12
  blr
  #_ REGISTER_OUT r3 0x0000000100000002
  #_ REGISTER_OUT r12 0x40000000

test_cmpli_1:
  #_ REGISTER_IN r3 1
  cmpli 5, 0, r3, 2
  mfcr r12
  blr
  #_ REGISTER_OUT r3 1
  #_ REGISTER_OUT r12 0x00000800

test_cmpli_1_constant:
  li r3, 1
  cmpli 5, 0, r3, 2
  mfcr r12
  blr
  #_ REGISTER_OUT r3 1
  #_ REGISTER_OUT r12 0x00000800

test_cmpli_2:
  #_ REGISTER_IN r3 2
  cmpli 3, 0, r3, 1
  mfcr r12
  blr
  #_ REGISTER_OUT r3 2
  #_ REGISTER_OUT r12 0x00040000

test_cmpli_2_constant:
  li r3, 2
  cmpli 3, 0, r3, 1
  mfcr r12
  blr
  #_ REGISTER_OUT r3 2
  #_ REGISTER_OUT r12 0x00040000
