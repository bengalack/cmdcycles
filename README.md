# cmdcycles (cmdcycle.com) | Measure V9938/V9958 commands in MSX Z80 cycles

## What it does

ignoring PAL/NTSC, 192/212, VBLANK/DISPLAY.


https://openmsx.org/vdp-vram-timing/vdp-timing.html 

https://www.msx.org/forum/msx-talk/development/extra-vdp-wait-cycles?page=0

https://gist.github.com/grauw/184cf27ed002f4d9d3ea1bb43c9bf1f6 


We can measure 12, 14 and 17 cycle delays, but measuring less than 15 is only possible with screen off, according to data from this research: https://aoineko.org/msxgl/index.php?title=VRAM_access_timing

G7, YAE, YJK seems to have struggles with cycles < 17 (ie. the 15 limit above applies here too) when LMMC is used (screen off). This may affect us.
G7 is screen 8.


| LMMM-TEOR          | 1    | 2    | 3    | 4    | 5    | 6    | 7    | 8    |
|--------------------|------|------|------|------|------|------|------|------|
| 1                  | 1348 | 1348 | 1348 | 1348 | 1348 | 1348 | 1348 | 1348 |  
| 2                  | 1348 | 1348 | 1348 | 1348 | 1348 | 1348 | 1348 | 1348 |  
| 3                  | 1348 | 1348 | 1348 | 1348 | 1348 | 1348 | 1348 | 1348 |  
| 4                  | 1348 | 1348 | 1348 | 1348 | 1348 | 1348 | 1348 | 1348 |  
| 5                  | 1348 | 1348 | 1348 | 1348 | 1348 | 1348 | 1348 | 1348 |  
| 6                  | 1348 | 1348 | 1348 | 1348 | 1348 | 1348 | 1348 | 1348 |  
| 7                  | 1348 | 1348 | 1348 | 1348 | 1348 | 1348 | 1348 | 1348 |  
| 8                  | 1348 | 1348 | 1348 | 1348 | 1348 | 1348 | 1348 | 1348 |

## Help text

![screenshot](img/help_v2.png)

## Understanding the results

## Detail section

## Requirements

* **Run:** MSX2 or higher, MSX-DOS
* **Build:** SDCC v4.2 or higher (tested with v4.6)

## Attributions

* This code includes code snippet by Grauw (see vdpwait.s)


## License

https://creativecommons.org/licenses/by-sa/4.0/