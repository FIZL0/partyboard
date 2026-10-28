#!/bin/sh
# FORCE file for reload.sh: the first board pause request from frame $1 on, while the board takes one, answers yes
# for pad 0 (START taps in scripted runs never pause: N34/N76), so BoardPauseStart opens the pause screen.
cat <<G
set \$pauseset = 0
break BoardPauseReqCheck if \$pauseset == 0 && netplay_frame() >= $1 && 'pause.c'::mainProcess == 0 && BoardPauseDisableGet() == 0
commands
  silent
  set \$pauseset = 1
  set var 'pause.c'::pausePad = 0
  printf "GDB: pause request forced at frame %u\n", netplay_frame()
  return 1
  continue
end
G
