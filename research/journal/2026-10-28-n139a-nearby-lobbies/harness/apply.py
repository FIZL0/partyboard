#!/usr/bin/env python3
"""N139a copy of N139's (journal 2026-10-28-n139-one-code-join), itself an N139 copy of N96's apply.py. N96 scratch: puts the harness into the tree (run from the repo root); `git checkout src/port/netplay.c
src/port/ui/online.cpp` takes it out again. N38's pad masher (PB_N96) and n96_harness.inc."""
import os, sys
here = os.path.dirname(os.path.abspath(__file__))
p = 'src/port/netplay.c'
s = open(p).read()
pads = '''/* N96 scratch (N38's): this machine's seat mashes A (START in instDll and the party results). */
static void n96_pads(PADStatus *local, u32 at)
{
    static int last_ovl = -1;
    static u32 since;
    if (getenv("PB_N96") == NULL) {
        return;
    }
    if ((int)omcurovl != last_ovl) {
        last_ovl = (int)omcurovl;
        since = at;
    }
    memset(local, 0, 4 * sizeof(*local));
    if (at % 24 < 4 && !(omcurovl == 3 && at - since < 120)) {
        local[0].button = (omcurovl == 3 || omcurovl == 78) ? PAD_BUTTON_START : PAD_BUTTON_A;
    }
}

BOOL netplay_tick_begin(void)
{'''
s = s.replace('BOOL netplay_tick_begin(void)\n{', pads, 1)
old = '    /* pbnet takes the local pads at the first call for a frame only. */\n    if (!step_taken_set'
assert old in s
s = s.replace(old, '    n96_pads(local, next);\n' + old, 1)
open(p, 'w').write(s)
p = 'src/port/ui/online.cpp'
s = open(p).read()
old = 'void update_online_match()\n{\n'
assert old in s
s = s.replace(old, old + '    n96_harness();\n', 1)
# after pbnet_text/trim exist: before the namespace's update_online_match, inside partyboard::ui
s = s.replace('#include <memory>\n', '#include <memory>\n#include <cstdio>\n#include <fstream>\n#include <iterator>\n', 1)
s = s.replace('void update_online_match()\n{\n', '#include "%s/n96_harness.inc"\nvoid update_online_match()\n{\n' % here, 1)
open(p, 'w').write(s)
