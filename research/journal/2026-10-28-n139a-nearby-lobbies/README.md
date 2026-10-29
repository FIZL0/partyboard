# N139a: nearby lobbies in the browser (waiting on N139)

## What happened

N139a builds on N139's one-code signalling: the 'lobby here' record carries the host's
N139 short code, and the record goes through N139's pkarr publish/fetch path
(`net/src/signal.rs`, the page's `pbnet_js_http_*` fetch imports, the relay's ~1 request
per 2 s per address budget that the slot polling has to share).

At the start of this session N139 was not on main: `task/N139` (11632408) and its tested
merge (`refs/agent-runner/tested/N139`, bae1b0ed) exist, but `origin/main` (0195715b) does not
contain them, and this branch (task/N139a, from 0195715b) has no `net/src/signal.rs`.

Merging task/N139 into this branch by hand would put N139's whole change into N139a's merge
and fight the runner's own merge of N139, so the task is parked with `--after N139` and
reopens once N139 lands.

## Not verified

Everything in the task: nothing was built or run, because the code it extends is not on
this branch yet.
