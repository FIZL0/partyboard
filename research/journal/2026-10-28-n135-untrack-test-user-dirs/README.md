# N135: untrack the .n133/ test user dirs

## What was there
c076e857 (N133) committed `.n133/{r1,r2,r3,u1}/`, the user dirs of its relaunch test runs (34 files):
4 x 16 MB `MarioPartyRD/Party Board/MemoryCardA.USA.raw` (card images holding the save's banner/icon art
taken from the disc, so extracted assets), `config.json`, `imgui.ini`, `logs/partyboard*.log`, an
`online/local-*.json`, and `Party Board/{dawn,pipeline}_cache.db*` (Dawn shader/pipeline caches).
Nothing else in the tree references them (N133's journal names no file under `.n133/`).
No other tracked card image, cache, `imgui.ini` or `MarioPartyRD` dir exists in the repo.

## What changed
- `git rm -r --cached .n133` + deleted the dir from this worktree.
- `.gitignore`: `.n[0-9]*/` (covers `.n133/`, `.n132-runs/` from N132's harness, and any later `.n1xx*/`).
  Checked: a scratch `.n135-probe/x/MemoryCardA.USA.raw` did not show in `git status --untracked-files=all`.
- `AGENTS.md`: the "never commit extracted assets" rule now names what a run writes (card images, caches,
  logs), says to put run user dirs under `.n<id>*/`, and to check `git status` before committing.

## Not verified
- History is not rewritten: the 4 card images (~64 MB) stay in c076e857's objects and in every clone's
  pack. Removing them needs a history rewrite of main plus a force push, which this task must not do
  (runner rule: never rewrite/push main). A human would have to decide on `git filter-repo` for that.
- The pattern only matches lowercase `.n<digits>` dirs at any depth; a run dir named otherwise (e.g.
  `runs/`, `.N135/`) is still not ignored; the AGENTS.md line is the guard for those.
