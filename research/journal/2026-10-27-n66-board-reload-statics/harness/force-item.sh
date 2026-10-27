#!/bin/sh
# FORCE file for reload.sh: player 0 becomes a COM with item $1 at its first turn, and its COM picks that item (BoardComUseItemCheck answers yes with itemUse set).
cat <<G
set \$itemset = 0
break BoardPlayerTurnExec if \$itemset == 0 && arg0 == 0
commands
  silent
  set \$itemset = 1
  set var GWPlayer[0].com = 1
  set var GWPlayer[0].items[0] = $1
  printf "GDB: player 0 is a COM with item $1 at frame %u\n", netplay_frame()
  continue
end
break BoardComUseItemCheck if \$itemset == 1 && player == 0
commands
  silent
  set \$itemset = 2
  set var 'com.c'::itemUse = $1
  return 1
  continue
end
G
