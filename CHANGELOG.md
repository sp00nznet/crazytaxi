# Changelog

Format: [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), versions:
[SemVer](https://semver.org/). No release has been tagged yet.

## [Unreleased]

### Added
- `Setup.cmd` quick start: checks prerequisites, asks before installing,
  extracts your disc, translates, builds, and leaves `Play Crazy Taxi.cmd`.
- `entries.txt`: Crazy Taxi's runtime-only entry points, passed to the
  recompiler with `--entries` (needs dcrecomp#2).
- netlab recipe (`recomp-netlab/projects/crazytaxi.env`); the smoke test
  prints SKIP on a machine without the disc and a pass count otherwise.
- CI: Python syntax check and the smoke test's no-disc SKIP path.
- `docs/debugging.md` (moved out of the README), `ROADMAP.md`, `CONTRIBUTING.md`.

### Changed
- Relicensed MIT -> GPL-2.0, matching dcrecomp, whose hardware layer is
  derived from Flycast. `NOTICE` says what that covers.
- dcrecomp bumped to 2e7c093: GPL-2.0, `--entries`, headless build links (#4).
