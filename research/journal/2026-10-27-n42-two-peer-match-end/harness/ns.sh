#!/bin/sh
# ns.sh: hold a private user+net namespace for both peers (lo + a dummy 10.77.0.1/24; iroh wants an address)
R=/home/ryha/repos/partyboard-slots/s1/build/n42run
setsid -f unshare -rn sleep 100000 < /dev/null > /dev/null 2>&1
sleep 1
NS=$(pgrep -n -f "^sleep 100000$")
echo $NS > $R/ns.pid
IN="nsenter -t $NS -U -n --preserve-credentials"
$IN ip link set lo up
$IN ip link add d0 type dummy
$IN ip addr add 10.77.0.1/24 dev d0
$IN ip link set d0 up
$IN ip -br addr
