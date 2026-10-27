#!/bin/sh
# pair.sh <tag> [guest args...]: host, then the guest with the host's code in its online.json; both detached
R=$(dirname "$(readlink -f "$0")")
T=$1; shift
NS=$(cat $R/ns.pid)
B=/home/ryha/repos/partyboard-slots/s3/build/linux-default-relwithdebinfo
rm -f $R/x/code
cd $B
setsid nsenter -t $NS -U -n --preserve-credentials env XDG_DATA_HOME=$R/host/data HOME=$R/host PB_N38=host PB_N38_DIR=$R/x \
  timeout -s KILL 900 ./partyboard --hash-log $R/host.$T.hash > $R/host.$T.out 2>&1 < /dev/null &
for i in $(seq 120); do [ -s $R/x/code ] && break; sleep 1; done
[ -s $R/x/code ] || { echo "no code"; exit 1; }
python3 -I -c 'import json,sys; p=sys.argv[1]; j=json.load(open(p)); j["last_code"]=open(sys.argv[2]).read().strip(); json.dump(j,open(p,"w"),indent=2)' \
  "$R/guest/data/MarioPartyRD/Party Board/online.json" $R/x/code
setsid nsenter -t $NS -U -n --preserve-credentials env XDG_DATA_HOME=$R/guest/data HOME=$R/guest PB_N38=guest PB_N38_DIR=$R/x \
  timeout -s KILL 900 ./partyboard --hash-log $R/guest.$T.hash "$@" > $R/guest.$T.out 2>&1 < /dev/null &
echo started
