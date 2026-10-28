# N136: a game + RmlUi shot every 100 script frames from 800 into SHOTS_DIR (sed it in: see README).
set breakpoint pending on
set pagination off
break script_pads if frame % 100 == 0 && frame >= 800 && frame <= 3000
commands
  silent
  eval "call (void)aurora_capture_frame_ui(\"SHOTS_DIR/f%05u.png\", 1)", frame
  continue
end
break exit
commands
  kill
  quit
end
run
