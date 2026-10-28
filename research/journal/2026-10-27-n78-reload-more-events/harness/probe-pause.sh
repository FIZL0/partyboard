#!/bin/sh
# FORCE file for reload.sh: at frame $1 print the global pauses (Hu3DPauseF, HuSprPauseF, the board's system key
# object's work[0]) and every process with PROCESS_STAT_PAUSE, to see what a reload left paused.
cat <<G
set \$probed = 0
break netplay_match_tick if \$probed == 0 && netplay_frame() >= $1
commands
  silent
  set \$probed = 1
  printf "GDB: probe at frame %u: Hu3DPauseF %d HuSprPauseF %d syskey work0 0x%x\n", netplay_frame(), Hu3DPauseF, HuSprPauseF, omDBGSysKeyObj ? omDBGSysKeyObj->work[0] : -1
  set \$p = processtop
  set \$n = 0
  set \$np = 0
  while \$p
    set \$n = \$n + 1
    if \$p->stat & 1
      set \$np = \$np + 1
      info symbol \$p->func
    end
    set \$p = \$p->next
  end
  printf "GDB: %d processes, %d paused\n", \$n, \$np
  continue
end
G
