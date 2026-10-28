set pagination off
set breakpoint pending on
handle SIGPIPE nostop noprint
break omOvlCallEx if overlay == 95
run
printf "N115 at omOvlCallEx(95): board %d turn %d max_turn %d -> 0 0 0 (the shortcut never called BoardSaveInit)\n", GWSystem.board, GWSystem.turn, GWSystem.max_turn
set var GWSystem.board = 0
set var GWSystem.turn = 0
set var GWSystem.max_turn = 0
delete
continue
bt 15
