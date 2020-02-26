#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""python diff.py file1 file2 diff"""

import difflib
import sys

diff = difflib.unified_diff(
    open(sys.argv[1], encoding='utf-8').readlines(),
    open(sys.argv[2], encoding='utf-8').readlines())
with open(sys.argv[3], 'w', encoding='utf-8') as f:
    f.write(''.join(diff))
