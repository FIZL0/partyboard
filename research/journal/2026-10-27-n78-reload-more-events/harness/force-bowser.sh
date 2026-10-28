#!/bin/sh
# FORCE file for reload.sh: the first Bowser space event becomes Bowser's own (not Koopa Kid's) with event 0, the
# Bowser minigame (ExecBowserGame), so the board loads instdll and comes back with flag (1,3) (ExecMGReturn).
cat <<G
set \$bowserset = 0
break CreateMiniBowser if \$bowserset == 0
commands
  silent
  set \$bowserset = 1
  set var 'bowser.c'::eventType = 0
  set var GWSystem.bowser_event = 0
  printf "GDB: Bowser event forced to the minigame at frame %u\n", netplay_frame()
  continue
end
G
# $1 (optional): the Bowser minigame (435..437) instead of ExecBowserGame's random one. With 4 COMs (force-allcom.sh)
# m435 never ended (a message window in it waited on for good: 11000+ frames).
[ -n "$1" ] && cat <<G
break bowser.c:460
commands
  silent
  set var GWSystem.mg_next = $1 - 401
  printf "GDB: Bowser minigame $1 at frame %u\n", netplay_frame()
  continue
end
G
