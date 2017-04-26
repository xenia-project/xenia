# Credits: These tests stolen from https://github.com/dolphin-emu/hwtests
# +0
test_fctiwz_1:
  #_ REGISTER_IN f0 0x0000000000000000
  fctiwz f1, f0
  blr
  #_ REGISTER_OUT f0 0x0000000000000000
  #_ REGISTER_OUT f1 0x0000000000000000

# -0
test_fctiwz_2:
  #_ REGISTER_IN f0 0x8000000000000000
  fctiwz f1, f0
  blr
  #_ REGISTER_OUT f0 0x8000000000000000
  #_ REGISTER_OUT f1 0x0000000000000000

# smallest positive subnormal
test_fctiwz_3:
  #_ REGISTER_IN f0 0x0000000000000001
  fctiwz f1, f0
  blr
  #_ REGISTER_OUT f0 0x0000000000000001
  #_ REGISTER_OUT f1 0x0000000000000000

# largest subnormal
test_fctiwz_4:
  #_ REGISTER_IN f0 0x000fffffffffffff
  fctiwz f1, f0
  blr
  #_ REGISTER_OUT f0 0x000fffffffffffff
  #_ REGISTER_OUT f1 0x0000000000000000

# +1
test_fctiwz_5:
  #_ REGISTER_IN f0 0x3ff0000000000000
  fctiwz f1, f0
  blr
  #_ REGISTER_OUT f0 0x3ff0000000000000
  #_ REGISTER_OUT f1 0x0000000000000001

# -1
test_fctiwz_6:
  #_ REGISTER_IN f0 0xbff0000000000000
  fctiwz f1, f0
  blr
  #_ REGISTER_OUT f0 0xbff0000000000000
  #_ REGISTER_OUT f1 0xffffffffffffffff

# -(2^31)
test_fctiwz_7:
  #_ REGISTER_IN f0 0xc1e0000000000000
  fctiwz f1, f0
  blr
  #_ REGISTER_OUT f0 0xc1e0000000000000
  #_ REGISTER_OUT f1 0xFFFFFFFF80000000

# 2^31 - 1
test_fctiwz_8:
  #_ REGISTER_IN f0 0x41dfffffffc00000
  fctiwz f1, f0
  blr
  #_ REGISTER_OUT f0 0x41dfffffffc00000
  #_ REGISTER_OUT f1 0x000000007fffffff

# +infinity
test_fctiwz_9:
  #_ REGISTER_IN f0 0x7ff0000000000000
  fctiwz f1, f0
  blr
  #_ REGISTER_OUT f0 0x7ff0000000000000
  #_ REGISTER_OUT f1 0x000000007fffffff

# -infinity
test_fctiwz_10:
  #_ REGISTER_IN f0 0xfff0000000000000
  fctiwz f1, f0
  blr
  #_ REGISTER_OUT f0 0xfff0000000000000
  #_ REGISTER_OUT f1 0xFFFFFFFF80000000

# TODO(DrChat): Xenia doesn't handle NaNs yet.
# # QNaN
# test_fctiwz_11:
#   #_ REGISTER_IN f0 0xfff8000000000000
#   fctiwz f1, f0
#   blr
#   #_ REGISTER_OUT f0 0xfff8000000000000
#   #_ REGISTER_OUT f1 0xFFFFFFFF80000000
# 
# # SNaN
# test_fctiwz_12:
#   #_ REGISTER_IN f0 0xfff4000000000000
#   fctiwz f1, f0
#   blr
#   #_ REGISTER_OUT f0 0xfff4000000000000
#   #_ REGISTER_OUT f1 0xFFFFFFFF80000000

# +0
test_fctidz_1:
  #_ REGISTER_IN f0 0x0000000000000000
  fctidz f1, f0
  blr
  #_ REGISTER_OUT f0 0x0000000000000000
  #_ REGISTER_OUT f1 0x0000000000000000

# -0
test_fctidz_2:
  #_ REGISTER_IN f0 0x8000000000000000
  fctidz f1, f0
  blr
  #_ REGISTER_OUT f0 0x8000000000000000
  #_ REGISTER_OUT f1 0x0000000000000000

# smallest positive subnormal
test_fctidz_3:
  #_ REGISTER_IN f0 0x0000000000000001
  fctidz f1, f0
  blr
  #_ REGISTER_OUT f0 0x0000000000000001
  #_ REGISTER_OUT f1 0x0000000000000000

# largest subnormal
test_fctidz_4:
  #_ REGISTER_IN f0 0x000fffffffffffff
  fctidz f1, f0
  blr
  #_ REGISTER_OUT f0 0x000fffffffffffff
  #_ REGISTER_OUT f1 0x0000000000000000

# +1
test_fctidz_5:
  #_ REGISTER_IN f0 0x3ff0000000000000
  fctidz f1, f0
  blr
  #_ REGISTER_OUT f0 0x3ff0000000000000
  #_ REGISTER_OUT f1 0x0000000000000001

# -1
test_fctidz_6:
  #_ REGISTER_IN f0 0xbff0000000000000
  fctidz f1, f0
  blr
  #_ REGISTER_OUT f0 0xbff0000000000000
  #_ REGISTER_OUT f1 0xffffffffffffffff

# -(2^31)
test_fctidz_7:
  #_ REGISTER_IN f0 0xc1e0000000000000
  fctidz f1, f0
  blr
  #_ REGISTER_OUT f0 0xc1e0000000000000
  #_ REGISTER_OUT f1 0xffffffff80000000

# 2^31 - 1
test_fctidz_8:
  #_ REGISTER_IN f0 0x41dfffffffc00000
  fctidz f1, f0
  blr
  #_ REGISTER_OUT f0 0x41dfffffffc00000
  #_ REGISTER_OUT f1 0x000000007fffffff

# +infinity
test_fctidz_9:
  #_ REGISTER_IN f0 0x7ff0000000000000
  fctidz f1, f0
  blr
  #_ REGISTER_OUT f0 0x7ff0000000000000
  #_ REGISTER_OUT f1 0x7fffffffffffffff

# -infinity
test_fctidz_10:
  #_ REGISTER_IN f0 0xfff0000000000000
  fctidz f1, f0
  blr
  #_ REGISTER_OUT f0 0xfff0000000000000
  #_ REGISTER_OUT f1 0x8000000000000000

# TODO(DrChat): Xenia doesn't handle NaNs yet.
# # QNaN
# test_fctidz_11:
#   #_ REGISTER_IN f0 0xfff8000000000000
#   fctidz f1, f0
#   blr
#   #_ REGISTER_OUT f0 0xfff8000000000000
#   #_ REGISTER_OUT f1 0x8000000000000000
# 
# # SNaN
# test_fctidz_12:
#   #_ REGISTER_IN f0 0xfff4000000000000
#   fctidz f1, f0
#   blr
#   #_ REGISTER_OUT f0 0xfff4000000000000
#   #_ REGISTER_OUT f1 0x8000000000000000
