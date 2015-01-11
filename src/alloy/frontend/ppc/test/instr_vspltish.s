test_vspltish_1:
  vspltish v3, 0
  blr
  #_ REGISTER_OUT v3 [00000000, 00000000, 00000000, 00000000]

test_vspltish_2:
  vspltish v3, 1
  blr
  #_ REGISTER_OUT v3 [00010001, 00010001, 00010001, 00010001]

test_vspltish_3:
  vspltish v3, -1
  blr
  #_ REGISTER_OUT v3 [FFFFFFFF, FFFFFFFF, FFFFFFFF, FFFFFFFF]

test_vspltish_4:
  vspltish v3, -2
  blr
  #_ REGISTER_OUT v3 [FFFEFFFE, FFFEFFFE, FFFEFFFE, FFFEFFFE]
