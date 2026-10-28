#!/bin/sh
# FORCE file for reload.sh: all four players are COMs from the board intro on, so minigames play themselves and
# instDll leaves on its own (comNum == 4); mash-a only taps START there at frames tuned for the turn-1 minigame, and
# a battle or Bowser minigame comes much earlier (the battle's then waited until 10607, the Bowser one never ended).
cat <<G
set \$allcom = 0
break BoardStartExec if \$allcom == 0
commands
  silent
  set \$allcom = 1
  set \$i = 0
  while \$i < 4
    set var GWPlayerCfg[\$i].iscom = 1
    set var GWPlayer[\$i].com = 1
    set \$i = \$i + 1
  end
  printf "GDB: all players are COMs at frame %u\n", netplay_frame()
  continue
end
G
