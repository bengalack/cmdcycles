# How cmdcycle measures

This is the detail behind the [README](../README.md).

## One attempt

1. Set up the command parameters (and STOP any command still running).
2. `OUT`: start the command.
3. Wait exactly *d* Z80 cycles (unrolled code for small *d*, a counted loop for
   larger *d*).
4. `IN`: read S#2. The CE bit says whether the command is still running.
5. STOP the command, so the next attempt starts from an idle VDP.

The attempt *succeeds* if the command was done at the `IN`. Every attempt has the
same length, so consecutive attempts start at known positions in the display line.

## Start position: the sweep

A display line is exactly 228 Z80 cycles (1368 VDP cycles). How long a command takes
depends on where in the line it starts, because the VDP's access slots repeat every
line.

- Each frame starts with a *lock*: the CPU finds its exact position relative to the
  start of a line. It polls the HR bit (coarse), then a vernier stage pins it to
  one cycle, then two sync checks verify it.
- The attempts then run back to back. Their length is calibrated to *n* × 228 + 1
  cycles, so each attempt starts one cycle later in the line than the previous one.
- 228 attempts cover every start position once: one *sweep*. `-k n` repeats this
  n times (default 1), with different start lines.
- A guard line stops the attempts in time, so a command never runs into the bottom
  border (where it would be faster).

## Cycle table (default)

For each test and size, a binary search finds the smallest *d* where at least one of
the k × 228 attempts succeeds. A probe ends at its first success. Only a "no" needs
all attempts. About 7–8 probes per cell.

The attempt length is calibrated once per cell. For other *d* the length is computed:
the OUT→IN wait is exactly *d*, and the dispatcher takes 21 cycles more before the
`OUT` when *d* < 256. Below *d* = 48 the wait uses separate unrolled code per *d*, so
those values are calibrated one by one.

The lowest delay is 20 cycles. A 20 in the table means 20 or less.

## Histogram (`-g W H`)

For one size, every *d* from below the minimum up to where all attempts succeed is
measured with all k × 228 attempts. The number of successes per *d* rises from 0 to
k × 228. The histogram shows the increase per *d*: how many start positions finish at
exactly that duration.

The shape is plateaus and steps. Counts that are multiples of k
come from start positions that behave the same in every sweep.

## T9769 systems: +1 per VDP I/O

On MSX2+/turbo R systems with a T9769, every VDP I/O takes one extra cycle. The tool
detects this (by counting VDP status polls in one frame) and adjusts its timed code.
The results have a +1 included, so they match systems without the T9769.

Before measuring, it checks that the CPU runs at the expected speed. Cartridges that
add wait states (for example some flash cartridges with mapper RAM) make every
measurement wrong, so the tool stops with an error.

## Drifting clocks: turbo R

On most MSX systems the CPU clock is the VDP clock ÷ 6. On the turbo R the two
clocks drift apart (about 9 cycles per 1024 lines). A drift test at the start detects
this. Then every attempt gets its own lock and an explicit wait to its start
position, instead of running back to back. It is slower (about 3×), but drift can
only act within one attempt.

The option `-r` forces this path on any system: to test it on an emulator that doesn't
emulate the drift, or to measure with a lock per attempt on any machine.

## VDP I/O spacing

The VDP needs at least 15 Z80 cycles between two accesses to its ports when the display
is active (see
[VRAM access timing](https://aoineko.org/msxgl/index.php?title=VRAM_access_timing)). All timed
code keeps to that.
