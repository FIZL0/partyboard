# 3-finger tap (opens the menu bar) on the AVD touchscreen; needs adb root. Device found with getevent.
D=/dev/input/event2
e() { sendevent $D $1 $2 $3; }
for i in 0 1 2; do e 3 47 $i; e 3 57 $((100+i)); e 3 53 $((10000+i*5000)); e 3 54 16000; e 3 58 1024; done
e 0 0 0
sleep 0.1
for i in 0 1 2; do e 3 47 $i; e 3 58 0; e 3 57 -1; done
e 0 0 0
