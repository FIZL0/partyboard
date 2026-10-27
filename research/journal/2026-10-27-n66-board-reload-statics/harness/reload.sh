#!/bin/sh
# One process, mash-a + --match 0@900 under gdb. EVENT (a board function) hit -> netplay_match_start (or, with
# MODE=cont, a continue from the last turn blob) DELAY frames later. Every board start (after N38's scrub) and the
# first turn point after it dump the board statics to $OUT/dump.txt. EVENT=none: no reload (the fresh baseline).
# FORCE=<file>: more gdb commands (steer the run to an event). DUMPLINE: netplay_match.c line after the scrub.
# Dumps: preN just before N66's reset (BoardRollNetplayReset), startN after the scrub, turnN at the first turn point.
# usage: reload.sh <outdir> <EVENT> <DELAY> [MODE]
set -e
H=$(cd "$(dirname "$0")" && pwd)
R=$(cd "$H/../../../.." && pwd)
B=$R/build/linux-default-relwithdebinfo
OUT=$1 EVENT=$2 DELAY=$3 MODE=${4:-start}
mkdir -p "$OUT/home"
rm -f "$OUT/dump.txt"
if [ "$MODE" = cont ]; then
  RELOAD='set $buf = (unsigned char *)malloc(65536)
  set $sz = (unsigned)netplay_checkpoint_last($buf, 65536)
  printf "GDB: turn blob %u bytes\n", $sz
  call (int)netplay_match_continue(&'"'"'netplay_match.c'"'"'::match.m, $buf, $sz)'
else
  RELOAD="call (int)netplay_match_start(&'netplay_match.c'::match.m)"
fi
cat > "$OUT/run.gdb" <<G
set pagination off
set confirm off
set disable-randomization off
handle SIGUSR1 SIGUSR2 SIGPIPE nostop noprint
set breakpoint pending on
source $H/dump.py
set \$t = 0
set \$done = 0
set \$epoch = 0
set \$turn = 0
G
if [ "$EVENT" != none ]; then cat >> "$OUT/run.gdb" <<G
break $EVENT
commands
  silent
  if \$t == 0
    set \$t = netplay_frame() + $DELAY
    printf "GDB: $EVENT hit, frame %u, player %d; reload at %u\n", netplay_frame(), GWSystem.player_curr, \$t
  end
  continue
end
break netplay_match_tick if \$t != 0 && \$done == 0 && netplay_frame() >= \$t
commands
  silent
  set \$done = 1
  printf "GDB: reload at frame %u, ovl %d, player %d\n", netplay_frame(), omcurovl, GWSystem.player_curr
  $RELOAD
  continue
end
G
fi
cat >> "$OUT/run.gdb" <<G
break BoardRollNetplayReset
commands
  silent
  eval "pbdump $OUT/dump.txt pre%d", \$epoch + 1
  continue
end
break netplay_match.c:${DUMPLINE:-727}
commands
  silent
  set \$epoch = \$epoch + 1
  set \$turn = 1
  printf "GDB: board start %d at frame %u\n", \$epoch, netplay_frame()
  eval "pbdump $OUT/dump.txt start%d", \$epoch
  continue
end
break netplay_turn_checkpoint if \$turn == 1
commands
  silent
  set \$turn = 0
  printf "GDB: first turn point of epoch %d at frame %u, player %d\n", \$epoch, netplay_frame(), GWSystem.player_curr
  eval "pbdump $OUT/dump.txt turn%d", \$epoch
  continue
end
G
[ -n "$FORCE" ] && cat "$FORCE" >> "$OUT/run.gdb"
cat >> "$OUT/run.gdb" <<G
run
printf "GDB: stopped\n"
bt 12
quit
G
cd "$B"
PB_SYMS=$H/../syms.txt HOME=$OUT/home XDG_DATA_HOME=$OUT/home/.local/share timeout -s KILL ${TMO:-400} \
  gdb -q -batch -x "$OUT/run.gdb" --args ./partyboard --script "$R/tools/netplay/mash-a.script" --match 0@900 --turbo \
  > "$OUT/log.txt" 2>&1 || true
grep -E "^GDB|netplay: (match|board)|SIGSEGV|SIGABRT" "$OUT/log.txt" | head -60
