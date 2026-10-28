set pagination off
set breakpoint pending on
handle SIGUSR1 SIGUSR2 SIGPIPE nostop noprint
break hash_log_line if at % 500 == 0 && at >= 3000
commands
silent
printf "F %u ovl? ball pos %f %f vel %f %f pull %f stuck %d\n", at, lbl_1_bss_1894.x, lbl_1_bss_1894.y, lbl_1_bss_1888.x, lbl_1_bss_1888.y, lbl_1_bss_788[0], lbl_1_bss_312
continue
end
# gdb 18.1 asserts (thread.c set_thread_options, THREAD_EXITED) if it watches partyboard exit: stop at the last
# frame needed, then kill and quit.
break hash_log_line if at == 11500
commands
kill
quit
end
run
