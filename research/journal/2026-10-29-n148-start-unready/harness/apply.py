#!/usr/bin/env python3
"""N148 scratch (from N96's apply.py): run from the repo root. N38's pad masher (PB_N148) in netplay.c and
n148_harness.inc in online.cpp. remove.py takes both out again (online.cpp has N148's real edits: no git checkout)."""
import os
here = os.path.dirname(os.path.abspath(__file__))
p = 'src/port/netplay.c'
s = open(p).read()
pads = '''/* N148 scratch (N38's): this machine's seat mashes A (START in instDll and the party results). */
static void n148_pads(PADStatus *local, u32 at)
{
    static int last_ovl = -1;
    static u32 since;
    if (getenv("PB_N148") == NULL) {
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
assert 'BOOL netplay_tick_begin(void)\n{' in s
s = s.replace('BOOL netplay_tick_begin(void)\n{', pads, 1)
old = '    /* pbnet takes the local pads at the first call for a frame only. */\n    if (!step_taken_set'
assert old in s
s = s.replace(old, '    n148_pads(local, next);\n' + old, 1)
open(p, 'w').write(s)
p = 'src/port/ui/online.cpp'
s = open(p).read()
old = 'void update_online_match()\n{\n'
assert old in s
s = s.replace('#include <memory>\n', '#include <memory>\n#include <cstdio>\n#include <fstream>\n#include <iterator>\n#include <typeinfo>\n#include <aurora/rmlui.hpp>\n', 1)
s = s.replace(old, '#include "%s/n148_harness.inc"\n' % here + old + '    n148_harness();\n', 1)
open(p, 'w').write(s)
