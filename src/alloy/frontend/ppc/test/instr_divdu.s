test_divdu_1:
  #_ REGISTER_IN r4 1
  #_ REGISTER_IN r5 2
  divdu r3, r4, r5
  blr
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 1
  #_ REGISTER_OUT r5 2

# TODO(benvanik): x64 ignore divide by zero (=0)
#test_divdu_2:
#  #_ REGISTER_IN r4 1
#  #_ REGISTER_IN r5 0
#  divdu r3, r4, r5
#  blr
#  #_ REGISTER_OUT r3 0
#  #_ REGISTER_OUT r4 1
#  #_ REGISTER_OUT r5 0

test_divdu_3:
  #_ REGISTER_IN r4 2
  #_ REGISTER_IN r5 1
  divdu r3, r4, r5
  blr
  #_ REGISTER_OUT r3 2
  #_ REGISTER_OUT r4 2
  #_ REGISTER_OUT r5 1

test_divdu_4:
  #_ REGISTER_IN r4 35
  #_ REGISTER_IN r5 7
  divdu r3, r4, r5
  blr
  #_ REGISTER_OUT r3 5
  #_ REGISTER_OUT r4 35
  #_ REGISTER_OUT r5 7

test_divdu_5:
  #_ REGISTER_IN r4 0
  #_ REGISTER_IN r5 1
  divdu r3, r4, r5
  blr
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 0
  #_ REGISTER_OUT r5 1

test_divdu_6:
  #_ REGISTER_IN r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_IN r5 1
  divdu r3, r4, r5
  blr
  #_ REGISTER_OUT r3 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r5 1

test_divdu_7:
  #_ REGISTER_IN r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_IN r5 0xFFFFFFFFFFFFFFFF
  divdu r3, r4, r5
  blr
  #_ REGISTER_OUT r3 1
  #_ REGISTER_OUT r4 0xFFFFFFFFFFFFFFFF
  #_ REGISTER_OUT r5 0xFFFFFFFFFFFFFFFF

test_divdu_8:
  #_ REGISTER_IN r4 1
  #_ REGISTER_IN r5 0xFFFFFFFFFFFFFFFF
  divdu r3, r4, r5
  blr
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 1
  #_ REGISTER_OUT r5 0xFFFFFFFFFFFFFFFF

test_divdu_9:
  #_ REGISTER_IN r4 0x8000000000000000
  #_ REGISTER_IN r5 -1
  divdu r3, r4, r5
  blr
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 0x8000000000000000
  #_ REGISTER_OUT r5 -1
