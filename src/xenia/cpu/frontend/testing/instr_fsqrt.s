test_fsqrt_1:
  #_ REGISTER_IN f1 1.0
  fsqrt f1, f1
  blr
  #_ REGISTER_OUT f1 1.0000000000000000

test_fsqrt_2:
  #_ REGISTER_IN f1 64.0
  fsqrt f1, f1
  blr
  #_ REGISTER_OUT f1 8.0000000000000000

test_fsqrt_3:
  #_ REGISTER_IN f1 0.5
  fsqrt f1, f1
  blr
  #_ REGISTER_OUT f1 0.70710678118654757
