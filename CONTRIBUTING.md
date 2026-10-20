# Contributing

Issues and PRs are welcome.

- **Never attach or commit game data**: the disc, `disc_extract/`, or the
  generated `src/game/` and `include/game/`. They are Sega's.
- **Fixes to the recompiler or hardware layer go to
  [dcrecomp](https://github.com/sp00nznet/dcrecomp)**, not here. This repo holds
  only what is specific to Crazy Taxi: `src/main.c`, `entries.txt`, `tools/`.
- Run `python tools/smoke_test.py` before and after, and paste both results in
  the PR.

PRs merge with a merge commit, so your commits keep your name.

## Where your code comes from

This repo is GPL-2.0. Your own code, or code under a GPL-2.0-compatible licence
(MIT, BSD, ISC, zlib, GPL-2.0-or-later, LGPL-2.1), is fine; say where anything
non-original came from. Not accepted: GPL-3.0-only, AGPL, non-commercial
licences, unlicensed code (most matching decomps), and anything from the
Katana SDK.

AI-assisted contributions are welcome, provided a person understood and
verified the change.
