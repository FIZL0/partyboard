# N48: README + GitHub description for the fork

## What I did

- README.md: new title "Party Board Online", credits mariopartyrd/partyboard as upstream, a "What this fork adds"
  section and a "Not done yet" list, above upstream's Overview. Upstream's Overview, download, build and credits
  sections are unchanged, except one added line under Download: those releases are upstream's, without online play,
  and this fork publishes none yet (build from source).
- Each claim was checked against main (plan/DONE.md and the code): N2/N24 (transport, codes, LAN, relay), N6/N7/N20
  (lobby, picks), N9/N29/N31 (checkpoint saves after minigames, Continue tab, delete at party end), N10 (drops), N11
  (desync recovery), commit 56c21cef / src/port/portmain.cpp:545 (first launch preset, no prompt), N12/N16/N17/N21
  (Android/iOS/tvOS netcode builds).
- Worded differently from the brief, because main does not do these yet:
  - "desync resync": N46 (opentennis-style state resync) is open. README says a desync reloads the last checkpoint
    (N11) and lists resync as not done.
  - "autosave": only after each minigame (N9), not every turn (N47 open). Listed as not done.
  - iOS: only the crate cross-builds (N12); the app was never built (no Xcode). tvOS links a stub (N17).
- `gh repo edit FIZL0/partyboard --description 'Mario Party 4 PC/Android/iOS port with online play (lockstep over
  iroh QUIC), fork of mariopartyrd/partyboard'`. `gh repo view` shows the new description; the old one was
  "A WIP PC port of Mario Party 4.". Not renamed.

## Not verified

- How the README renders on GitHub: I didn't look at it there. It is plain GitHub Markdown, `&lt;name&gt;` is used in a list item.
- Upstream's badges/links (Build Status, Discord) still point at upstream. I left them alone because they are upstream's sections.
