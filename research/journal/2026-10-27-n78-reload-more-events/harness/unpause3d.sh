#!/bin/sh
# FORCE file for reload.sh: at frame $1 (after a reload) clear Hu3DPauseF, which a reload in the board pause leaves
# set (the old board's system key object, which would lift it, is gone): shows the stall is that flag alone.
cat <<G
set \$unp = 0
break netplay_match_tick if \$unp == 0 && netplay_frame() >= $1
commands
  silent
  set \$unp = 1
  printf "GDB: Hu3DPauseF %d -> 0 at frame %u\n", Hu3DPauseF, netplay_frame()
  set var Hu3DPauseF = 0
  continue
end
G
