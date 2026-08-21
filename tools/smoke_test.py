#!/usr/bin/env python3
"""Boot smoke test: run the recompiled game and check how far init gets.

Not a matching decompilation test - we have no reference SH-4 simulator to
diff against. This checks the progress markers we have actually fixed, so a
recompiler or HAL change that regresses them fails loudly instead of quietly
going back to a spin loop.

Usage:  python tools/smoke_test.py [path/to/crazytaxi.exe] [seconds]
Exit 0 if every check passes, 1 otherwise.
"""
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
EXE = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "build/Release/crazytaxi.exe"
SECONDS = int(sys.argv[2]) if len(sys.argv) > 2 else 30

# (name, predicate over the captured log, failure hint)
CHECKS = [
    ("game binary loads",
     lambda t: "Loaded disc_extract/1ST_READ.BIN" in t,
     "disc_extract/1ST_READ.BIN missing - extract the disc first"),
    ("IRQ handler registered",
     lambda t: "[IRQ] handler registered" in t,
     "sh4_set_irq_handler never called from main.c"),
    ("init clears the 0x8C1583F8 spin loop",
     lambda t: t.count("pr=0x8C158400") < 100,
     "back to spinning on the frame counter - VBlank is not being delivered"),
    ("TA receives packets",
     lambda t: any(int(m) > 0 for m in re.findall(r"TA frame stats: (\d+) pkts", t)),
     "no geometry reaches the tile accelerator"),
    ("no unresolved calls into the ISR path",
     lambda t: "unresolved call to 0x0C156634" not in t,
     "func_8C156634 (ISR FPU save) lost its entry point"),
]


def main():
    if not EXE.exists():
        print(f"FAIL: {EXE} not found - build first")
        return 1

    # The game has no exit condition yet, so run it to a wall clock and kill it.
    # Output goes to a file rather than a pipe: a pipe fills up and blocks the
    # process long before the timeout, which looks exactly like a hang.
    logfile = ROOT / "build" / "smoke_test.log"
    logfile.parent.mkdir(parents=True, exist_ok=True)
    with open(logfile, "w", encoding="utf-8", errors="replace") as out:
        proc = subprocess.Popen([str(EXE)], cwd=ROOT, stdout=out,
                                stderr=subprocess.STDOUT)
        try:
            proc.wait(timeout=SECONDS)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()
    log = logfile.read_text(encoding="utf-8", errors="replace")

    failed = 0
    for name, check, hint in CHECKS:
        ok = check(log)
        print(f"{'PASS' if ok else 'FAIL'}  {name}")
        if not ok:
            print(f"      {hint}")
            failed += 1

    # Informational, not a check: init returning is the next milestone.
    print(f"\ninit returned: {'yes' if 'Game init returned' in log else 'not yet'}")
    unresolved = sorted(set(re.findall(r"unresolved call to (0x[0-9A-F]+)", log)))
    if unresolved:
        print(f"unresolved indirect targets ({len(unresolved)}): {', '.join(unresolved)}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
