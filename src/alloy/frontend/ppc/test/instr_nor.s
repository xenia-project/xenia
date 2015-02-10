test_nor_cr_1:
  #_ REGISTER_IN r3 0x00000000FFFFFFFF
  nor. r3, r3, r3
  li r3, 0
  bne .nor_cr_1_ne
  li r3, 1
  .nor_cr_1_ne:
  blr
  #_ REGISTER_OUT r3 1
