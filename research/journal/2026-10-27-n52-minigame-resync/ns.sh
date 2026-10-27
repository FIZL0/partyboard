#!/bin/sh
# ns.sh: a private user+net namespace with lo and a dummy 10.77.0.1/24 (iroh needs an address); prints its pid
R=$(dirname "$(readlink -f "$0")")
setsid unshare -rn sleep 100000 < /dev/null > /dev/null 2>&1 &
P=$!
sleep 0.5
nsenter -t $P -U -n --preserve-credentials sh -c 'ip link set lo up; ip link add d0 type dummy; ip addr add 10.77.0.1/24 dev d0; ip link set d0 up'
echo $P > $R/ns.pid
echo $P
