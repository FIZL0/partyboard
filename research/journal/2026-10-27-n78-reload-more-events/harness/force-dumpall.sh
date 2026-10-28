#!/bin/sh
# FORCE file for reload.sh: dump the board statics at every turn point to $1 as tp1, tp2, ... (the fresh baseline:
# tp1 follows the board intro, tp2.. are turn points like a resumed one, with no intro before them).
cat <<G
set \$tp = 0
break netplay_turn_checkpoint
commands
  silent
  set \$tp = \$tp + 1
  printf "GDB: turn point %d at frame %u, turn %d, player %d\n", \$tp, netplay_frame(), GWSystem.turn, GWSystem.player_curr
  eval "pbdump $1 tp%d", \$tp
  continue
end
G
