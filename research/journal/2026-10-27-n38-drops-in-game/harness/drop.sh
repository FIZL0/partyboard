#!/bin/sh
# drop.sh <guest pid> <seconds>: drop every UDP datagram to/from the guest's sockets (5353 mDNS aside) for <seconds>
R=/home/ryha/repos/partyboard-slots/s3/build/n38run
NS=$(cat $R/ns.pid)
IN="nsenter -t $NS -U -n --preserve-credentials"
PORTS=$($IN ss -uapn | grep "pid=$1," | awk '{print $4}' | sed 's/.*://' | grep -v '^5353$' | sort -u | paste -sd, -)
echo "guest ports: $PORTS"
$IN nft -f - <<NFT
table inet n38 {
  chain i { type filter hook input priority 0; udp dport { $PORTS } drop; udp sport { $PORTS } drop; }
  chain o { type filter hook output priority 0; udp dport { $PORTS } drop; udp sport { $PORTS } drop; }
}
NFT
echo "drop on  $(date +%s%3N)"
sleep $2
$IN nft delete table inet n38
echo "drop off $(date +%s%3N)"
