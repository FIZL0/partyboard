set pagination off
set breakpoint pending on
handle SIGPIPE nostop noprint
break omOvlCallEx if overlay == 95
run
printf "N115 at omOvlCallEx(95): board %d turn %d max_turn %d; characters -> 3 0 2 4, diff/group 0 (mentDll fn_1_88A4)\n", GWSystem.board, GWSystem.turn, GWSystem.max_turn
set var GWPlayerCfg[0].character = 3
set var GWPlayerCfg[1].character = 0
set var GWPlayerCfg[2].character = 2
set var GWPlayerCfg[3].character = 4
set var GWPlayerCfg[0].diff = 0
set var GWPlayerCfg[1].diff = 0
set var GWPlayerCfg[2].diff = 0
set var GWPlayerCfg[3].diff = 0
set var GWPlayerCfg[0].group = 0
set var GWPlayerCfg[1].group = 0
set var GWPlayerCfg[2].group = 0
set var GWPlayerCfg[3].group = 0
delete
break BoardLast5Exec
commands
printf "N115 BoardLast5Exec: turn %d max_turn %d\n", GWSystem.turn, GWSystem.max_turn
continue
end
break BoardTurnNext
commands
printf "N115 BoardTurnNext: turn %d max_turn %d\n", GWSystem.turn, GWSystem.max_turn
continue
end
break omOvlReturnEx
commands
printf "N115 omOvlReturnEx: board %d turn %d max_turn %d\n", GWSystem.board, GWSystem.turn, GWSystem.max_turn
continue
end
continue
