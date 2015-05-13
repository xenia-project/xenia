.macro make_test_constant dest
  lis \dest, 0xDEAD
  ori \dest, \dest, 0xBEEF
  sldi \dest, \dest, 32
.endm

test_ori_1:
  #_ REGISTER_IN r4 0xDEADBEEF00000000

  ori r3, r4, 0xFEDC

  blr
  #_ REGISTER_OUT r3 0xDEADBEEF0000FEDC
  #_ REGISTER_OUT r4 0xDEADBEEF00000000

test_ori_1_constant:
  make_test_constant r4
  ori r3, r4, 0xFEDC

  blr
  #_ REGISTER_OUT r3 0xDEADBEEF0000FEDC
  #_ REGISTER_OUT r4 0xDEADBEEF00000000

test_ori_2:
  #_ REGISTER_IN r4 0xDEADBEEF10000000

  ori r3, r4, 0xFEDC

  blr
  #_ REGISTER_OUT r3 0xDEADBEEF1000FEDC
  #_ REGISTER_OUT r4 0xDEADBEEF10000000

test_ori_2_constant:
  make_test_constant r4
  lis r3, 0x1000
  or r4, r4, r3

  ori r3, r4, 0xFEDC

  blr
  #_ REGISTER_OUT r3 0xDEADBEEF1000FEDC
  #_ REGISTER_OUT r4 0xDEADBEEF10000000
