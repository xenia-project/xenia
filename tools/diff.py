import difflib
import sys

diff = difflib.unified_diff(
    open(sys.argv[1]).readlines(),
    open(sys.argv[2]).readlines())
with open(sys.argv[3], 'w') as f:
  f.write(''.join(diff))
