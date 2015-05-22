test_vmaddfp_1:
  #_ REGISTER_IN v4 [3f800000, 3fc00000, 3f8ccccd, 3ff33333]
  # 1.0, 1.5, 1.1, 1.9
  vmaddfp v3, v4, v4, v4
  blr
  #_ REGISTER_OUT v3 [00000001, 00000001, 00000001, 00000001]
  #_ REGISTER_OUT v4 [40000000, 40700000, 4013d70a, 40b051eb]
  # 2.0, 3.75, 2.31, 5.51
  # 40b051eb is actually 5.50999975, not 5.51?
  # 40b051ec is 5.51
