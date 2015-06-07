test_twlt_1:
  #_ REGISTER_IN r3 24
  #_ REGISTER_IN r4 16
  twlt r3, r4
  blr
  #_ REGISTER_OUT r3 24
  #_ REGISTER_OUT r4 16

test_twlt_1_constant:
  li r3, 24
  li r4, 16
  twlt r3, r4
  blr
  #_ REGISTER_OUT r3 24
  #_ REGISTER_OUT r4 16

test_twlt_2:
  #_ REGISTER_IN r3 24
  #_ REGISTER_IN r4 0
  twlt r3, r4
  blr
  #_ REGISTER_OUT r3 24
  #_ REGISTER_OUT r4 0

test_twlt_2_constant:
  li r3, 24
  li r4, 0
  twlt r3, r4
  blr
  #_ REGISTER_OUT r3 24
  #_ REGISTER_OUT r4 0

test_twle_1:
  #_ REGISTER_IN r3 24
  #_ REGISTER_IN r4 16
  twle r3, r4
  blr
  #_ REGISTER_OUT r3 24
  #_ REGISTER_OUT r4 16

test_twle_1_constant:
  li r3, 24
  li r4, 16
  twle r3, r4
  blr
  #_ REGISTER_OUT r3 24
  #_ REGISTER_OUT r4 16

test_twle_2:
  #_ REGISTER_IN r3 24
  #_ REGISTER_IN r4 0
  twle r3, r4
  blr
  #_ REGISTER_OUT r3 24
  #_ REGISTER_OUT r4 0

test_twle_2_constant:
  li r3, 24
  li r4, 0
  twle r3, r4
  blr
  #_ REGISTER_OUT r3 24
  #_ REGISTER_OUT r4 0

test_tweq_1:
  #_ REGISTER_IN r3 0
  #_ REGISTER_IN r4 24
  tweq r3, r4
  blr
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 24

test_tweq_1_constant:
  li r3, 0
  li r4, 24
  tweq r3, r4
  blr
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 24

test_tweq_2:
  #_ REGISTER_IN r3 24
  #_ REGISTER_IN r4 0
  tweq r3, r4
  blr
  #_ REGISTER_OUT r3 24
  #_ REGISTER_OUT r4 0

test_tweq_2_constant:
  li r3, 24
  li r4, 0
  tweq r3, r4
  blr
  #_ REGISTER_OUT r3 24
  #_ REGISTER_OUT r4 0

test_twge_1:
  #_ REGISTER_IN r3 24
  #_ REGISTER_IN r4 48
  twge r3, r4
  blr
  #_ REGISTER_OUT r3 24
  #_ REGISTER_OUT r4 48

test_twge_1_constant:
  li r3, 24
  li r4, 48
  twge r3, r4
  blr
  #_ REGISTER_OUT r3 24
  #_ REGISTER_OUT r4 48

test_twge_2:
  #_ REGISTER_IN r3 0
  #_ REGISTER_IN r4 48
  twge r3, r4
  blr
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 48

test_twge_2_constant:
  li r3, 0
  li r4, 48
  twge r3, r4
  blr
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 48

test_twge_3:
  #_ REGISTER_IN r3 -1
  #_ REGISTER_IN r4 0
  twge r3, r4
  blr
  #_ REGISTER_OUT r3 -1
  #_ REGISTER_OUT r4 0

test_twge_3_constant:
  li r3, -1
  li r4, 0
  twge r3, r4
  blr
  #_ REGISTER_OUT r3 -1
  #_ REGISTER_OUT r4 0

test_twgt_1:
  #_ REGISTER_IN r3 24
  #_ REGISTER_IN r4 48
  twgt r3, r4
  blr
  #_ REGISTER_OUT r3 24
  #_ REGISTER_OUT r4 48

test_twgt_1_constant:
  li r3, 24
  li r4, 48
  twgt r3, r4
  blr
  #_ REGISTER_OUT r3 24
  #_ REGISTER_OUT r4 48

test_twgt_2:
  #_ REGISTER_IN r3 0
  #_ REGISTER_IN r4 48
  twgt r3, r4
  blr
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 48

test_twgt_2_constant:
  li r3, 0
  li r4, 48
  twgt r3, r4
  blr
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 48

test_twgt_3:
  #_ REGISTER_IN r3 -1
  #_ REGISTER_IN r4 0
  twgt r3, r4
  blr
  #_ REGISTER_OUT r3 -1
  #_ REGISTER_OUT r4 0

test_twgt_3_constant:
  li r3, -1
  li r4, 0
  twgt r3, r4
  blr
  #_ REGISTER_OUT r3 -1
  #_ REGISTER_OUT r4 0

test_twnl_1:
  #_ REGISTER_IN r3 24
  #_ REGISTER_IN r4 48
  twnl r3, r4
  blr
  #_ REGISTER_OUT r3 24
  #_ REGISTER_OUT r4 48

test_twnl_1_constant:
  li r3, 24
  li r4, 48
  twnl r3, r4
  blr
  #_ REGISTER_OUT r3 24
  #_ REGISTER_OUT r4 48

test_twnl_2:
  #_ REGISTER_IN r3 0
  #_ REGISTER_IN r4 48
  twnl r3, r4
  blr
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 48

test_twnl_2_constant:
  li r3, 0
  li r4, 48
  twnl r3, r4
  blr
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 48

test_twnl_3:
  #_ REGISTER_IN r3 -1
  #_ REGISTER_IN r4 0
  twnl r3, r4
  blr
  #_ REGISTER_OUT r3 -1
  #_ REGISTER_OUT r4 0

test_twnl_3_constant:
  li r3, -1
  li r4, 0
  twnl r3, r4
  blr
  #_ REGISTER_OUT r3 -1
  #_ REGISTER_OUT r4 0

test_twne_1:
  #_ REGISTER_IN r3 24
  #_ REGISTER_IN r4 24
  twne r3, r4
  blr
  #_ REGISTER_OUT r3 24
  #_ REGISTER_OUT r4 24

test_twne_1_constant:
  li r3, 24
  li r4, 24
  twne r3, r4
  blr
  #_ REGISTER_OUT r3 24
  #_ REGISTER_OUT r4 24

test_twne_2:
  #_ REGISTER_IN r3 0
  #_ REGISTER_IN r4 0
  twne r3, r4
  blr
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 0

test_twne_2_constant:
  li r3, 0
  li r4, 0
  twne r3, r4
  blr
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 0

test_twng_1:
  #_ REGISTER_IN r3 24
  #_ REGISTER_IN r4 16
  twng r3, r4
  blr
  #_ REGISTER_OUT r3 24
  #_ REGISTER_OUT r4 16

test_twng_1_constant:
  li r3, 24
  li r4, 16
  twng r3, r4
  blr
  #_ REGISTER_OUT r3 24
  #_ REGISTER_OUT r4 16

test_twng_2:
  #_ REGISTER_IN r3 24
  #_ REGISTER_IN r4 0
  twng r3, r4
  blr
  #_ REGISTER_OUT r3 24
  #_ REGISTER_OUT r4 0

test_twng_2_constant:
  li r3, 24
  li r4, 0
  twng r3, r4
  blr
  #_ REGISTER_OUT r3 24
  #_ REGISTER_OUT r4 0

test_twng_3:
  #_ REGISTER_IN r3 0
  #_ REGISTER_IN r4 -1
  twng r3, r4
  blr
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 -1

test_twng_3_constant:
  li r3, 0
  li r4, -1
  twng r3, r4
  blr
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 -1

test_twllt_1:
  #_ REGISTER_IN r3 24
  #_ REGISTER_IN r4 16
  twllt r3, r4
  blr
  #_ REGISTER_OUT r3 24
  #_ REGISTER_OUT r4 16

test_twllt_1_constant:
  li r3, 24
  li r4, 16
  twllt r3, r4
  blr
  #_ REGISTER_OUT r3 24
  #_ REGISTER_OUT r4 16

test_twllt_2:
  #_ REGISTER_IN r3 24
  #_ REGISTER_IN r4 0
  twllt r3, r4
  blr
  #_ REGISTER_OUT r3 24
  #_ REGISTER_OUT r4 0

test_twllt_2_constant:
  li r3, 24
  li r4, 0
  twllt r3, r4
  blr
  #_ REGISTER_OUT r3 24
  #_ REGISTER_OUT r4 0

test_twlle_1:
  #_ REGISTER_IN r3 24
  #_ REGISTER_IN r4 16
  twlle r3, r4
  blr
  #_ REGISTER_OUT r3 24
  #_ REGISTER_OUT r4 16

test_twlle_1_constant:
  li r3, 24
  li r4, 16
  twlle r3, r4
  blr
  #_ REGISTER_OUT r3 24
  #_ REGISTER_OUT r4 16

test_twlle_2:
  #_ REGISTER_IN r3 24
  #_ REGISTER_IN r4 0
  twlle r3, r4
  blr
  #_ REGISTER_OUT r3 24
  #_ REGISTER_OUT r4 0

test_twlle_2_constant:
  li r3, 24
  li r4, 0
  twlle r3, r4
  blr
  #_ REGISTER_OUT r3 24
  #_ REGISTER_OUT r4 0

test_twlge_1:
  #_ REGISTER_IN r3 24
  #_ REGISTER_IN r4 48
  twlge r3, r4
  blr
  #_ REGISTER_OUT r3 24
  #_ REGISTER_OUT r4 48

test_twlge_1_constant:
  li r3, 24
  li r4, 48
  twlge r3, r4
  blr
  #_ REGISTER_OUT r3 24
  #_ REGISTER_OUT r4 48

test_twlge_2:
  #_ REGISTER_IN r3 0
  #_ REGISTER_IN r4 48
  twlge r3, r4
  blr
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 48

test_twlge_2_constant:
  li r3, 0
  li r4, 48
  twlge r3, r4
  blr
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 48

test_twlgt_1:
  #_ REGISTER_IN r3 24
  #_ REGISTER_IN r4 48
  twlgt r3, r4
  blr
  #_ REGISTER_OUT r3 24
  #_ REGISTER_OUT r4 48

test_twlgt_1_constant:
  li r3, 24
  li r4, 48
  twlgt r3, r4
  blr
  #_ REGISTER_OUT r3 24
  #_ REGISTER_OUT r4 48

test_twlgt_2:
  #_ REGISTER_IN r3 0
  #_ REGISTER_IN r4 48
  twlgt r3, r4
  blr
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 48

test_twlgt_2_constant:
  li r3, 0
  li r4, 48
  twlgt r3, r4
  blr
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 48

test_twlnl_1:
  #_ REGISTER_IN r3 24
  #_ REGISTER_IN r4 48
  twlnl r3, r4
  blr
  #_ REGISTER_OUT r3 24
  #_ REGISTER_OUT r4 48

test_twlnl_1_constant:
  li r3, 24
  li r4, 48
  twlnl r3, r4
  blr
  #_ REGISTER_OUT r3 24
  #_ REGISTER_OUT r4 48

test_twlnl_2:
  #_ REGISTER_IN r3 0
  #_ REGISTER_IN r4 48
  twlnl r3, r4
  blr
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 48

test_twlnl_2_constant:
  li r3, 0
  li r4, 48
  twlnl r3, r4
  blr
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 48

test_twlng_1:
  #_ REGISTER_IN r3 24
  #_ REGISTER_IN r4 16
  twlng r3, r4
  blr
  #_ REGISTER_OUT r3 24
  #_ REGISTER_OUT r4 16

test_twlng_1_constant:
  li r3, 24
  li r4, 16
  twlng r3, r4
  blr
  #_ REGISTER_OUT r3 24
  #_ REGISTER_OUT r4 16

test_twlng_2:
  #_ REGISTER_IN r3 24
  #_ REGISTER_IN r4 0
  twlng r3, r4
  blr
  #_ REGISTER_OUT r3 24
  #_ REGISTER_OUT r4 0

test_twlng_2_constant:
  li r3, 24
  li r4, 0
  twlng r3, r4
  blr
  #_ REGISTER_OUT r3 24
  #_ REGISTER_OUT r4 0
