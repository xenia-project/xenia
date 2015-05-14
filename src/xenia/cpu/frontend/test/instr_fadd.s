test_fadd_1:
  #_ REGISTER_IN f1 1.0
  #_ REGISTER_IN f2 2.0
  fadd f3, f1, f2
  blr
  #_ REGISTER_OUT f3 3.0

test_fadd_2:
  #_ REGISTER_IN f1 0.0
  #_ REGISTER_IN f2 0.0
  fadd f3, f1, f2
  blr
  #_ REGISTER_OUT f3 0.0

test_fadd_3:
  #_ REGISTER_IN f1 -200.0
  #_ REGISTER_IN f2 200.0
  fadd f3, f1, f2
  blr
  #_ REGISTER_OUT f3 0.0
