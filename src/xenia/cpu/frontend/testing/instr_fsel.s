test_fsel_1:
  #_ REGISTER_IN f2 2.0
  #_ REGISTER_IN f3 3.0
  #_ REGISTER_IN f4 4.0
  fsel f1, f2, f3, f4
  blr
  #_ REGISTER_OUT f1 3.0
  #_ REGISTER_OUT f2 2.0
  #_ REGISTER_OUT f3 3.0
  #_ REGISTER_OUT f4 4.0

test_fsel_2:
  #_ REGISTER_IN f2 -2.0
  #_ REGISTER_IN f3 3.0
  #_ REGISTER_IN f4 4.0
  fsel f1, f2, f3, f4
  blr
  #_ REGISTER_OUT f1 4.0
  #_ REGISTER_OUT f2 -2.0
  #_ REGISTER_OUT f3 3.0
  #_ REGISTER_OUT f4 4.0

test_fsel_3:
  #_ REGISTER_IN f2 0.0
  #_ REGISTER_IN f3 3.0
  #_ REGISTER_IN f4 4.0
  fsel f1, f2, f3, f4
  blr
  #_ REGISTER_OUT f1 3.0
  #_ REGISTER_OUT f2 0.0
  #_ REGISTER_OUT f3 3.0
  #_ REGISTER_OUT f4 4.0
