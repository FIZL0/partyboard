set pagination off
set confirm off
set disable-randomization off
handle SIGUSR1 SIGUSR2 SIGPIPE nostop noprint
set breakpoint pending on
set $n = 0
break netplay_match_tick if 'netplay_match.c'::match.running && omcurovl == 89 && ((netplay_frame() == 2600 && $n == 0) || (netplay_frame() == 4100 && $n == 1) || (netplay_frame() == 6000 && $n == 2) || (netplay_frame() == 8000 && $n == 3) || (netplay_frame() == 10500 && $n == 4))
commands
  silent
  set $n = $n + 1
  printf "GDB: reload %d at frame %u, ovl %d, player_curr %d\n", $n, netplay_frame(), omcurovl, GWSystem.player_curr
  call (int)netplay_match_start(&'netplay_match.c'::match.m)
  continue
end
run
printf "GDB: stopped\n"
bt 12
quit
