# Debugging Crazy Taxi

dcrecomp carries the tools that get used on this. All off by default:

| Variable | What it does |
| --- | --- |
| `DCRECOMP_HWREG` | first write to each hardware register |
| `DCRECOMP_STACKEVERY=N` | the call chain interrupted, every Nth interrupt |
| `DCRECOMP_WATCH=addr` | write watchpoint, with the chain that did it |
| `DCRECOMP_SCREENSHOT=f.ppm` | write the frame to a PPM |
| `-DABI_CHECK=ON` | flag a function returning with a callee-saved register or SP changed |

A crash prints the faulting context, a backtrace walked from it, and the last
24 indirect-call targets - usually the only useful part, because MSVC's tail
calls flatten the native stack even when it is intact.

`DCRECOMP_STACKEVERY` is what identified the loop above: the same call chain on
every sample means the game has stopped making progress, and the chain names
the function to read.

