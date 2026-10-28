#!/bin/sh
# One process, mash-a + --match 0@900 --turbo --hash-log under gdb (N76).
# PAUSE=<frame>: from that frame on, pause the way the game does:
#   PKIND=mg    a START press for omSystemKeyCheck (a minigame's START pause: MGSeqPauseInit's PauseProc)
#   PKIND=board BoardPauseReqCheck says yes once (the board's pause menu: BoardPauseStart -> omSysPauseCtrl(1))
# RELOAD=<frame>: netplay_match_start there (a resync's reload). LEAVE=<frame>: netplay_match_leave there (N34). Every overlay start and the first board frames
# print the pause state (Hu3DPauseF, HuSprPauseF, paused processes, MGSeq's pause process).
# usage: run.sh <outdir>
set -e
H=$(cd "$(dirname "$0")" && pwd)
R=$(cd "$H/../../../.." && pwd)
B=$R/build/linux-default-relwithdebinfo
OUT=$1
mkdir -p "$OUT/home"
cat > "$OUT/run.gdb" <<G
set pagination off
set confirm off
set disable-randomization off
handle SIGUSR1 SIGUSR2 SIGPIPE nostop noprint
set breakpoint pending on
set \$paused = 0
set \$reloaded = 0
set \$watch = 0
define pbstate
  set \$n = 0
  set \$m = 0
  set \$p = processtop
  while \$p != 0
    if (\$p->stat & 3) != 0
      set \$n = \$n + 1
    end
    if \$p->func == 'minigame_seq.c'::PauseProc
      set \$m = \$m + 1
    end
    set \$p = \$p->next
  end
  printf "GDB: frame %u ovl %d: Hu3DPauseF %d HuSprPauseF %d paused-procs %d mgPauseProcs %d\n", netplay_frame(), omcurovl, Hu3DPauseF, 'sprman.c'::HuSprPauseF, \$n, \$m
end
G
if [ -n "$PAUSE" ]; then
  if [ "${PKIND:-mg}" = mg ]; then cat >> "$OUT/run.gdb" <<G
break omSystemKeyCheck if \$paused == 0 && netplay_frame() >= $PAUSE && omSysPauseEnableFlag && !(object->work[0] & 1)
commands
  silent
  set HuPadBtnDown[0] = HuPadBtnDown[0] | 0x1000
  set \$paused = 1
  printf "GDB: START pressed at frame %u, ovl %d\n", netplay_frame(), omcurovl
  continue
end
G
  else cat >> "$OUT/run.gdb" <<G
break BoardPauseReqCheck if \$paused == 0 && netplay_frame() >= $PAUSE
commands
  silent
  set \$paused = 1
  set 'pause.c'::pausePad = 0
  printf "GDB: board pause at frame %u\n", netplay_frame()
  return 1
  continue
end
G
  fi
fi
if [ -n "$RELOAD" ]; then cat >> "$OUT/run.gdb" <<G
break netplay_match_tick if \$reloaded == 0 && netplay_frame() >= $RELOAD
commands
  silent
  set \$reloaded = 1
  pbstate
  printf "GDB: reload at frame %u, ovl %d\n", netplay_frame(), omcurovl
  call (int)netplay_match_start(&'netplay_match.c'::match.m)
  set \$watch = 1
  continue
end
G
fi
if [ -n "$LEAVE" ]; then cat >> "$OUT/run.gdb" <<G
break netplay_match_tick if \$reloaded == 0 && netplay_frame() >= $LEAVE
commands
  silent
  set \$reloaded = 1
  pbstate
  printf "GDB: leave at frame %u, ovl %d\n", netplay_frame(), omcurovl
  call (void)netplay_match_leave()
  set \$watch = 1
  continue
end
G
fi
cat >> "$OUT/run.gdb" <<G
break omDLLStart
commands
  silent
  pbstate
  continue
end
break netplay_match_tick if \$watch > 0 && \$watch < 400
commands
  silent
  set \$watch = \$watch + 1
  if \$watch == 60 || \$watch == 399
    pbstate
  end
  continue
end
run
printf "GDB: stopped\n"
bt 12
quit
G
cd "$B"
HOME=$OUT/home XDG_DATA_HOME=$OUT/home/.local/share timeout -s KILL ${TMO:-300} \
  gdb -q -batch -x "$OUT/run.gdb" --args ./partyboard --script "$R/tools/netplay/mash-a.script" --match 0@900 --turbo \
  --hash-log "$OUT/hash.log" > "$OUT/log.txt" 2>&1 || true
grep -E "^GDB|netplay: (match|board)|SIGSEGV|SIGABRT" "$OUT/log.txt" | head -60
