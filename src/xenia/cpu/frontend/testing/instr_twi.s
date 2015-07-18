test_twlti_1:
  #_ REGISTER_IN r3 24
  twlti r3, 16
  blr
  #_ REGISTER_OUT r3 24

test_twlti_1_constant:
  li r3, 24
  twlti r3, 16
  blr
  #_ REGISTER_OUT r3 24

test_twlti_2:
  #_ REGISTER_IN r3 24
  twlti r3, 0
  blr
  #_ REGISTER_OUT r3 24

test_twlti_2_constant:
  li r3, 24
  twlti r3, 0
  blr
  #_ REGISTER_OUT r3 24

test_twlei_1:
  #_ REGISTER_IN r3 24
  twlei r3, 16
  blr
  #_ REGISTER_OUT r3 24

test_twlei_1_constant:
  li r3, 24
  twlei r3, 16
  blr
  #_ REGISTER_OUT r3 24

test_twlei_2:
  #_ REGISTER_IN r3 24
  twlei r3, 0
  blr
  #_ REGISTER_OUT r3 24

test_twlei_2_constant:
  li r3, 24
  twlei r3, 0
  blr
  #_ REGISTER_OUT r3 24

test_tweqi_1:
  #_ REGISTER_IN r3 0
  tweqi r3, 24
  blr
  #_ REGISTER_OUT r3 0

test_tweqi_1_constant:
  li r3, 0
  tweqi r3, 24
  blr
  #_ REGISTER_OUT r3 0

test_tweqi_2:
  #_ REGISTER_IN r3 24
  tweqi r3, 0
  blr
  #_ REGISTER_OUT r3 24

test_tweqi_2_constant:
  li r3, 24
  tweqi r3, 0
  blr
  #_ REGISTER_OUT r3 24

test_twgei_1:
  #_ REGISTER_IN r3 24
  twgei r3, 48
  blr
  #_ REGISTER_OUT r3 24

test_twgei_1_constant:
  li r3, 24
  twgei r3, 48
  blr
  #_ REGISTER_OUT r3 24

test_twgei_2:
  #_ REGISTER_IN r3 0
  twgei r3, 48
  blr
  #_ REGISTER_OUT r3 0

test_twgei_2_constant:
  li r3, 0
  twgei r3, 48
  blr
  #_ REGISTER_OUT r3 0

test_twgei_3:
  #_ REGISTER_IN r3 -1
  twgei r3, 0
  blr
  #_ REGISTER_OUT r3 -1

test_twgei_3_constant:
  li r3, -1
  twgei r3, 0
  blr
  #_ REGISTER_OUT r3 -1

test_twgti_1:
  #_ REGISTER_IN r3 24
  twgti r3, 48
  blr
  #_ REGISTER_OUT r3 24

test_twgti_1_constant:
  li r3, 24
  twgti r3, 48
  blr
  #_ REGISTER_OUT r3 24

test_twgti_2:
  #_ REGISTER_IN r3 0
  twgti r3, 48
  blr
  #_ REGISTER_OUT r3 0

test_twgti_2_constant:
  li r3, 0
  twgti r3, 48
  blr
  #_ REGISTER_OUT r3 0

test_twgti_3:
  #_ REGISTER_IN r3 -1
  twgti r3, 0
  blr
  #_ REGISTER_OUT r3 -1

test_twgti_3_constant:
  li r3, -1
  twgti r3, 0
  blr
  #_ REGISTER_OUT r3 -1

test_twnli_1:
  #_ REGISTER_IN r3 24
  twnli r3, 48
  blr
  #_ REGISTER_OUT r3 24

test_twnli_1_constant:
  li r3, 24
  twnli r3, 48
  blr
  #_ REGISTER_OUT r3 24

test_twnli_2:
  #_ REGISTER_IN r3 0
  twnli r3, 48
  blr
  #_ REGISTER_OUT r3 0

test_twnli_2_constant:
  li r3, 0
  twnli r3, 48
  blr
  #_ REGISTER_OUT r3 0

test_twnli_3:
  #_ REGISTER_IN r3 -1
  twnli r3, 0
  blr
  #_ REGISTER_OUT r3 -1

test_twnli_3_constant:
  li r3, -1
  twnli r3, 0
  blr
  #_ REGISTER_OUT r3 -1

test_twnei_1:
  #_ REGISTER_IN r3 24
  twnei r3, 24
  blr
  #_ REGISTER_OUT r3 24

test_twnei_1_constant:
  li r3, 24
  twnei r3, 24
  blr
  #_ REGISTER_OUT r3 24

test_twnei_2:
  #_ REGISTER_IN r3 0
  twnei r3, 0
  blr
  #_ REGISTER_OUT r3 0

test_twnei_2_constant:
  li r3, 0
  twnei r3, 0
  blr
  #_ REGISTER_OUT r3 0

test_twngi_1:
  #_ REGISTER_IN r3 24
  twngi r3, 16
  blr
  #_ REGISTER_OUT r3 24

test_twngi_1_constant:
  li r3, 24
  twngi r3, 16
  blr
  #_ REGISTER_OUT r3 24

test_twngi_2:
  #_ REGISTER_IN r3 24
  twngi r3, 0
  blr
  #_ REGISTER_OUT r3 24

test_twngi_2_constant:
  li r3, 24
  twngi r3, 0
  blr
  #_ REGISTER_OUT r3 24

test_twngi_3:
  #_ REGISTER_IN r3 0
  twngi r3, -1
  blr
  #_ REGISTER_OUT r3 0

test_twngi_3_constant:
  li r3, 0
  twngi r3, -1
  blr
  #_ REGISTER_OUT r3 0

test_twllti_1:
  #_ REGISTER_IN r3 24
  twllti r3, 16
  blr
  #_ REGISTER_OUT r3 24

test_twllti_1_constant:
  li r3, 24
  twllti r3, 16
  blr
  #_ REGISTER_OUT r3 24

test_twllti_2:
  #_ REGISTER_IN r3 24
  twllti r3, 0
  blr
  #_ REGISTER_OUT r3 24

test_twllti_2_constant:
  li r3, 24
  twllti r3, 0
  blr
  #_ REGISTER_OUT r3 24

test_twllei_1:
  #_ REGISTER_IN r3 24
  twllei r3, 16
  blr
  #_ REGISTER_OUT r3 24

test_twllei_1_constant:
  li r3, 24
  twllei r3, 16
  blr
  #_ REGISTER_OUT r3 24

test_twllei_2:
  #_ REGISTER_IN r3 24
  twllei r3, 0
  blr
  #_ REGISTER_OUT r3 24

test_twllei_2_constant:
  li r3, 24
  twllei r3, 0
  blr
  #_ REGISTER_OUT r3 24

test_twlgei_1:
  #_ REGISTER_IN r3 24
  twlgei r3, 48
  blr
  #_ REGISTER_OUT r3 24

test_twlgei_1_constant:
  li r3, 24
  twlgei r3, 48
  blr
  #_ REGISTER_OUT r3 24

test_twlgei_2:
  #_ REGISTER_IN r3 0
  twlgei r3, 48
  blr
  #_ REGISTER_OUT r3 0

test_twlgei_2_constant:
  li r3, 0
  twlgei r3, 48
  blr
  #_ REGISTER_OUT r3 0

test_twlgti_1:
  #_ REGISTER_IN r3 24
  twlgti r3, 48
  blr
  #_ REGISTER_OUT r3 24

test_twlgti_1_constant:
  li r3, 24
  twlgti r3, 48
  blr
  #_ REGISTER_OUT r3 24

test_twlgti_2:
  #_ REGISTER_IN r3 0
  twlgti r3, 48
  blr
  #_ REGISTER_OUT r3 0

test_twlgti_2_constant:
  li r3, 0
  twlgti r3, 48
  blr
  #_ REGISTER_OUT r3 0

test_twlnli_1:
  #_ REGISTER_IN r3 24
  twlnli r3, 48
  blr
  #_ REGISTER_OUT r3 24

test_twlnli_1_constant:
  li r3, 24
  twlnli r3, 48
  blr
  #_ REGISTER_OUT r3 24

test_twlnli_2:
  #_ REGISTER_IN r3 0
  twlnli r3, 48
  blr
  #_ REGISTER_OUT r3 0

test_twlnli_2_constant:
  li r3, 0
  twlnli r3, 48
  blr
  #_ REGISTER_OUT r3 0

test_twlngi_1:
  #_ REGISTER_IN r3 24
  twlngi r3, 16
  blr
  #_ REGISTER_OUT r3 24

test_twlngi_1_constant:
  li r3, 24
  twlngi r3, 16
  blr
  #_ REGISTER_OUT r3 24

test_twlngi_2:
  #_ REGISTER_IN r3 24
  twlngi r3, 0
  blr
  #_ REGISTER_OUT r3 24

test_twlngi_2_constant:
  li r3, 24
  twlngi r3, 0
  blr
  #_ REGISTER_OUT r3 24
