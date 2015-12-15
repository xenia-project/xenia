test_fmul_1:
  #_ REGISTER_IN f1 5.0
  #_ REGISTER_IN f2 5.0
  fmul f3, f1, f2
  blr
  #_ REGISTER_OUT f3 25.0

test_fmul_2:
  #_ REGISTER_IN f1 5.0
  #_ REGISTER_IN f2 0.0
  fmul f3, f1, f2
  blr
  #_ REGISTER_OUT f3 0.0

test_fmul_3:
  #_ REGISTER_IN f1 -2.0
  #_ REGISTER_IN f2 2.0
  fmul f3, f1, f2
  blr
  #_ REGISTER_OUT f3 -4.0
