test_tdlti_1:
  #_ REGISTER_IN r3 24
  tdlti r3, 16
  blr
  #_ REGISTER_OUT r3 24

test_tdlti_1_constant:
  li r3, 24
  tdlti r3, 16
  blr
  #_ REGISTER_OUT r3 24

test_tdlti_2:
  #_ REGISTER_IN r3 24
  tdlti r3, 0
  blr
  #_ REGISTER_OUT r3 24

test_tdlti_2_constant:
  li r3, 24
  tdlti r3, 0
  blr
  #_ REGISTER_OUT r3 24

test_tdlei_1:
  #_ REGISTER_IN r3 24
  tdlei r3, 16
  blr
  #_ REGISTER_OUT r3 24

test_tdlei_1_constant:
  li r3, 24
  tdlei r3, 16
  blr
  #_ REGISTER_OUT r3 24

test_tdlei_2:
  #_ REGISTER_IN r3 24
  tdlei r3, 0
  blr
  #_ REGISTER_OUT r3 24

test_tdlei_2_constant:
  li r3, 24
  tdlei r3, 0
  blr
  #_ REGISTER_OUT r3 24

test_tdeqi_1:
  #_ REGISTER_IN r3 0
  tdeqi r3, 24
  blr
  #_ REGISTER_OUT r3 0

test_tdeqi_1_constant:
  li r3, 0
  tdeqi r3, 24
  blr
  #_ REGISTER_OUT r3 0

test_tdeqi_2:
  #_ REGISTER_IN r3 24
  tdeqi r3, 0
  blr
  #_ REGISTER_OUT r3 24

test_tdeqi_2_constant:
  li r3, 24
  tdeqi r3, 0
  blr
  #_ REGISTER_OUT r3 24

test_tdgei_1:
  #_ REGISTER_IN r3 24
  tdgei r3, 48
  blr
  #_ REGISTER_OUT r3 24

test_tdgei_1_constant:
  li r3, 24
  tdgei r3, 48
  blr
  #_ REGISTER_OUT r3 24

test_tdgei_2:
  #_ REGISTER_IN r3 0
  tdgei r3, 48
  blr
  #_ REGISTER_OUT r3 0

test_tdgei_2_constant:
  li r3, 0
  tdgei r3, 48
  blr
  #_ REGISTER_OUT r3 0

test_tdgei_3:
  #_ REGISTER_IN r3 -1
  tdgei r3, 0
  blr
  #_ REGISTER_OUT r3 -1

test_tdgei_3_constant:
  li r3, -1
  tdgei r3, 0
  blr
  #_ REGISTER_OUT r3 -1

test_tdgti_1:
  #_ REGISTER_IN r3 24
  tdgti r3, 48
  blr
  #_ REGISTER_OUT r3 24

test_tdgti_1_constant:
  li r3, 24
  tdgti r3, 48
  blr
  #_ REGISTER_OUT r3 24

test_tdgti_2:
  #_ REGISTER_IN r3 0
  tdgti r3, 48
  blr
  #_ REGISTER_OUT r3 0

test_tdgti_2_constant:
  li r3, 0
  tdgti r3, 48
  blr
  #_ REGISTER_OUT r3 0

test_tdgti_3:
  #_ REGISTER_IN r3 -1
  tdgti r3, 0
  blr
  #_ REGISTER_OUT r3 -1

test_tdgti_3_constant:
  li r3, -1
  tdgti r3, 0
  blr
  #_ REGISTER_OUT r3 -1

test_tdnli_1:
  #_ REGISTER_IN r3 24
  tdnli r3, 48
  blr
  #_ REGISTER_OUT r3 24

test_tdnli_1_constant:
  li r3, 24
  tdnli r3, 48
  blr
  #_ REGISTER_OUT r3 24

test_tdnli_2:
  #_ REGISTER_IN r3 0
  tdnli r3, 48
  blr
  #_ REGISTER_OUT r3 0

test_tdnli_2_constant:
  li r3, 0
  tdnli r3, 48
  blr
  #_ REGISTER_OUT r3 0

test_tdnli_3:
  #_ REGISTER_IN r3 -1
  tdnli r3, 0
  blr
  #_ REGISTER_OUT r3 -1

test_tdnli_3_constant:
  li r3, -1
  tdnli r3, 0
  blr
  #_ REGISTER_OUT r3 -1

test_tdnei_1:
  #_ REGISTER_IN r3 24
  tdnei r3, 24
  blr
  #_ REGISTER_OUT r3 24

test_tdnei_1_constant:
  li r3, 24
  tdnei r3, 24
  blr
  #_ REGISTER_OUT r3 24

test_tdnei_2:
  #_ REGISTER_IN r3 0
  tdnei r3, 0
  blr
  #_ REGISTER_OUT r3 0

test_tdnei_2_constant:
  li r3, 0
  tdnei r3, 0
  blr
  #_ REGISTER_OUT r3 0

test_tdngi_1:
  #_ REGISTER_IN r3 24
  tdngi r3, 16
  blr
  #_ REGISTER_OUT r3 24

test_tdngi_1_constant:
  li r3, 24
  tdngi r3, 16
  blr
  #_ REGISTER_OUT r3 24

test_tdngi_2:
  #_ REGISTER_IN r3 24
  tdngi r3, 0
  blr
  #_ REGISTER_OUT r3 24

test_tdngi_2_constant:
  li r3, 24
  tdngi r3, 0
  blr
  #_ REGISTER_OUT r3 24

test_tdngi_3:
  #_ REGISTER_IN r3 0
  tdngi r3, -1
  blr
  #_ REGISTER_OUT r3 0

test_tdngi_3_constant:
  li r3, 0
  tdngi r3, -1
  blr
  #_ REGISTER_OUT r3 0

test_tdllti_1:
  #_ REGISTER_IN r3 24
  tdllti r3, 16
  blr
  #_ REGISTER_OUT r3 24

test_tdllti_1_constant:
  li r3, 24
  tdllti r3, 16
  blr
  #_ REGISTER_OUT r3 24

test_tdllti_2:
  #_ REGISTER_IN r3 24
  tdllti r3, 0
  blr
  #_ REGISTER_OUT r3 24

test_tdllti_2_constant:
  li r3, 24
  tdllti r3, 0
  blr
  #_ REGISTER_OUT r3 24

test_tdllei_1:
  #_ REGISTER_IN r3 24
  tdllei r3, 16
  blr
  #_ REGISTER_OUT r3 24

test_tdllei_1_constant:
  li r3, 24
  tdllei r3, 16
  blr
  #_ REGISTER_OUT r3 24

test_tdllei_2:
  #_ REGISTER_IN r3 24
  tdllei r3, 0
  blr
  #_ REGISTER_OUT r3 24

test_tdllei_2_constant:
  li r3, 24
  tdllei r3, 0
  blr
  #_ REGISTER_OUT r3 24

test_tdlgei_1:
  #_ REGISTER_IN r3 24
  tdlgei r3, 48
  blr
  #_ REGISTER_OUT r3 24

test_tdlgei_1_constant:
  li r3, 24
  tdlgei r3, 48
  blr
  #_ REGISTER_OUT r3 24

test_tdlgei_2:
  #_ REGISTER_IN r3 0
  tdlgei r3, 48
  blr
  #_ REGISTER_OUT r3 0

test_tdlgei_2_constant:
  li r3, 0
  tdlgei r3, 48
  blr
  #_ REGISTER_OUT r3 0

test_tdlgti_1:
  #_ REGISTER_IN r3 24
  tdlgti r3, 48
  blr
  #_ REGISTER_OUT r3 24

test_tdlgti_1_constant:
  li r3, 24
  tdlgti r3, 48
  blr
  #_ REGISTER_OUT r3 24

test_tdlgti_2:
  #_ REGISTER_IN r3 0
  tdlgti r3, 48
  blr
  #_ REGISTER_OUT r3 0

test_tdlgti_2_constant:
  li r3, 0
  tdlgti r3, 48
  blr
  #_ REGISTER_OUT r3 0

test_tdlnli_1:
  #_ REGISTER_IN r3 24
  tdlnli r3, 48
  blr
  #_ REGISTER_OUT r3 24

test_tdlnli_1_constant:
  li r3, 24
  tdlnli r3, 48
  blr
  #_ REGISTER_OUT r3 24

test_tdlnli_2:
  #_ REGISTER_IN r3 0
  tdlnli r3, 48
  blr
  #_ REGISTER_OUT r3 0

test_tdlnli_2_constant:
  li r3, 0
  tdlnli r3, 48
  blr
  #_ REGISTER_OUT r3 0

test_tdlngi_1:
  #_ REGISTER_IN r3 24
  tdlngi r3, 16
  blr
  #_ REGISTER_OUT r3 24

test_tdlngi_1_constant:
  li r3, 24
  tdlngi r3, 16
  blr
  #_ REGISTER_OUT r3 24

test_tdlngi_2:
  #_ REGISTER_IN r3 24
  tdlngi r3, 0
  blr
  #_ REGISTER_OUT r3 24

test_tdlngi_2_constant:
  li r3, 24
  tdlngi r3, 0
  blr
  #_ REGISTER_OUT r3 24
