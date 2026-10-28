# N136 (with mgmode-exit.script): leave Mini-Game mode at script frame 2500 by setting the menu's quit flag
# (lbl_1_bss_2D0, what B + Yes sets), and make mgmode's GWGameStat backup differ so fn_1_54F4 saves.
# The run has no card file (skipBootSequence), so saves are turned on there too: SLSave takes its create-file path.
set breakpoint pending on
set pagination off
break script_pads if frame == 2500
commands
  set var lbl_1_bss_2D0 = 1
  printf "N136: quit flag set\n"
  continue
end
break mgmode.c:1186
commands
  set var ((unsigned char *)&mgGameStatBackup)[0] ^= 1
  set var SaveEnableF = 1
  printf "N136: backup changed, saves on\n"
  continue
end
break SLSaveQuiet
commands
  bt 4
  continue
end
break SLSave
commands
  bt 4
  continue
end
break exit
commands
  kill
  quit
end
run
