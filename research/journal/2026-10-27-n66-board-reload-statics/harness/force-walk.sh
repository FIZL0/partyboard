#!/bin/sh
# FORCE file for reload.sh: the first space player $2 passes (BoardSpaceWalkExec) from frame $3 on gets flag $1
# (0x08000000 Boo house, 0x10000000 lottery, 0x180000 shop).
cat <<G
set \$walkset = 0
break BoardSpaceWalkExec if \$walkset == 0 && player == $2 && netplay_frame() >= $3
commands
  silent
  set \$walkset = 1
  set var BoardSpaceGet(0, space)->flag |= $1
  printf "GDB: space %d has flag $1 now, frame %u\n", space, netplay_frame()
  continue
end
G
