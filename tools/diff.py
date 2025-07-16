#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""python diff.py file1 file2 diff
"""

from difflib import unified_diff
from sys import argv

diff = unified_diff(
    open(argv[1], encoding="utf-8").readlines(),
    open(argv[2], encoding="utf-8").readlines())
with open(argv[3], "w", encoding="utf-8") as f:
    f.write("".join(diff))
