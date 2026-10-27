set pagination off
set confirm off
set disable-randomization off
handle SIGUSR1 SIGUSR2 SIGPIPE nostop noprint
set breakpoint pending on
set $t = 0
set $done = 0
break ExecShop
commands
  silent
  if $t == 0
    set $t = netplay_frame() + 120
    printf "GDB: ExecShop hit, frame %u, player %d; reload at %u\n", netplay_frame(), shopPlayer, $t
  end
  continue
end
break netplay_match_tick if $t != 0 && $done == 0 && netplay_frame() >= $t
commands
  silent
  set $done = 1
  printf "GDB: reload at frame %u, shopProc %p\n", netplay_frame(), shopProc
  set $buf = (unsigned char *)malloc(65536)
  set $sz = (unsigned)netplay_checkpoint_last($buf, 65536)
  printf "GDB: turn blob %u bytes, turn %d player %d\n", $sz, GWSystem.turn, GWSystem.player_curr
  call (int)netplay_match_continue(&'netplay_match.c'::match.m, $buf, $sz)
  continue
end
break HuPrcChildDtorRun
commands
  silent
  printf "GDB: destructor pass skipped (old behaviour)\n"
  return 0
  continue
end
run
printf "GDB: stopped\n"
bt 12
quit
