test_vspltisb_1:
  vspltisb v3, 0
  blr
  #_ REGISTER_OUT v3 [00000000, 00000000, 00000000, 00000000]

test_vspltisb_2:
  vspltisb v3, 1
  blr
  #_ REGISTER_OUT v3 [01010101, 01010101, 01010101, 01010101]

test_vspltisb_3:
  vspltisb v3, -1
  blr
  #_ REGISTER_OUT v3 [FFFFFFFF, FFFFFFFF, FFFFFFFF, FFFFFFFF]

test_vspltisb_4:
  vspltisb v3, -2
  blr
  #_ REGISTER_OUT v3 [FEFEFEFE, FEFEFEFE, FEFEFEFE, FEFEFEFE]
