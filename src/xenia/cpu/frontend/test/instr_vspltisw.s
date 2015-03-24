test_vspltisw_1:
  vspltisw v3, 0
  blr
  #_ REGISTER_OUT v3 [00000000, 00000000, 00000000, 00000000]

test_vspltisw_2:
  vspltisw v3, 1
  blr
  #_ REGISTER_OUT v3 [00000001, 00000001, 00000001, 00000001]

test_vspltisw_3:
  vspltisw v3, -1
  blr
  #_ REGISTER_OUT v3 [FFFFFFFF, FFFFFFFF, FFFFFFFF, FFFFFFFF]

test_vspltisw_4:
  vspltisw v3, -2
  blr
  #_ REGISTER_OUT v3 [FFFFFFFE, FFFFFFFE, FFFFFFFE, FFFFFFFE]
