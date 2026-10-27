# Undo the fix for main.c only (filterObj and the other objects stay as the old board left them).
break BoardMainNetplayReset
commands
  silent
  printf "GDB: skipping BoardMainNetplayReset, filterObj %p\n", 'main.c'::filterObj
  return
  continue
end
break BoardFilterFadeInit
commands
  silent
  printf "GDB: BoardFilterFadeInit at frame %u, filterObj %p\n", netplay_frame(), 'main.c'::filterObj
  continue
end
