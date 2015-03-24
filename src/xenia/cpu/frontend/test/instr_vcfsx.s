test_vcfsx_1:
  #_ REGISTER_IN v3 [3f800000, 3fc00000, 3f8ccccd, 3ff33333]
  # 1.0, 1.5, 1.1, 1.9
  vcfsx v3, v3, 0
  blr
  #_ REGISTER_OUT v3 [4e7e0000, 4e7f0000, 4e7e3333, 4e7fcccd]

test_vcfsx_2:
  #_ REGISTER_IN v3 [3f800000, 3fc00000, 3f8ccccd, 3ff33333]
  # 1.0, 1.5, 1.1, 1.9
  vcfsx v3, v3, 1
  blr
  #_ REGISTER_OUT v3 [4dfe0000, 4dff0000, 4dfe3333, 4dffcccd]

test_vcfsx_3:
  #_ REGISTER_IN v3 [3f800000, 3fc00000, 3f8ccccd, 3ff33333]
  # 1.0, 1.5, 1.1, 1.9
  vcfsx v3, v3, 10
  blr
  #_ REGISTER_OUT v3 [497e0000, 497f0000, 497e3333, 497fcccd]
