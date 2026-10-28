# Live master volume: 100 at boot (config.json), then 50, 0, 100 at fixed game frames (~60/s; the title music
# plays from ~13 s to its own fade at ~30 s, so every step sits inside it); kill before exit (gdb 18.1 assert)
set pagination off
set breakpoint pending on
break partyboard_audio_frame
ignore 1 900
commands 1
  silent
  printf "N124 frame 900: volume 50\n"
  call partyboard_audio_set_master_volume(50)
  disable 1
  enable 2
  continue
end
break partyboard_audio_frame
disable 2
ignore 2 180
commands 2
  silent
  printf "N124 frame 1080: volume 0\n"
  call partyboard_audio_set_master_volume(0)
  disable 2
  enable 3
  continue
end
break partyboard_audio_frame
disable 3
ignore 3 180
commands 3
  silent
  printf "N124 frame 1260: volume 100\n"
  call partyboard_audio_set_master_volume(100)
  disable 3
  enable 4
  continue
end
break partyboard_audio_frame
disable 4
ignore 4 360
commands 4
  silent
  printf "N124 frame 1620: kill\n"
  kill
  quit
end
run
