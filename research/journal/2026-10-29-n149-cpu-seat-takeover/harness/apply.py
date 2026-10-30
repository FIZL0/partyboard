#!/usr/bin/env python3
"""N149 scratch (N148's apply.py): run from the repo root. Saves netplay.c and online.cpp to .n149-runs/orig/, then
puts an A masher for every local pad (PB_N149) into netplay.c and n149_harness.inc into online.cpp. remove.sh puts
the saved copies back."""
import os, shutil
here = os.path.dirname(os.path.abspath(__file__))
os.makedirs('.n149-runs/orig', exist_ok=True)
for p in ['src/port/netplay.c', 'src/port/ui/online.cpp']:
    shutil.copy(p, '.n149-runs/orig/' + os.path.basename(p))
p = 'src/port/netplay.c'
s = open(p).read()
pads = '''/* N149 scratch (N38's): this machine's seats all mash A (START in instDll and the party results). */
static void n149_pads(PADStatus *local, u32 at)
{
    static int last_ovl = -1;
    static u32 since;
    int k;
    if (getenv("PB_N149") == NULL) {
        return;
    }
    if ((int)omcurovl != last_ovl) {
        last_ovl = (int)omcurovl;
        since = at;
    }
    memset(local, 0, 4 * sizeof(*local));
    if (at % 24 < 4 && !(omcurovl == 3 && at - since < 120)) {
        for (k = 0; k < 4; k++) {
            local[k].button = (omcurovl == 3 || omcurovl == 78) ? PAD_BUTTON_START : PAD_BUTTON_A;
        }
    }
}

BOOL netplay_tick_begin(void)
{'''
assert 'BOOL netplay_tick_begin(void)\n{' in s
s = s.replace('BOOL netplay_tick_begin(void)\n{', pads, 1)
old = '    /* pbnet takes the local pads at the first call for a frame only. */\n    if (!step_taken_set'
assert old in s
s = s.replace(old, '    n149_pads(local, next);\n' + old, 1)
open(p, 'w').write(s)
p = 'src/port/ui/online.cpp'
s = open(p).read()
old = 'void update_online_match()\n{\n'
assert old in s
s = s.replace('#include <memory>\n', '#include <memory>\n#include <cstdio>\n#include <fstream>\n#include <iterator>\n#include <typeinfo>\n#include <aurora/rmlui.hpp>\n', 1)
s = s.replace(old, '#include "%s/n149_harness.inc"\n' % here + old + '    n149_harness();\n', 1)
open(p, 'w').write(s)
