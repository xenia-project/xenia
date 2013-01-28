# This is a variant of rlwinmx:
# extrwi ra,rs,n,b (n > 0) == rlwinm ra,rs,b+n,32-n,31

# REGISTER_IN r5 0x30

# rlwinm r7, r5, 29, 28, 31
extrwi r7, r5, 4, 25

blr
# REGISTER_OUT r5 0x30
# REGISTER_OUT r7 0x06
