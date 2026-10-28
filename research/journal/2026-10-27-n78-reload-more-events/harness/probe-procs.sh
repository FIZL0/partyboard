#!/bin/sh
# FORCE file for reload.sh: at frame $1 print the overlay, Hu3DPauseF and every process (stat, func): where a run
# that stopped moving waits (it showed m435 parked in a HuWinProc message wait).
cat <<G
set \$pp = 0
break netplay_match_tick if \$pp == 0 && netplay_frame() >= $1
commands
  silent
  set \$pp = 1
  printf "GDB: procs at frame %u ovl %d Hu3DPauseF %d\n", netplay_frame(), omcurovl, Hu3DPauseF
  set \$p = processtop
  while \$p
    printf "GDB: stat %x ", \$p->stat
    info symbol \$p->func
    set \$p = \$p->next
  end
  continue
end
G
