# N127: iPhone check of N119's Bonjour LAN path — blocked, needs the user's iPhone

## What happened

Unattended run, 2026-10-28. The task is a hands-on check on a real iPhone (install the IPA, grant Local Network,
host/join both ways with a PC). There is no iPhone here:
- `idevice_id -l` → "Unable to retrieve device list!"; `usbmuxd` is inactive; no Apple device on USB.
- `ideviceinstaller` / `ios-deploy` are not installed, and the IPA is unsigned anyway (sideload/re-sign is the user's
  step, see the iOS IPA memory / N86).
- Granting Local Network and backgrounding the app are taps on the phone.

So nothing was run. No code changed. The task is blocked until the user runs the checklist below.

## Checklist for the user (each step: what to see)

Build: `tools/ios-build.sh && tools/ios-ipa.sh` → `build/partyboard-ios.ipa`, `tools/ios-ipa-check.py` passes.
Sign + install with your usual sideload tool. PC and iPhone on the same Wi-Fi (no client isolation).
iPhone logs: Console.app is macOS-only (no Mac here), so use `idevicesyslog | grep 'pbnet'` on Linux
(`sudo systemctl start usbmuxd`, phone plugged in and trusted), or the in-game log file (logs/partyboard.log, N109)
pulled from the app's Documents via Files.

1. First LAN tab open on the iPhone → iOS asks for Local Network. Allow. If denied, the log must say
   `pbnet: Bonjour: Local Network access is off for Party Board (Settings > Privacy): no LAN lobbies` (check this
   once too: deny, see the line, then re-enable in Settings).
2. iPhone hosts → PC's LAN tab lists it (name, open seats). `avahi-browse -rpt _pbn._udp` on the PC shows
   `<id>._pbn._udp` with the TXT `sd=..`, `user-data=pb1..`.
3. PC hosts → iPhone's LAN tab lists it.
4. Join both ways from the LAN list: PC joins the iPhone's lobby, iPhone joins the PC's lobby. Lobby fills.
5. Listing updates follow: when a seat fills, the other side's list shows one seat fewer within a few seconds; after
   the host leaves, the entry disappears (goodbye/deregistration), not after a 120 s TTL.
6. Background/foreground: on the iPhone LAN tab, press Home, wait ~30 s (and once with screen lock), return. If the
   daemon connection broke, the log shows `pbnet: Bonjour: connection broken, starting again`, and 2 s later the
   PC's lobbies are listed again and an iPhone host is visible on the PC again. Repeat while hosting.
7. Watch for `pbnet: Bonjour: register ... failed`, `browse failed`, `TXT update failed`, `browse error` — any of
   them is a finding for net/src/bonjour.rs.

## Not verified

Everything in the checklist: no iPhone attached to this machine and no way to tap through iOS permission prompts in
an unattended run. N119's Linux stand-in (Avahi libdns_sd, `cargo test --features bonjour -- --ignored`) is the only
coverage of the Bonjour path so far.
