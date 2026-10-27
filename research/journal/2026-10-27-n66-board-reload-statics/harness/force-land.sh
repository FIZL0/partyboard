#!/bin/sh
# FORCE file for reload.sh: the first space player $2 lands on (BoardSpaceLandExec) becomes type $1.
cat <<G
set \$landset = 0
break BoardSpaceLandExec if \$landset == 0 && player == $2
commands
  silent
  set \$landset = 1
  set var BoardSpaceGet(0, space)->type = $1
  printf "GDB: space %d is type $1 now, frame %u\n", space, netplay_frame()
  continue
end
G
