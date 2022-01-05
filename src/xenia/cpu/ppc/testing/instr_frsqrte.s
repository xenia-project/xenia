# frsqrte tests disabled because accuracy is CPU dependent.

test_frsqrte_1:
  # _ REGISTER_IN f1 1.0
#  frsqrte f1, f1
  blr
  # _ REGISTER_OUT f1 0.99975585937500000
  # want: 0.97

test_frsqrte_2:
  # _ REGISTER_IN f1 64.0
#  frsqrte f1, f1
  blr
  # _ REGISTER_OUT f1 0.12496948242187500

test_frsqrte_3:
  # _ REGISTER_IN f1 0.5
#  frsqrte f1, f1
  blr
  # _ REGISTER_OUT f1 1.41381835937500000
  # want: 1.375
