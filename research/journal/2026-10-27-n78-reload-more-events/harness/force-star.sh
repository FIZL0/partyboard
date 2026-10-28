#!/bin/sh
# FORCE file for reload.sh: the first space player $1 passes (BoardSpaceWalkExec) becomes the star's space
# (boardSpaceStarTbl[star_pos]; the board's space init builds the table again at every board start) and the player
# has 100 coins, so BoardStarExec runs there and the star is bought (mash-a never passed a star on w02..w06).
cat <<G
set \$starset = 0
break BoardSpaceWalkExec if \$starset == 0 && player == $1
commands
  silent
  set \$starset = 1
  set var boardSpaceStarTbl[GWSystem.star_pos] = space
  set var GWPlayer[$1].coins = 100
  printf "GDB: space %d is the star's now, frame %u\n", space, netplay_frame()
  continue
end
G
