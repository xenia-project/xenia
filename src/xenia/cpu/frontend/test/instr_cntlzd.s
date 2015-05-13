test_cntlzd_1:
  #_ REGISTER_IN r5 0
  cntlzd r6, r5
  blr
  #_ REGISTER_OUT r5 0
  #_ REGISTER_OUT r6 64

test_cntlzd_1_constant:
  li r5, 0
  cntlzd r6, r5
  blr
  #_ REGISTER_OUT r5 0
  #_ REGISTER_OUT r6 64

test_cntlzd_2:
  #_ REGISTER_IN r5 1
  cntlzd r6, r5
  blr
  #_ REGISTER_OUT r5 1
  #_ REGISTER_OUT r6 63

test_cntlzd_2_constant:
  li r5, 1
  cntlzd r6, r5
  blr
  #_ REGISTER_OUT r5 1
  #_ REGISTER_OUT r6 63

test_cntlzd_3:
  #_ REGISTER_IN r5 0xFFFFFFFFFFFFFFFF
  cntlzd r6, r5
  blr
  #_ REGISTER_OUT r5 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r6 0

test_cntlzd_3_constant:
  li r5, -1
  cntlzd r6, r5
  blr
  #_ REGISTER_OUT r5 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r6 0

test_cntlzd_4:
  #_ REGISTER_IN r5 0x7FFFFFFFFFFFFFFF
  cntlzd r6, r5
  blr
  #_ REGISTER_OUT r5 0x7FFFFFFFFFFFFFFF
  #_ REGISTER_OUT r6 1

test_cntlzd_4_constant:
  li r5, -1
  srdi r5, r5, 1
  cntlzd r6, r5
  blr
  #_ REGISTER_OUT r5 0x7FFFFFFFFFFFFFFF
  #_ REGISTER_OUT r6 1
