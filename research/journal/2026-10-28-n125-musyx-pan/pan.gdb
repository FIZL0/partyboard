# N125: stop at the first out-of-range float conversion (GCC's float-cast-overflow handler) and show the emitter.
set pagination off
set confirm off
set breakpoint pending on
handle SIGPIPE SIGUSR1 SIGUSR2 SIG33 SIG34 SIG35 nostop noprint pass
break __ubsan_handle_float_cast_overflow
commands
  bt 4
  frame 1
  info args
  frame 2
  info locals
  print *em
  print s3dListenerRoot
  kill
  quit
end
run
