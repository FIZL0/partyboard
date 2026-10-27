# N55: upstream aurora InputType::Code (blocked: needs a human)

## What I checked
- `extern/aurora` is pinned at 5143394. Upstream `encounter/aurora` main was fetched on 2026-10-27 and is
  at d0ace68. `git log -S'InputType::Code'` and a grep for `AUTOCORRECT` on origin/main find nothing,
  so the change is **not upstream** yet.
- `gh` is logged in as FIZL0. Its permissions on encounter/aurora are pull-only (`push: false`) and
  it has no fork of aurora. I did not create a fork or open a PR: that publishes to an external repo, and
  the task says it needs a human.
- `cmake/patches/aurora/0001-rmlui-code-input-type.patch` applies cleanly (`git apply --check`) to
  upstream main d0ace68, so the upstream PR needs no rebase.

## Ready to send
`0001-rmlui-add-InputType-Code.patch` here is the same diff as a `git format-patch` commit on top of
d0ace68, with a commit message. To upstream it:

    gh repo fork encounter/aurora --clone=false
    cd extern/aurora && git switch -c input-type-code origin/main
    git am ../../research/journal/2026-10-27-n55-upstream-aurora-code-input/0001-rmlui-add-InputType-Code.patch
    git push <fork> input-type-code && gh pr create -R encounter/aurora

After it merges: bump the `extern/aurora` submodule to a commit that has it, then delete
`cmake/patches/aurora/0001-rmlui-code-input-type.patch` and its bullet in `cmake/aurora_patches.cmake`
(the configure step already skips the patch with a reverse-apply check once it's in the tree).

## Not verified
- The upstream PR itself (not opened; no push rights or fork, needs a human).
- The submodule bump and patch removal: they wait on the upstream merge.
