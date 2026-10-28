# N136: force mgmode's exit save. The backup differs from GWGameStat, so fn_1_54F4's memcmp is true (GWGameStat itself
# is saved unchanged). Stop before the game exits (gdb 18.1 aborts on the inferior's exit).
set breakpoint pending on
set pagination off
break mgmode.c:1186
commands
  set var ((unsigned char *)&mgGameStatBackup)[0] ^= 1
  printf "N136: backup changed\n"
  continue
end
break SLSaveQuiet
commands
  bt 3
  continue
end
break SLSave
commands
  bt 3
  continue
end
break exit
commands
  kill
  quit
end
run
