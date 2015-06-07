test_tdlt_1:
  #_ REGISTER_IN r3 24
  #_ REGISTER_IN r4 16
  tdlt r3, r4
  blr
  #_ REGISTER_OUT r3 24
  #_ REGISTER_OUT r4 16

test_tdlt_1_constant:
  li r3, 24
  li r4, 16
  tdlt r3, r4
  blr
  #_ REGISTER_OUT r3 24
  #_ REGISTER_OUT r4 16

test_tdlt_2:
  #_ REGISTER_IN r3 24
  #_ REGISTER_IN r4 0
  tdlt r3, r4
  blr
  #_ REGISTER_OUT r3 24
  #_ REGISTER_OUT r4 0

test_tdlt_2_constant:
  li r3, 24
  li r4, 0
  tdlt r3, r4
  blr
  #_ REGISTER_OUT r3 24
  #_ REGISTER_OUT r4 0

test_tdle_1:
  #_ REGISTER_IN r3 24
  #_ REGISTER_IN r4 16
  tdle r3, r4
  blr
  #_ REGISTER_OUT r3 24
  #_ REGISTER_OUT r4 16

test_tdle_1_constant:
  li r3, 24
  li r4, 16
  tdle r3, r4
  blr
  #_ REGISTER_OUT r3 24
  #_ REGISTER_OUT r4 16

test_tdle_2:
  #_ REGISTER_IN r3 24
  #_ REGISTER_IN r4 0
  tdle r3, r4
  blr
  #_ REGISTER_OUT r3 24
  #_ REGISTER_OUT r4 0

test_tdle_2_constant:
  li r3, 24
  li r4, 0
  tdle r3, r4
  blr
  #_ REGISTER_OUT r3 24
  #_ REGISTER_OUT r4 0

test_tdeq_1:
  #_ REGISTER_IN r3 0
  #_ REGISTER_IN r4 24
  tdeq r3, r4
  blr
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 24

test_tdeq_1_constant:
  li r3, 0
  li r4, 24
  tdeq r3, r4
  blr
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 24

test_tdeq_2:
  #_ REGISTER_IN r3 24
  #_ REGISTER_IN r4 0
  tdeq r3, r4
  blr
  #_ REGISTER_OUT r3 24
  #_ REGISTER_OUT r4 0

test_tdeq_2_constant:
  li r3, 24
  li r4, 0
  tdeq r3, r4
  blr
  #_ REGISTER_OUT r3 24
  #_ REGISTER_OUT r4 0

test_tdge_1:
  #_ REGISTER_IN r3 24
  #_ REGISTER_IN r4 48
  tdge r3, r4
  blr
  #_ REGISTER_OUT r3 24
  #_ REGISTER_OUT r4 48

test_tdge_1_constant:
  li r3, 24
  li r4, 48
  tdge r3, r4
  blr
  #_ REGISTER_OUT r3 24
  #_ REGISTER_OUT r4 48

test_tdge_2:
  #_ REGISTER_IN r3 0
  #_ REGISTER_IN r4 48
  tdge r3, r4
  blr
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 48

test_tdge_2_constant:
  li r3, 0
  li r4, 48
  tdge r3, r4
  blr
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 48

test_tdge_3:
  #_ REGISTER_IN r3 -1
  #_ REGISTER_IN r4 0
  tdge r3, r4
  blr
  #_ REGISTER_OUT r3 -1
  #_ REGISTER_OUT r4 0

test_tdge_3_constant:
  li r3, -1
  li r4, 0
  tdge r3, r4
  blr
  #_ REGISTER_OUT r3 -1
  #_ REGISTER_OUT r4 0

test_tdgt_1:
  #_ REGISTER_IN r3 24
  #_ REGISTER_IN r4 48
  tdgt r3, r4
  blr
  #_ REGISTER_OUT r3 24
  #_ REGISTER_OUT r4 48

test_tdgt_1_constant:
  li r3, 24
  li r4, 48
  tdgt r3, r4
  blr
  #_ REGISTER_OUT r3 24
  #_ REGISTER_OUT r4 48

test_tdgt_2:
  #_ REGISTER_IN r3 0
  #_ REGISTER_IN r4 48
  tdgt r3, r4
  blr
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 48

test_tdgt_2_constant:
  li r3, 0
  li r4, 48
  tdgt r3, r4
  blr
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 48

test_tdgt_3:
  #_ REGISTER_IN r3 -1
  #_ REGISTER_IN r4 0
  tdgt r3, r4
  blr
  #_ REGISTER_OUT r3 -1
  #_ REGISTER_OUT r4 0

test_tdgt_3_constant:
  li r3, -1
  li r4, 0
  tdgt r3, r4
  blr
  #_ REGISTER_OUT r3 -1
  #_ REGISTER_OUT r4 0

test_tdnl_1:
  #_ REGISTER_IN r3 24
  #_ REGISTER_IN r4 48
  tdnl r3, r4
  blr
  #_ REGISTER_OUT r3 24
  #_ REGISTER_OUT r4 48

test_tdnl_1_constant:
  li r3, 24
  li r4, 48
  tdnl r3, r4
  blr
  #_ REGISTER_OUT r3 24
  #_ REGISTER_OUT r4 48

test_tdnl_2:
  #_ REGISTER_IN r3 0
  #_ REGISTER_IN r4 48
  tdnl r3, r4
  blr
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 48

test_tdnl_2_constant:
  li r3, 0
  li r4, 48
  tdnl r3, r4
  blr
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 48

test_tdnl_3:
  #_ REGISTER_IN r3 -1
  #_ REGISTER_IN r4 0
  tdnl r3, r4
  blr
  #_ REGISTER_OUT r3 -1
  #_ REGISTER_OUT r4 0

test_tdnl_3_constant:
  li r3, -1
  li r4, 0
  tdnl r3, r4
  blr
  #_ REGISTER_OUT r3 -1
  #_ REGISTER_OUT r4 0

test_tdne_1:
  #_ REGISTER_IN r3 24
  #_ REGISTER_IN r4 24
  tdne r3, r4
  blr
  #_ REGISTER_OUT r3 24
  #_ REGISTER_OUT r4 24

test_tdne_1_constant:
  li r3, 24
  li r4, 24
  tdne r3, r4
  blr
  #_ REGISTER_OUT r3 24
  #_ REGISTER_OUT r4 24

test_tdne_2:
  #_ REGISTER_IN r3 0
  #_ REGISTER_IN r4 0
  tdne r3, r4
  blr
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 0

test_tdne_2_constant:
  li r3, 0
  li r4, 0
  tdne r3, r4
  blr
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 0

test_tdng_1:
  #_ REGISTER_IN r3 24
  #_ REGISTER_IN r4 16
  tdng r3, r4
  blr
  #_ REGISTER_OUT r3 24
  #_ REGISTER_OUT r4 16

test_tdng_1_constant:
  li r3, 24
  li r4, 16
  tdng r3, r4
  blr
  #_ REGISTER_OUT r3 24
  #_ REGISTER_OUT r4 16

test_tdng_2:
  #_ REGISTER_IN r3 24
  #_ REGISTER_IN r4 0
  tdng r3, r4
  blr
  #_ REGISTER_OUT r3 24
  #_ REGISTER_OUT r4 0

test_tdng_2_constant:
  li r3, 24
  li r4, 0
  tdng r3, r4
  blr
  #_ REGISTER_OUT r3 24
  #_ REGISTER_OUT r4 0

test_tdng_3:
  #_ REGISTER_IN r3 0
  #_ REGISTER_IN r4 -1
  tdng r3, r4
  blr
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 -1

test_tdng_3_constant:
  li r3, 0
  li r4, -1
  tdng r3, r4
  blr
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 -1

test_tdllt_1:
  #_ REGISTER_IN r3 24
  #_ REGISTER_IN r4 16
  tdllt r3, r4
  blr
  #_ REGISTER_OUT r3 24
  #_ REGISTER_OUT r4 16

test_tdllt_1_constant:
  li r3, 24
  li r4, 16
  tdllt r3, r4
  blr
  #_ REGISTER_OUT r3 24
  #_ REGISTER_OUT r4 16

test_tdllt_2:
  #_ REGISTER_IN r3 24
  #_ REGISTER_IN r4 0
  tdllt r3, r4
  blr
  #_ REGISTER_OUT r3 24
  #_ REGISTER_OUT r4 0

test_tdllt_2_constant:
  li r3, 24
  li r4, 0
  tdllt r3, r4
  blr
  #_ REGISTER_OUT r3 24
  #_ REGISTER_OUT r4 0

test_tdlle_1:
  #_ REGISTER_IN r3 24
  #_ REGISTER_IN r4 16
  tdlle r3, r4
  blr
  #_ REGISTER_OUT r3 24
  #_ REGISTER_OUT r4 16

test_tdlle_1_constant:
  li r3, 24
  li r4, 16
  tdlle r3, r4
  blr
  #_ REGISTER_OUT r3 24
  #_ REGISTER_OUT r4 16

test_tdlle_2:
  #_ REGISTER_IN r3 24
  #_ REGISTER_IN r4 0
  tdlle r3, r4
  blr
  #_ REGISTER_OUT r3 24
  #_ REGISTER_OUT r4 0

test_tdlle_2_constant:
  li r3, 24
  li r4, 0
  tdlle r3, r4
  blr
  #_ REGISTER_OUT r3 24
  #_ REGISTER_OUT r4 0

test_tdlge_1:
  #_ REGISTER_IN r3 24
  #_ REGISTER_IN r4 48
  tdlge r3, r4
  blr
  #_ REGISTER_OUT r3 24
  #_ REGISTER_OUT r4 48

test_tdlge_1_constant:
  li r3, 24
  li r4, 48
  tdlge r3, r4
  blr
  #_ REGISTER_OUT r3 24
  #_ REGISTER_OUT r4 48

test_tdlge_2:
  #_ REGISTER_IN r3 0
  #_ REGISTER_IN r4 48
  tdlge r3, r4
  blr
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 48

test_tdlge_2_constant:
  li r3, 0
  li r4, 48
  tdlge r3, r4
  blr
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 48

test_tdlgt_1:
  #_ REGISTER_IN r3 24
  #_ REGISTER_IN r4 48
  tdlgt r3, r4
  blr
  #_ REGISTER_OUT r3 24
  #_ REGISTER_OUT r4 48

test_tdlgt_1_constant:
  li r3, 24
  li r4, 48
  tdlgt r3, r4
  blr
  #_ REGISTER_OUT r3 24
  #_ REGISTER_OUT r4 48

test_tdlgt_2:
  #_ REGISTER_IN r3 0
  #_ REGISTER_IN r4 48
  tdlgt r3, r4
  blr
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 48

test_tdlgt_2_constant:
  li r3, 0
  li r4, 48
  tdlgt r3, r4
  blr
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 48

test_tdlnl_1:
  #_ REGISTER_IN r3 24
  #_ REGISTER_IN r4 48
  tdlnl r3, r4
  blr
  #_ REGISTER_OUT r3 24
  #_ REGISTER_OUT r4 48

test_tdlnl_1_constant:
  li r3, 24
  li r4, 48
  tdlnl r3, r4
  blr
  #_ REGISTER_OUT r3 24
  #_ REGISTER_OUT r4 48

test_tdlnl_2:
  #_ REGISTER_IN r3 0
  #_ REGISTER_IN r4 48
  tdlnl r3, r4
  blr
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 48

test_tdlnl_2_constant:
  li r3, 0
  li r4, 48
  tdlnl r3, r4
  blr
  #_ REGISTER_OUT r3 0
  #_ REGISTER_OUT r4 48

test_tdlng_1:
  #_ REGISTER_IN r3 24
  #_ REGISTER_IN r4 16
  tdlng r3, r4
  blr
  #_ REGISTER_OUT r3 24
  #_ REGISTER_OUT r4 16

test_tdlng_1_constant:
  li r3, 24
  li r4, 16
  tdlng r3, r4
  blr
  #_ REGISTER_OUT r3 24
  #_ REGISTER_OUT r4 16

test_tdlng_2:
  #_ REGISTER_IN r3 24
  #_ REGISTER_IN r4 0
  tdlng r3, r4
  blr
  #_ REGISTER_OUT r3 24
  #_ REGISTER_OUT r4 0

test_tdlng_2_constant:
  li r3, 24
  li r4, 0
  tdlng r3, r4
  blr
  #_ REGISTER_OUT r3 24
  #_ REGISTER_OUT r4 0
