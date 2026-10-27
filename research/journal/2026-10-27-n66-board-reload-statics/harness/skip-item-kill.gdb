# Undo the ItemRotProc kill: after the reload, report what the left process does.
set $rot = 0
break BoardItemNetplayKill
commands
  silent
  printf "GDB: skipping BoardItemNetplayKill\n"
  return 0
  continue
end
break item.c:268 if $done == 1
commands
  silent
  set $rot = $rot + 1
  if $rot == 1 || $rot % 600 == 0
    printf "GDB: ItemRotProc after the reload, frame %u, sets model %d (%d times), process %p\n", netplay_frame(), temp_r31->unk02, $rot, HuPrcCurrentGet()
  end
  continue
end
