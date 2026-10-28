#!/bin/sh
# coin.sh <outdir> [audio driver]: w04's music-synced coin event (mg_coin.c), forced from gdb. mash-a + --match 3@900
# (w04) --turbo --hash-log; player 0's first BoardSpaceWalkExec: that space gets the 0x400000 flag and player 0 is
# made small (BoardPlayerSizeSet 1), so the walk mini event runs fn_1_DB0, and its choice reads yes. Logs the frames the
# event's steps are reached at (D230: waits for MIDI ctrl 10-19, D2A4: 20-29, E18C: the button, E374: 30-39, ...)
# and every value msmMusGetMidiCtrl gives mg_coin (on change). Stops at frame END (default 9000).
# Env: PB_AUDIO_NET_STUBS=1 for N112's answers (12).
set -e
H=$(cd "$(dirname "$0")" && pwd)
R=$(cd "$H/../../../.." && pwd)
B=${B:-$R/build/linux-default-relwithdebinfo}
OUT=$1 DRIVER=${2:-dummy}
rm -rf "$OUT"; mkdir -p "$OUT/home"
cat > "$OUT/run.gdb" <<G
set pagination off
set confirm off
handle SIGUSR1 SIGUSR2 SIGPIPE nostop noprint
set breakpoint pending on
set \$forced = 0
set \$ctrl = -999
set \$st = -999
break BoardSpaceWalkExec if \$forced == 0 && player == 0
commands
  silent
  set \$forced = 1
  set var BoardSpaceGet(0, space)->flag = BoardSpaceGet(0, space)->flag | 0x400000
  call (void)BoardPlayerSizeSet(0, 1)
  printf "GDB: frame %u: space %d forced coin event\n", netplay_frame(), space
  continue
end
break BoardWinChoiceGet if \$forced == 1
commands
  silent
  set \$forced = 2
  set var winChoice = 0
  printf "GDB: frame %u: choice yes\n", netplay_frame()
  continue
end
G
for f in fn_1_DB0 fn_1_C7D4 fn_1_D230 fn_1_D2A4 fn_1_D554 fn_1_E18C fn_1_E374 fn_1_E4C0 fn_1_E6AC fn_1_D0AC fn_1_E144 fn_1_D0DC; do
cat >> "$OUT/run.gdb" <<G
tbreak $f
commands
  silent
  printf "GDB: frame %u: $f\n", netplay_frame()
  continue
end
G
done
cat >> "$OUT/run.gdb" <<G
break fn_1_CF8C
commands
  silent
  if lbl_1_data_588 != -1
    set \$v = (int)HuAudSeqMidiCtrlGet(lbl_1_data_588, 0, 1)
    set \$s = (int)msmMusGetStatus(lbl_1_data_588)
    if \$v != \$ctrl || \$s != \$st
      printf "GDB: frame %u: ctrl %d status %d\n", netplay_frame(), \$v, \$s
      set \$ctrl = \$v
      set \$st = \$s
    end
  end
  continue
end
break netplay_match_tick if netplay_frame() >= ${END:-9000}
commands
  printf "GDB: end at frame %u\n", netplay_frame()
  kill
  quit
end
run
quit
G
cd "$B"
env HOME=$OUT/home XDG_DATA_HOME=$OUT/home/.local/share SDL_AUDIO_DRIVER=$DRIVER SDL_AUDIO_DISK_OUTPUT_FILE=$OUT/out.raw \
  timeout -s KILL ${TMO:-900} gdb -q -batch -x "$OUT/run.gdb" --args ./partyboard \
  --script "$R/tools/netplay/mash-a.script" --match 3@900 --turbo --hash-log "$OUT/hash.log" > "$OUT/log.txt" 2>&1 || true
grep -a -E "^GDB|BGM1|netplay: match|SIGSEGV|SIGABRT" "$OUT/log.txt" | head -60
