# N138: run a Windows game under wine in gdb, stop at the first SIGSEGV whose pc is page-aligned (a jump to a bad
# address), print registers, the stack and the maps, kill. From the game's folder, inside the headless gamescope:
#   gamescope --backend headless -w 640 -h 480 -- gdb -batch -x segv.gdb --args wine partyboard.exe <args>
# Run with WINEDEBUG=+loaddll for the DLL bases (wine maps PE images anonymously: /proc maps has no names).
set pagination off
set confirm off
set print thread-events off
set print inferior-events off
handle SIGSEGV stop print pass
handle SIGUSR1 nostop noprint pass
handle SIGUSR2 nostop noprint pass
handle SIGQUIT nostop noprint pass
handle SIGPIPE nostop noprint pass
handle SIGILL stop print pass
run
set $n = 0
while $n < 2000
  if (((long)$pc) & 0xfff) == 0 && ((long)$pc) > 0x600000000000
    info registers
    x/40gx $rsp
    info proc mappings
    kill
    quit
  end
  set $n = $n + 1
  continue
end
kill
quit
