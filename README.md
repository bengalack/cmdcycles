# cmdcycles (cmdcycle.com) | Measure V9938/V9958 commands in MSX Z80 cycles

## What it does

This tool is making a bunch of VDP commands and tries to measure the time the operation takes. From [earlier research](https://openmsx.org/vdp-vram-timing/vdp-timing.html) we know that screen off/on, sprites on/off affects the timings, so we measure separately under these conditions. Measurement is done in the active area only. We also know that some VDPs (systems) [add extra cycles](https://www.msx.org/forum/msx-talk/development/extra-vdp-wait-cycles?page=0) on VDP I/O, so that is catered for as well.

You want to redirect the output to a file, the size will be between 25kB-40kB.

_I should probably explain more about how this tool works! It's coming._

__Note: This is a slow tool. One run (only) will give you this output on an NTSC:__

```
Duration test:  14 minutes 31 seconds  
Duration print: 4 minutes 15 seconds  
```

## Help text

![screenshot](img/help.png)

## Understanding the results

The way it measures is:
* Execute a command
* Wait a known period (in cycles)
* Check if operation is finished on not

The cycle chart is showing the smallest possible measured command execution. The FAKE HISTOGRAM is not a histogram at all, it is the amount of repeated executions at each cycle count before it succeeded, and we moved on to try the next possibly value (one cycle lower than the previous). The fake histogram should be read from right to left, and the final value denotes the amount of tries before it found the lowest possible cycle duration for this command. This duration is found in the parentes.

## Requirements

* **Run:** MSX2 or higher, MSX-DOS
* **Build:** SDCC v4.2 or higher (tested with v4.5.24)

## Download

Find the precompiled com file(s) in the `dska\`-folder. The normal output file is called `cmdcycle.com`, but there is a compile setting which sets the specific wait-after-sync-method (not the same was "wait for execution to finish"). I have provided the 3 variants there as well, and they are called:

`cmdcycl0.com` - wait-after-sync-method: no wait  
`cmdcycl1.com` - wait-after-sync-method: incremental (counter)  
`cmdcycl2.com` - wait-after-sync-method: random (R-reg)  

## Attributions

* This code includes code snippet by Grauw (see vdpwait.s)

## License

https://creativecommons.org/licenses/by-sa/4.0/