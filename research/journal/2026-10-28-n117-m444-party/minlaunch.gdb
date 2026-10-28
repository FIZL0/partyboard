set pagination off
set breakpoint pending on
handle SIGUSR1 SIGUSR2 SIGPIPE nostop noprint
break pinball.c:367
commands
silent
printf "launch vel %f forced\n", lbl_1_bss_1888.y
set var lbl_1_bss_1888.y = $minv
continue
end
break hash_log_line if at % 500 == 0 && at >= 1000 && at <= 3500
commands
silent
printf "F %u ball %f %f vel %f %f\n", at, lbl_1_bss_1894.x, lbl_1_bss_1894.y, lbl_1_bss_1888.x, lbl_1_bss_1888.y
continue
end
# gdb 18.1 asserts (thread.c set_thread_options, THREAD_EXITED) if it watches partyboard exit: stop at the last
# frame needed, then kill and quit. The prints stop at 3500: m444 (min0) unloads by 3954 and its symbols go with it.
break hash_log_line if at == 3600
commands
kill
quit
end
run
