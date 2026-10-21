# cmdcycles | report V9938/V9958 command durations w/histogram

## What it does

`cmdcycle` runs the VDP commands HMMV, LMMV, HMMM, LMMM, YMMM and LINE in 64 sizes
(1..8 × 1..8 pixels/bytes) and measures how long each one takes, in Z80 cycles. The
results are meant as a reference for VDP emulation.

- Measured in the active display area, under three conditions, because display and
  sprites [change the timing](https://openmsx.org/vdp-vram-timing/vdp-timing.html):
  display + sprites, display without sprites, no display.
- Systems with a T9769 (MSX2+/turbo R) [add a wait cycle](https://www.msx.org/forum/msx-talk/development/extra-vdp-wait-cycles?page=0)
  to every VDP I/O. This is detected, and the results include that +1 so they can be
  compared directly with other systems.
- Works on drifting CPU/VDP clocks too (turbo R).

## How it measures

A command is started (OUT), then after exactly *d* cycles the status is read (IN): is it
done? The result for a size is the smallest *d* at which the command can be done.

A command's duration depends on where in the display line it starts. So every *d* is
tried from all 228 start positions in a line (a *sweep*), with an exact cycle count
between OUT and IN. `-k` repeats the sweep k times (default 1). More sweeps catch rare
cases, at the cost of time.

More detail: [docs/measurement.md](docs/measurement.md).

## Usage

```text
cmdcycle [opt] [sys] > file.md
```

The output is markdown. Redirect it to a file - as there is a lot of information (MSX DOS2 is needed for this).

| Option | |
| --- | --- |
| `-h` | Help (`-h -g`: help for the histogram) |
| `-g W H` | Histogram of one size (W × H), all tests, instead of the cycle table |
| `-k n` | Sweeps per measurement, 1..287 (default 1) |
| `-1` | Display and sprites on (default) |
| `-2` | Display on, sprites off |
| `-3` | No display |
| `-r` | Lock per attempt (as on the turbo R), on any system |
| `sys` | Name of the system, shown in the report |

Examples:

```text
cmdcycle Sony_HB-F1XD > f1xd.md          cycle table, k = 1
cmdcycle -3 -k 8 Sony_HB-F1XD > f1xd.md  cycle table, no display, k = 8
cmdcycle -g 8 8 Sony_HB-F1XD > g88.md    histogram of the 8x8 size
```

Run time (NTSC):

| | k = 1 | k = 8 |
| --- | --- | --- |
| Cycle table, MSX2/2+ | ~6 min | ~40 min |
| Cycle table, turbo R | ~20 min | ~2 h |
| Histogram | ~1–3 min | ~5–20 min |

## Results

### Cycle table

One table per test: columns are the width, rows the height. Each cell is the minimum
duration of that command in Z80 cycles. The lowest value that can be measured is 20;
a 20 means "20 or less".

### Histogram (`-g W H`)

For one size, how often each duration occurs over all start positions. The first
column is the minimum (as in the cycle table). The counts add up to k × 228.

Example: <!-- TODO: model name, command line -->

<!-- TODO: insert the generated histograms (all 6 tests) from a real model -->

## Errors

| Message | Meaning |
| --- | --- |
| `ERROR:Unexpected CPU timing ...` | The CPU is slower or faster than an MSX should be, for example because of cartridge wait states. The results would be wrong. |
| `ERROR: 6` | No stable lock on the display line was possible. |

## Requirements

- **Run:** MSX2 or higher, MSX-DOS
- **Build:** SDCC 4.2 or higher (tested with 4.5.24), `build.bat`

## Download

`dska/cmdcycle.com`

## License

<https://creativecommons.org/licenses/by-sa/4.0/>
