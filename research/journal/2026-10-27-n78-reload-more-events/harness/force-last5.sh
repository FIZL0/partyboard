#!/bin/sh
# FORCE file for reload.sh: the match has $1 turns from the board intro on: 5 runs the last-5-turns event in turn 1
# (before any turn checkpoint, so start mode only), 6 in turn 2 after the first minigame
# (main.c MainFunc: CHECK_LAST5_TURN after BoardStartExec).
cat <<G
set \$last5set = 0
break BoardStartExec if \$last5set == 0
commands
  silent
  set \$last5set = 1
  set var GWSystem.max_turn = $1
  printf "GDB: max_turn $1 at frame %u\n", netplay_frame()
  continue
end
G
