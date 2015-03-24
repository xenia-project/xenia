test_cntlzw_1:
  #_ REGISTER_IN r5 0
  cntlzw r6, r5
  blr
  #_ REGISTER_OUT r5 0
  #_ REGISTER_OUT r6 32

test_cntlzw_2:
  #_ REGISTER_IN r5 1
  cntlzw r6, r5
  blr
  #_ REGISTER_OUT r5 1
  #_ REGISTER_OUT r6 31

test_cntlzw_3:
  #_ REGISTER_IN r5 0xFFFFFFFF
  cntlzw r6, r5
  blr
  #_ REGISTER_OUT r5 0xFFFFFFFF
  #_ REGISTER_OUT r6 0

test_cntlzw_4:
  #_ REGISTER_IN r5 0x7FFFFFFF
  cntlzw r6, r5
  blr
  #_ REGISTER_OUT r5 0x7FFFFFFF
  #_ REGISTER_OUT r6 1
