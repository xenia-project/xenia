test_fabs_1:
  #_ REGISTER_IN f1 1.0
  fabs f1, f1
  blr
  #_ REGISTER_OUT f1 1.0

test_fabs_2:
  #_ REGISTER_IN f1 -1.0
  fabs f1, f1
  blr
  #_ REGISTER_OUT f1 1.0

test_fabs_3:
  #_ REGISTER_IN f1 -1234.0
  fabs f1, f1
  blr
  #_ REGISTER_OUT f1 1234.0
