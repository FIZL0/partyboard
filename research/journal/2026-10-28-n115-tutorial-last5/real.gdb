set $tut = 0
set pagination off
set breakpoint pending on
handle SIGPIPE nostop noprint
break omOvlCallEx
commands
printf "N115 omOvlCallEx(%d, event %d): board %d turn %d max_turn %d flag1000B %d chars %d %d %d %d\n", overlay, event, GWSystem.board, GWSystem.turn, GWSystem.max_turn, _CheckFlag(0x1000B), GWPlayerCfg[0].character, GWPlayerCfg[1].character, GWPlayerCfg[2].character, GWPlayerCfg[3].character
continue
end
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
break fn_1_88A4
commands
printf "N115 mentDll fn_1_88A4 (the tutorial question)\n"
set $tut = 1
continue
end
break fn_1_C174
commands
printf "N115 mentDll fn_1_C174(%d)\n", arg0
bt 6
continue
end
break fn_1_1DD8
commands
printf "N115 mentDll fn_1_1DD8(0x%x, %d, %d)\n", arg0, arg1, arg2
continue
end
break HuWinChoiceGet if $tut == 1
commands
printf "N115 HuWinChoiceGet(window %d, default %d) -> 0 (the player moves the cursor to the first answer)\n", window, start_choice
set var start_choice = 0
set $tut = 2
continue
end
run
bt 15
