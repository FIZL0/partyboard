#!/usr/bin/env python3
"""N142 scratch: apply.py hooks n142_test.inc into src/port/ui/ui.cpp (run from the repo root); apply.py --revert takes
it out again. As N89's ui-hook.diff: the .inc's includes go at the top of ui.cpp (outside the namespace)."""
import os, re, sys
here = os.path.dirname(os.path.abspath(__file__))
p = 'src/port/ui/ui.cpp'
s = open(p).read()
TAG = ' // N142-HOOK'
if '--revert' in sys.argv:
    s = ''.join(l for l in s.splitlines(True) if TAG not in l)
    open(p, 'w').write(s)
    sys.exit(0)
assert TAG not in s, 'already applied'
inc = open(os.path.join(here, 'n142_test.inc')).read()
head = []
block = inc[inc.index('#include'):inc.index('namespace {')]
for line in block.splitlines():
    head.append(line + TAG + '\n')
s = ''.join(head) + s
old = 'void update() noexcept\n{\n'
assert old in s
s = s.replace(old, '#include "port/main.h"' + TAG + '\n#include "%s/n142_test.inc"' % here + TAG + '\n' + old +
              '    n88_tick();' + TAG + '\n', 1)
open(p, 'w').write(s)
