#!/bin/bash
# taunt.sh <build> <tag>: party.script --match 0@1500 under gdb with a taunt "played" (tauntActiveFXTbl[0] = 5: seat 0 is the only human, port 0; what
# the stubbed HuAudFXPlay returns) every time TauntUpdate reaches the L check; hash log + hit counts
H=/home/ryha/repos/partyboard-slots/s2/build/n85; b=$1; t=$2
R=$H/runs/taunt-$b-$t; rm -rf $R; mkdir -p "$R/data/MarioPartyRD/Party Board"
G=$R/cmds.gdb; cat > $G <<'G'
set pagination off
set breakpoint pending on
handle SIGPIPE SIGUSR1 SIGUSR2 nostop noprint
break board/main.c:2217
commands
silent
set var tauntActiveFXTbl[0] = 5
continue
end
break board/main.c:2185
commands
silent
continue
end
break board/main.c:2209
commands
silent
continue
end
run
info breakpoints
kill
quit
G
cd $H/$b
XDG_DATA_HOME=$R/data HOME=$R timeout -s KILL 3000 gdb -q -batch -x $G --args ./partyboard --script /home/ryha/repos/partyboard-slots/s2/tools/netplay/party.script --turbo --match 0@1500 --hash-log $R/h.hash > $R/out 2>&1
grep -E "^[0-9]+ +breakpoint|already hit" $R/out; grep -m1 "match over\|script ended" $R/out
