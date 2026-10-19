// ---------------------------------------------------------------------------
// Assumptions:
//  * We start in DOS, hence 0x0038 already contains 0xC3 (jp)
//
// Notes:
//  * There is no support for global initialisation of RAM variables in this config
//  * SORRY! for the Hungarian notation, but is helps me when mixing asm and c
//      * Prefixes:
//      * s  = signed char    (s8)
//      * u  = unsigned char  (u8)
//      * i  = signed short   (s16)
//      * n  = unsigned short (u16)
//      * l  = signed long    (s32)
//      * ul = unsigned long  (u32)
//      * f  = float
//      * p  = pointer
//      * o  = object (struct)
//      * a  = array (single or multi-dim)
//      * b  = bool
//      * sz = zero terminated string (C/SDCC adds the zero automatically)
//      * g_ = global
//      
//      Postfixes:
//      * NI = No Interrupt allowed
//
// author: pal.hansen@gmail.com
// ---------------------------------------------------------------------------

#include <stdio.h>      // herein be sprintf
#include <string.h>     // memcpy/memset/strcmp
#include <stdbool.h>
#include <stdlib.h>     // atoi


// Defines shared with ASM files ---------------------------------------------
//
#define METHOD_WAIT         1           //; 0: No wait, 1: Counter, 2: Random (R-reg)
#define VDPCMD_LINE		    0b01110000  // LINE

// Typedefs & defines --------------------------------------------------------
//
#define halt()				{__asm halt __endasm;}
#define enableInterrupt()	{__asm ei __endasm;}
#define disableInterrupt()	{__asm di __endasm;}
#define break()				{__asm in a,(0x2e) __endasm;} // for debugging. may be risky to use as it trashes A
// #define arraysize(arr)      (sizeof(arr)/sizeof((arr)[0]))

#define VDPCMD_LMMM		    0b10010000 // LOGICAL COPY BLOCK
#define VDPCMD_LMMV		    0b10000000 // LOGICAL FILL
#define VDPCMD_LMCM         0b10100000 // "LOGICAL" (PIXEL) MOVE VRAM > CPU/RAM

#define VDPCMD_HMMC		    0b11110000 // FAST COPY BLOCK FROM MEM (2 and 2 pix horz)
#define VDPCMD_HMMM		    0b11010000 // FAST COPY BLOCK (2 and 2 pix horz)
#define VDPCMD_YMMM         0b11100000 // FASTEST COPY BLOCK (only Y differs)
#define VDPCMD_HMMV		    0b11000000 // FAST FILL (2 and 2 pix horz)

#define LOGICAL_OP_IMP      0b0000 // DC=SC
#define LOGICAL_OP_AND      0b0001 // DC=SCxDC
#define LOGICAL_OP_OR       0b0010 // DC=SC+DC
#define LOGICAL_OP_EOR      0b0011 // DC=SCxDC+SCxDC
#define LOGICAL_OP_NOT      0b0100 // DC=SC

#define LOGICAL_OP_TIMP     0b1000 // if SC=0 then DC=DC else DC=SC
#define LOGICAL_OP_TAND     0b1001 // if SC=0 then DC=DC else DC=SCxDC
#define LOGICAL_OP_TOR      0b1010 // if SC=0 then DC=DC else DC=SC+DC
#define LOGICAL_OP_TEOR     0b1011 // if SC=0 then DC=DC else DC=SCxDC+SCxDC
#define LOGICAL_OP_TNOT     0b1100 // if SC=0 then DC=DC else DC=SC

#define NUM_TESTS           6
#define NUM_HORIZONTALS     8
#define NUM_VERTICALS       8

#define INITIAL_LOOP_CYCLES 33      // max size: 63 (!)
#define SCRATCH_HIST_SIZE   1024
#define HEAP_MIN_SIZE       1024    // bytes: the heap is the free memory between the image and HIMEM (minus STACK_RESERVE)
#define BUFFER_SIZE         1024    // size of g_auBuffer, in bytes (help text is the greatest consumer of this buffer)
#define HIST_STR_BUF_SIZE   1024    // size of g_auHistStrBuf, in bytes
#define LEEWAY              150     // add a buffer to the initial delay to avoid having the real test MISS hitting the initial measurement (happens on turboR in emulator)
#define STACK_RESERVE       512     // bytes that must stay free between the program image and HIMEM
#define HIMEM               0xFC4A  // system variable: highest usable address

#define DEFAULT_INNER_LOOPS 64      // 0 = 256
#define DEFAULT_OUTER_LOOPS 3
#define HIST_SWEEP_LEN      228     // -g: Z80 cycles per display line = start positions per sweep
#define HIST_MAX_SWEEPS     287     // -g: 287 * 228 = 65436, fits u16
#define HIST_MIN_DELAY      20      // -g: dispatch_vdp_test can't make all delay values below 20
#define HIST_OVERHEAD_MAX   1400    // -g: safe upper bound in cycles for one attempt minus d (setup, checks, pad)
#define HIST_PAD_MIN        48      // -g: waitExact minimum (histogram.s)
#define HIST_CALIB_TRIES    3       // -g: calibration attempts per delay value
#define HIST_CMD_COLOR      0xAA    // -g: fixed colour for all commands (no colour-dependent timing)
#define HIST_LOCK_FAILED    0xFFFF  // -g: histMeasureDelay result when no HR lock was possible
#define SWEEP_LINEAR_MIN    48      // -n: from this delay value on, dispatch_vdp_test is linear in d (one calibration per cell)
#define SWEEP_DISPATCH_SHORT 21     // -n: dispatch_vdp_test takes 21 cycles more before the OUT when d < 256 (ld a,l / cp #48 / jr nc after a not-taken jr nz)

#define HIST_DRIFT_FAILED   0x7FFF  // -g: histMeasureDrift result when no lock was possible

// T976x detection (measureVDPPollsInOneFrame, iowait.s): loop passes in one
// frame, 35 cycles per pass without extra VDP I/O wait, 36 with the T976x +1.
// NTSC frame 59736 cycles, PAL 71364. The threshold is in the middle (a margin
// of ~850 / ~1000 cycles either way). Fewer passes than the threshold: T976x.
#define T976X_THRESHOLD_NTSC 1683   // middle of 1707 (59736 / 35) and 1659 (59736 / 36)
#define T976X_THRESHOLD_PAL  2010   // middle of 2039 (71364 / 35) and 1982 (71364 / 36)

// Plausible counts: from the T976x value minus half the gap to the normal value
// plus half the gap. Outside: unexpected CPU timing (e.g. cartridge wait states).
#define T976X_MEASUREMENTS   3      // frames measured; the smallest count is used (see detectT976xEngine)
#define T976X_PASSES_MIN_NTSC 1635  // 1659 - 24
#define T976X_PASSES_MAX_NTSC 1731  // 1707 + 24
#define T976X_PASSES_MIN_PAL  1954  // 1982 - 28
#define T976X_PASSES_MAX_PAL  2067  // 2039 + 28

#define DRIFT_RUNS          4       // -d: repeats
#define DRIFT_STEPS         4       // -d: wait lengths, see g_auDriftBlocks

#define SCREEN_MODE         8 // 5? 8?

enum condition { ACTIVE_DISPLAY, ACTIVE_DISPLAY_NO_SPRITES, NO_DISPLAY, NUM_CONDITIONS };

typedef signed char         s8;
typedef unsigned char       u8;
typedef signed short        s16;
typedef unsigned short      u16;
typedef signed long         s32;
typedef unsigned long       u32;

#define MAX_SYS_LEN         (61+1-22) // minus datestring

// -------------------------------------------------------------------------
typedef union {
	struct {
		u8  w,h;
	};
	u16 wh;
} COMBO2BYTES;

typedef struct {
    u8                      uW;
    u8                      uH;
    u8                      uCmd;
    u8                      uInnerIterations;
    u16                     nStartDelay;
    u8                      uOuterIterations;
    u16*                    pHistogramWinner; 
} RunCombo;

typedef struct {
    u16*                    panHistogram;
    u16                     nHistogramLength;
    u16                     nHistogramStartValue;
} Histogram;

typedef struct {
    u8                      uW;             // 0
    u8                      uH;             // 1
    u8                      uCmd;           // 2
    u8                      uGuardLine;     // 3     last line an attempt may start on
    u16                     nDelay;         // 4 & 5
    u16                     nTries;         // 6 & 7 k * 228
    u16                     nPad;           // 8 & 9 waitExact N per attempt, from calibration
    u16                     nSyncMisses;    // 10 & 11 accumulated by histogram.s and histCalibrate
    u8                      uStopOnSuccess; // 12    0xFF: stop at the first success (-n probes), 0: run all attempts
} HistCombo;

typedef struct {
    u16                     nYear;      // 1980-
    u8                      uMonth;     // 0-11
    u8                      uDay;       // 0-30
    u8                      uHours;     // 0-23
    u8                      uMinutes;   // 0-59
    u8                      uSeconds;   // 0-59
} DateTime;

// Declarations --------------------------------------------------------------
//

// from bioshelper.s
extern u8       getMSXType(void);
extern u8       getCPU(void);
extern void     changeCPU(u8 uMode);

extern void     enableTurbo(bool bEnable) __preserves_regs(e,h,l,iyl,iyh);
extern bool     isTurboEnabled(void) __preserves_regs(d,e,h,l,iyl,iyh);
extern bool     hasTurboFeature(void) __preserves_regs(d,e,h,l,iyl,iyh);

extern u8       changeMode(u8 uModeNum); 
extern void     setLineWidth(u8 uWidth);
extern void     print(u8* szMessage);
// extern u8       waitForKey(void);
extern bool     userOutputsToScreen(void);
extern void     getTime(DateTime* pDateTime);

extern void     prepareLineInterruptsNI(void) __preserves_regs(b,c,d,e,h,l,iyl);
extern void     cleanupLineInterruptsNI(void) __preserves_regs(b,c,d,e,h,l,iyl,iyh);

extern u16      runTestCombo(RunCombo* p, u16* panHistogramLastIndex);
extern u16      runTestComboNoHalts(RunCombo* p, u16* panHistogramLastIndex);
extern u8       getInitialDelayNI(u8 uVDP_CMD);

// from vdp.s
extern void     setVDPCmdParamsNI(u8 uCmd, u16 oHW); // oWH: COMBO2BYTES (u16)
extern bool     getPALRefreshRate(void);
extern void     setPALRefreshRate(bool bEnabled);
extern void     vdpSpritesEnabled(bool bEnabled);
extern void     vdpScreenEnabled(bool bEnabled);
extern void     vdpSet212Lines(bool b212);
extern void     vdpEnableLineInterruptNI(bool bEnable);
extern void     vdpSetInterruptLine(u8 uLine);
extern u8       getVDPModel(void);

// from cycle-test.z80.s
extern u8       dispatch_vdp_test(u8 uVDP_CMD, u16 nTargetDelay); // returns CMD status after wait (bit 0 is set if busy)

// from iowait.s
extern u16      measureVDPPollsInOneFrame(void);

// from histogram.s
extern void     histInit(bool bT976x);
extern u8       histPhaseAfterRun(HistCombo* p, u16 nTries); // samples until the HR edge, 0xFF = failed
extern u16      histMeasureDelay(HistCombo* p);              // successes out of p->nTries
extern u16      getImageEnd(void);
extern u8       histDriftPhase(HistCombo* p, u16 nBlocks);   // samples until the HR edge after a wait, 0xFF = failed
extern u16      histMeasureDelayRelock(HistCombo* p);        // like histMeasureDelay, but a lock before every attempt
extern void     histRelaxSyncCheck1(void);                   // relock mode: sync check 1 one cycle earlier (edge - 2)


// Consts --------------------------------------------------------------------
//
const u8                g_szVersion[]           = "0.99";
const u8                g_szErrorMSX[]          = "MSX2 or higher is required";

const u8                g_szTopLine1[]          = "cmdcycle v%s - %04hu-%02d-%02d %02d:%02d:%02d - %s  \r\n"; // markdown needs two spaces at end of line to force line break
const u8                g_szTopLine2[]          = "* VDP detected: %s%s, screen %d, mode: %s. (-i:%d -o:%d)\r\n";
const u8                g_szTopLine2x[]         = "* Sync method: %s, wait method: %s\r\n";
const u8                g_szTopLine3[]          = "* Result data on this T976x based system have a +1 cycle included\r\n";
const u8                g_szTopLine4_1[]        = "* No real histogram - numbers mean 'times tried before success, moving to a smaller number'\r\n";
const u8                g_szTopLine4_2[]        = "* Histogram type: True histogram\r\n";
const u8                g_szTopLine4_3[]        = "* Method: phase sweep, k:%u sweeps of 228 per probe\r\n";
const u8                g_szTopLine4_4[]        = "* %u is the lowest delay value measured: %u means %u or less\r\n";
const u8                g_szTopLine2g[]         = "* VDP detected: %s%s, screen %d, mode: %s. (-i:%d -o:%d k:%u)\r\n";
const u8                g_szTopLine2n[]         = "* VDP detected: %s%s, screen %d, mode: %s.\r\n"; // k: see the method line
const u8                g_szTopLine2xg[]        = "* Sync method: Line interrupts + HR vernier lock per frame\r\n";
const u8                g_szTopLine2xgR[]       = "* Sync method: Line interrupts + HR vernier lock per attempt (CPU/VDP clock drift: %d cycles per 1024 lines)\r\n";
const u8                g_szTopLine2xgF[]       = "* Sync method: Line interrupts + HR vernier lock per attempt (CPU/VDP clock drift test failed)\r\n";
const u8                g_szTopLine5[]          = "* Attempts per delay value: %u (k:%u * %d)\r\n";
const u8                g_szTopLine6[]          = "* Sync misses: %u (vernier %u, check1 %u, check2 %u, calibration %u, phase %u)\r\n";
const u8                g_szEmptyLine[]         = "\r\n";
//                                              = "                                                                               "; // 79!
const u8                g_szTitle1[]            = "## CYCLES\r\n";
const u8                g_szHeader1[]           = "| %-15s    | %d    | %d    | %d    | %d    | %d    | %d    | %d    | %d    |\r\n";
const u8                g_szHeader2[]           = "|--------------------|-----:|-----:|-----:|-----:|-----:|-----:|-----:|-----:|\r\n";
const u8                g_szResultLine[]        = "| %d                  | %4hu | %4hu | %4hu | %4hu | %4hu | %4hu | %4hu | %4hu |\r\n";
const u8                g_szHistHeader1[]       = "| %-15s    |                                                       |\r\n";
const u8                g_szHistHeader2[]       = "|--------------------|:------------------------------------------------------|\r\n";
const u8                g_szHistResultLine[]    = "| %dx%d %-6s         | "; // %6s is like "(32)  " or "(1440)". The histogram string is printed separately

const u8                g_szSumLine[]           = "Sum: %lu  \r\n";
const u8                g_szTotalLine[]         = "Grand total: %lu  \r\n";
const u8                g_szDurationTest[]      = "Duration test:  %d minutes %d seconds  \r\n"; // markdown needs two spaces at end of line to force line break
const u8                g_szDurationPrint[]     = "Duration print: %d minutes %d seconds  \r\n"; // markdown needs two spaces at end of line to force line break

const u8                g_szTitle2[]            = "## FAKE HISTOGRAM\r\n";
const u8                g_szTitle3[]            = "## HISTOGRAM\r\n";
const u8                g_szHistSize[]          = "### Size %dx%d\r\n";
const u8                g_szHistTabHeader1[]    = "| Operation          | Minimum | %-54s|\r\n";
const u8                g_szHistTabHeader2[]    = "|--------------------|:-------:|:------------------------------------------------------|\r\n";
const u8                g_szHistTabRow[]        = "| %-18s | %-7s | "; // the numbers are printed one by one after this
const u8                g_szHistColCount[]      = "Count";
const u8                g_szHistColNorm[]       = "Normalized (%)";

const u8* const         g_aszConditionNames[] = {
                                                    "Active display",
                                                    "No sprites",
                                                    "No display"
                                                };

const u8                g_szSyncMethod0[]         = "Line interrupts";
const u8                g_szSyncMethod1[]         = "VBLANK interrupt";

#if METHOD_WAIT==1
const u8                g_szWaitMethod[]         = "Incremental";
#elif METHOD_WAIT==2
const u8                g_szWaitMethod[]         = "Random (R-reg)";
#else
const u8                g_szWaitMethod[]         = "No wait";
#endif

                                                //"                             " // 29 chars (turbo r)
const u8                g_szHelptext[]          = "Usage:cmdcycle [opt] [sys]\r\n"
                                                  "\r\n"
                                                  "Measure the duration of VDP\r\n"
                                                  "command in MSX Z80 cycles\r\n"
                                                  "(outside VBLANK area).\r\n"
                                                  "Format is markdown, written\r\n"
                                                  "to stdout (use redirection)\r\n"
                                                  "\r\n"
                                                  "Version: %s\r\n"
                                                  "Wait: %s\r\n"
                                                  "\r\n"
                                                  "Options (opt):\r\n"
                                                  " -h Show this help message\r\n"
                                                  // " -5 Screen 5 (default: 8)\r\n"
                                                  " -o n Outer loops (%d)\r\n"
                                                  " -i n Inner loops (%d)\r\n"
                                                  " -1 Mode: normal (default)\r\n"
                                                  " -2 Mode: no sprites\r\n"
                                                  " -3 Mode: no screen\r\n"
                                                  " -t VBLANK timing\r\n"
                                                  " -g x y k Histogram (-h -g)\r\n"
                                                  " -n k Sweep method (-h -n)\r\n"
                                                  "\r\n"
                                                  "sys: Show sys name in report";

                                                //"                             " // 29 chars (turbo r)
const u8                g_szHelptextHist[]      = "Usage:cmdcycle -g x y k [sys]\r\n"
                                                  "\r\n"
                                                  "Histogram of the duration of\r\n"
                                                  "one command size (x*y) for\r\n"
                                                  "all tests.\r\n"
                                                  "\r\n"
                                                  "x: 1..%d (width)\r\n"
                                                  "y: 1..%d (height)\r\n"
                                                  "k: 1..%d sweeps of 228\r\n"
                                                  "\r\n"
                                                  "-i -o -1 -2 -3 apply as\r\n"
                                                  "usual. -t is not supported.";

                                                //"                             " // 29 chars (turbo r)
const u8                g_szHelptextSweep[]     = "Usage:cmdcycle -n k [sys]\r\n"
                                                  "\r\n"
                                                  "Cycle table measured with\r\n"
                                                  "the sweep method: every\r\n"
                                                  "start position in the line\r\n"
                                                  "is tried (k sweeps of 228).\r\n"
                                                  "\r\n"
                                                  "k: 1..%d\r\n"
                                                  "\r\n"
                                                  "-1 -2 -3 apply as usual.\r\n"
                                                  "-i -o -t -g not supported.";

const u8* const         aVDP_NAME[32] =
                        {
                             "V9938"
                            ,"<unknown>"
                            ,"V9958"
                            ,"V9968"
                            ,"V9978"
                            ,"<unknown>"
                            ,"<unknown>"
                            ,"<unknown>"
                            ,"<unknown>"
                            ,"<unknown>"
                            ,"<unknown>"
                            ,"<unknown>"
                            ,"<unknown>"
                            ,"<unknown>"
                            ,"<unknown>"
                            ,"<unknown>"
                            ,"<unknown>"
                            ,"<unknown>"
                            ,"<unknown>"
                            ,"<unknown>"
                            ,"<unknown>"
                            ,"<unknown>"
                            ,"<unknown>"
                            ,"<unknown>"
                            ,"<unknown>"
                            ,"<unknown>"
                            ,"<unknown>"
                            ,"<unknown>"
                            ,"<unknown>"
                            ,"<unknown>"
                            ,"<unknown>"
                            ,"<unknown>"
                        };

const u8* const         aTEST_NAME[NUM_TESTS] = \
                        {
                            "Copy: LMMM-TEOR",
                            "Copy: HMMM",
                            "Copy: YMMM",
                            "Fill: HMMV",
                            "Fill: LMMV",
                            "Line: LINE-TEOR"
                        };

const u8 const          aEXECUTE_CMD[NUM_TESTS] = \
                        {
                            VDPCMD_LMMM | LOGICAL_OP_TEOR,  // just random logical op (which does not become 0)
                            VDPCMD_HMMM,
                            VDPCMD_YMMM,
                            VDPCMD_HMMV,
                            VDPCMD_LMMV | LOGICAL_OP_TOR,   // just random logical op  (which does not become 0)
                            VDPCMD_LINE | LOGICAL_OP_EOR    // just random logical op
                        };

const u8 const          aHORZ_LEN[NUM_HORIZONTALS] = 
                        {
                             1
                            ,2
                            ,3
                            ,4
                            ,5
                            ,6
                            ,7
                            ,8
                        };

const u8 const          aVERT_LEN[NUM_VERTICALS] = 
                        {
                             1
                            ,2
                            ,3
                            ,4
                            ,5
                            ,6
                            ,7
                            ,8
                        };

// RAM variables -------------------------------------------------------------
//
u8                      g_auBuffer[ BUFFER_SIZE ];          // temp/general buffer here to avoid stack explosion
u8                      g_auHistStrBuf[ HIST_STR_BUF_SIZE ];// temp/general buffer here to avoid stack explosion

u8                      g_uVDPModel;
u8                      g_uInnerIterations;
u8                      g_uOuterIterations;
bool                    g_bHasT976xEngine;
u8                      g_uCondition;
bool                    g_bVBLANKTiming;
bool                    g_bHistogramMode;       // -g x y
bool                    g_bSweepMode;           // -n k: cycle table with the sweep method
bool                    g_bForceRelock;         // -r (diagnostic, with -n/-g): lock per attempt even when the clocks don't drift
bool                    g_bVerifyCounts;        // -v (diagnostic, with -n): count the successes at each minimum (all attempts)
u16                     g_nMinCount;            // -v: successes at the minimum of the last cell
u8                      g_uHitIdx;              // histogram.s: index in its frame of the attempt that ended a measurement (0xFF: relock path)
u8                      g_uFrameAttempts;       // histogram.s: attempts in the last full frame
u8                      g_uMinHitIdx;           // -v: g_uHitIdx of the success that set the minimum of the last cell
u8                      g_uMinFrameAttempts;    // -v: g_uFrameAttempts at that success
u8                      g_uGuardExtra;          // -m n (diagnostic): guard line n lines higher
u8                      g_uOnlyTest;            // -x c (diagnostic, with -n): only test c (1..6), 0: all
u8                      g_uVrMask;              // histogram.s: 0x40 = an attempt that ends outside the active area (VR) is discarded; 0 without display
bool                    g_bDriftMode;           // -d (hidden diagnostic): CPU/VDP clock drift
bool                    g_bRelock;              // -g: CPU/VDP clocks drift, so lock before every attempt
u16                     g_nT976xPasses;         // T976x detection: loop passes measured in one frame (-d report)
s16                     g_iDrift;               // -g: measured drift in CPU cycles per 1024 lines (HIST_DRIFT_FAILED: no lock)
u8                      g_auDrift[DRIFT_RUNS][DRIFT_STEPS]; // -d: vernier samples per run and wait length
u8                      g_uCmdColor;            // R#44 for all VDP commands. 0: random (R reg), else fixed (read by vdp.s)
u16                     g_nSweeps;              // -g: k
u16                     g_nTotalSyncMisses;     // -g: sync misses over the whole run
u16                     g_nMissVernier;         // -g diagnostics: vernier found no edge (histogram.s)
u16                     g_nMissSync1;           // -g diagnostics: sync check 1, expected HR == 0 (histogram.s)
u16                     g_nMissSync2;           // -g diagnostics: sync check 2, expected HR == 1 (histogram.s)
u16                     g_nMissCalib;           // -g diagnostics: calibration verification not 1 mod 228
u16                     g_nMissPhase;           // -g diagnostics: phase measurement after a run failed
u16                     g_nCalibLmod;           // histCalibrate: attempt length mod 228 at pad HIST_PAD_MIN (last calibration)
bool                    g_abHistTruncated[NUM_TESTS]; // -g: span hit the scratch buffer limit
u8                      g_uHistU;               // -g: index into aHORZ_LEN
u8                      g_uHistV;               // -g: index into aVERT_LEN
u8                      g_uFirstU;              // sizes to test, range: [first, end)
u8                      g_uEndU;
u8                      g_uFirstV;
u8                      g_uEndV;

u16*                    g_pHeapHead;            // next free heap address, jalla-malloc
u8*                     g_pHeapStart;           // heap: from the end of the program image ...
u16                     g_nHeapSize;            // ... this many bytes (set at startup, up to HIMEM - STACK_RESERVE)

u8                      ERROR_CODE;

u8                      g_auSysStr[MAX_SYS_LEN];// name of system

u16                     g_anScratchHistogram[SCRATCH_HIST_SIZE];                        // scratch for sampling
u16                     g_anResultDelays[NUM_TESTS][NUM_HORIZONTALS][NUM_VERTICALS];    // Initial and results delays
u16                     g_anMinCounts[NUM_TESTS][NUM_HORIZONTALS][NUM_VERTICALS];       // -v: successes at the minimum, of k * 228
u16                     g_anMinHit[NUM_TESTS][NUM_HORIZONTALS][NUM_VERTICALS];          // -v: hit index * 256 + attempts per frame
Histogram               g_aoHistogram[NUM_TESTS][NUM_HORIZONTALS][NUM_VERTICALS];       // Condensed and tidy

// ---------------------------------------------------------------------------
// Must only be called when tR runs in z80 mode, normal ROM/RAM.
// Approximation; if 1 extra cycle, we say that we have a T976x engine
// Returns false when the count is outside both expected windows: then the CPU
// is slowed (or sped up) by something else, e.g. a cartridge inserting wait
// states, and no cycle-exact measurement is possible.
bool detectT976xEngine(void)
{
    // Interrupts off: cartridge interrupts can not disturb it. But reading S#0 at
    // the very moment the VDP sets the VBLANK flag can return 0 and clear it, so
    // a frame boundary can be lost and the count covers two frames. A lost flag
    // can only make a count too large, so the smallest of a few counts is used.
    u16 nCmds = 0xFFFF;
    for(u8 n = 0; n < T976X_MEASUREMENTS; n++)
    {
        u16 nCount = measureVDPPollsInOneFrame();
        if(nCount < nCmds)
            nCmds = nCount;
    }
    g_nT976xPasses = nCmds; // shown in the -d report

    bool bPAL = getPALRefreshRate();
    u16 nComparableNumber = bPAL ? T976X_THRESHOLD_PAL : T976X_THRESHOLD_NTSC;
    g_bHasT976xEngine = !(nCmds >= nComparableNumber);

    if(bPAL)
        return (nCmds >= T976X_PASSES_MIN_PAL) && (nCmds <= T976X_PASSES_MAX_PAL);

    return (nCmds >= T976X_PASSES_MIN_NTSC) && (nCmds <= T976X_PASSES_MAX_NTSC);
}

// ---------------------------------------------------------------------------
void initVarsAndRig(void)
{
    strcpy(g_auSysStr,"<system/model name not set>");

    g_uVDPModel = getVDPModel();

    g_uInnerIterations  = DEFAULT_INNER_LOOPS;
    g_uOuterIterations  = DEFAULT_OUTER_LOOPS;
    g_bVBLANKTiming     = false;
    g_bHistogramMode    = false;
    g_bSweepMode        = false;
    g_bForceRelock      = false;
    g_bVerifyCounts     = false;
    g_uGuardExtra       = 0;
    g_uOnlyTest         = 0;
    g_bDriftMode        = false;
    g_bRelock           = false;
    g_iDrift            = 0;
    g_uCmdColor         = 0;
    g_nSweeps           = 1;
    g_nTotalSyncMisses  = 0;
    g_nMissVernier      = 0;
    g_nMissSync1        = 0;
    g_nMissSync2        = 0;
    g_nMissCalib        = 0;
    g_nMissPhase        = 0;
    memset(g_abHistTruncated, 0, sizeof(g_abHistTruncated));
    g_uHistU            = 0;
    g_uHistV            = 0;
    g_uFirstU           = 0;
    g_uEndU             = NUM_HORIZONTALS;
    g_uFirstV           = 0;
    g_uEndV             = NUM_VERTICALS;

    g_uCondition = ACTIVE_DISPLAY; // default

    g_pHeapStart = (u8*)getImageEnd(); // heap is not part of the image (keeps the .com small)
    g_pHeapHead  = (u16*)g_pHeapStart;
    g_nHeapSize  = 0;                  // set by the memory check in main

    memset(g_anResultDelays, 0, sizeof(g_anResultDelays));
    memset(g_aoHistogram, 0, sizeof(g_aoHistogram));

    ERROR_CODE = 0xff;
}

// ---------------------------------------------------------------------------
// The correct screen mode must be set up front for the values to be correct
//
void establishInitialDelays(void)
{
    u16 nMultiplier = (u16)INITIAL_LOOP_CYCLES;
    if(g_bHasT976xEngine)
        nMultiplier += 1; // T976x engines are +1 cycle in the command

    disableInterrupt();
    prepareLineInterruptsNI(); // sets status reg 2
    for(u8 c = 0; c < NUM_TESTS; c++)
    {
        u8 cCMD = aEXECUTE_CMD[c];
        for(u8 v = g_uFirstV; v < g_uEndV; v++)
        {
            COMBO2BYTES oWH;
            oWH.h = aVERT_LEN[v];

            for(u8 u = g_uFirstU; u < g_uEndU; u++)
            {
                oWH.w = aHORZ_LEN[u];

                setVDPCmdParamsNI(cCMD, oWH.wh); // packing params to avoid using stack for parameters
                g_anResultDelays[c][u][v] = (u16)(getInitialDelayNI(cCMD) * nMultiplier) + (u16)LEEWAY; // obeys active area restriction using line interrupts.
            }
        }
    }

    cleanupLineInterruptsNI();// sets status reg 0
    enableInterrupt();
}

// ---------------------------------------------------------------------------
// 
// nSize is a number of u16 elements, NOT bytes
u16* myMalloc(u16 nSize)
{
    u16 nBytes = nSize * sizeof(u16);
    u16 nUsed  = (u16)((u8*)g_pHeapHead - g_pHeapStart);

    if(nBytes > (u16)(g_nHeapSize - nUsed)) // subtraction, so the u16 math cannot wrap
        return NULL; // out of memory

    u16* p = g_pHeapHead;

    g_pHeapHead += nSize; // u16* arithmetic, so this advances nBytes bytes

    return p;
}

// ---------------------------------------------------------------------------
// Copy the actual histogram to a new buffer and store a pointer to it in the
// RunCombo struct, as well as some key data
bool recordAndClearHistogram(RunCombo* pParams, u16* panHistogramLastIndex, u16 nBestDelay, Histogram* pRecord)
{
    u16* pHistogramWinner = pParams->pHistogramWinner;
    u16* p = panHistogramLastIndex;
    u16* pLast = p;

    if(pHistogramWinner <= g_anScratchHistogram)
    {
        // print("ERROR: pistogram has written outside scratch buffer");
        ERROR_CODE = 1;
        break();
        return false;
    }

    while(p >= pHistogramWinner) // find the first of the last 1s in a streak of 1s. If none, use last element
    {
        if( *p > 1 )
            break;

        pLast = p;
        p--;
    }

    u16 nLength = (u16)(pLast - pHistogramWinner + 1);

    // if(nLength>SCRATCH_HIST_SIZE) // makes sense only if we allow smaller size than SCRATCH_HIST_SIZE
    // {
    //     // print("ERROR: histogram size going bananas");
    //     ERROR_CODE = 2;
    //     break();
    //     return false;
    // }

    u16* pHistogram = myMalloc(nLength);

    if(pHistogram == NULL)
    {
        // print("ERROR: Out of memory for histogram");
        ERROR_CODE = 3;
        break();
        return false;
    }

    memcpy(pHistogram, pHistogramWinner, nLength * sizeof(u16));

    pRecord->panHistogram = pHistogram;
    pRecord->nHistogramLength = nLength;
    pRecord->nHistogramStartValue = nBestDelay;

    // Clean, but only what is needed
    u16* pStart = pHistogramWinner - 1;
    u16 nTotalLen = (u16)((u8*)panHistogramLastIndex - (u8*)pStart + 2);
    memset(pStart, 0, nTotalLen);

    return true;
}

// ---------------------------------------------------------------------------
//
bool runTest(void)
{
    RunCombo oParams;

    oParams.uInnerIterations = g_uInnerIterations;
    oParams.uOuterIterations = g_uOuterIterations;

    memset(g_anScratchHistogram, 0, sizeof(g_anScratchHistogram));
    u16* panHistogramLastIndex = &g_anScratchHistogram[SCRATCH_HIST_SIZE-1];

    // This adjusts a +1 cycle for T976x engines. This is be always "safe" for
    // all measurements that are at least 19 (real HW are always above 32 here).
    // For emulators, we can get lower numbers, we just need to be
    // aware of logic around cycles: 12, 13, 15, 16, 18...
    u16 nAdd = g_bHasT976xEngine ? 1 : 0;

    for(u8 c = 0; c < NUM_TESTS; c++)
    {
        oParams.uCmd = aEXECUTE_CMD[c];

        for(u8 v = g_uFirstV; v < g_uEndV; v++)
        {
            oParams.uH = aVERT_LEN[v];

            for(u8 u = g_uFirstU; u < g_uEndU; u++)
            {
                oParams.uW = aHORZ_LEN[u];

                u16 nStartDelay = g_anResultDelays[c][u][v];

                oParams.nStartDelay = nStartDelay; // note this value will be changed by the function
                oParams.pHistogramWinner = (u16*)0x0000;

                u16 nBestDelay;
                if(g_bVBLANKTiming)
                    nBestDelay = runTestCombo(&oParams, panHistogramLastIndex);
                else
                    nBestDelay = runTestComboNoHalts(&oParams, panHistogramLastIndex);

                nBestDelay += nAdd;

                g_anResultDelays[c][u][v] = nBestDelay;

                if(!recordAndClearHistogram(&oParams, panHistogramLastIndex, nBestDelay, &g_aoHistogram[c][u][v]))
                    return false;
            }
        }
    }

    return true;
}

// ---------------------------------------------------------------------------
// (s1 - s2) mod 228, for vernier sample counts 0..228 (228 is the same as 0)
u16 histPhaseDiff(u8 s1, u8 s2)
{
    u16 n1 = (s1 >= HIST_SWEEP_LEN) ? (u16)(s1 - HIST_SWEEP_LEN) : s1;
    u16 n2 = (s2 >= HIST_SWEEP_LEN) ? (u16)(s2 - HIST_SWEEP_LEN) : s2;

    return (n1 >= n2) ? (n1 - n2) : (n1 + HIST_SWEEP_LEN - n2);
}

// ---------------------------------------------------------------------------
// Finds the pad that makes one attempt exactly n * 228 + 1 cycles long, and
// verifies it (see histogram.s). Stores it in p->nPad. Every failed attempt
// counts as a sync miss; after HIST_CALIB_TRIES the last pad is used.
bool histCalibrate(HistCombo* p)
{
    for(u8 n = 0; n < HIST_CALIB_TRIES; n++)
    {
        p->nPad = HIST_PAD_MIN;

        u8 s1 = histPhaseAfterRun(p, 1);
        u8 s2 = histPhaseAfterRun(p, 2);

        if((s1 != 0xff) && (s2 != 0xff))
        {
            u16 nMod = histPhaseDiff(s1, s2); // attempt length mod 228 with pad HIST_PAD_MIN
            g_nCalibLmod = nMod;               // -n derives the pad of other delay values from this

            p->nPad = HIST_PAD_MIN + ((nMod <= 1) ? (1 - nMod) : (HIST_SWEEP_LEN + 1 - nMod));

            s1 = histPhaseAfterRun(p, 1);
            s2 = histPhaseAfterRun(p, 2);

            if((s1 != 0xff) && (s2 != 0xff))
            {
                if(histPhaseDiff(s1, s2) == 1)
                    return true;

                g_nMissCalib++; // verification: attempt length not 1 mod 228
            }
            else
                g_nMissPhase++;
        }
        else
            g_nMissPhase++;     // phase measurement after a run failed

        p->nSyncMisses++;
    }

    return false;
}

// ---------------------------------------------------------------------------
// One delay value: guard line, calibration, measurement.
// Returns the number of successes out of p->nTries.
u16 histMeasure(HistCombo* p, u16 nDelay)
{
    p->nDelay = nDelay;
    p->uGuardLine = (u8)(211 - (nDelay + HIST_OVERHEAD_MAX) / HIST_SWEEP_LEN - 2 - g_uGuardExtra); // an attempt started on this line ends by line 211

    if(g_bRelock)                       // drifting CPU/VDP clocks: lock before every attempt, no calibration
        return histMeasureDelayRelock(p);

    histCalibrate(p); // on failure the last pad is used, the misses show in the report

    return histMeasureDelay(p);
}

// ---------------------------------------------------------------------------
// Drift between the CPU and VDP clocks over 1024 lines, in CPU cycles
// (see histDriftPhase). 0 means coupled clocks. HIST_DRIFT_FAILED if no lock.
s16 histMeasureDrift(HistCombo* p)
{
    u8 s1 = histDriftPhase(p, 1);
    u8 s2 = histDriftPhase(p, 65);     // 64 * 16 = 1024 lines more

    if((s1 == 0xff) || (s2 == 0xff))
        return HIST_DRIFT_FAILED;

    s16 iDrift = (s16)histPhaseDiff(s1, s2);
    if(iDrift > HIST_SWEEP_LEN / 2)
        iDrift -= HIST_SWEEP_LEN;

    return iDrift;
}

// ---------------------------------------------------------------------------
// -n: the part of an attempt's length that depends on d: the OUT -> IN wait
// (exactly d) plus dispatch_vdp_test's extra checks before the OUT for d < 256.
u16 sweepVarLen(u16 nD)
{
    return nD + ((nD < 256) ? SWEEP_DISPATCH_SHORT : 0);
}

// ---------------------------------------------------------------------------
// -n: pad for delay value nD, from one calibration at nD0 (Lmod0 = attempt
// length mod 228 at pad HIST_PAD_MIN). Valid for nD, nD0 >= SWEEP_LINEAR_MIN
// (dispatch_vdp_test's dynamic loop).
u16 sweepPad(u16 nLmod0, u16 nD0, u16 nD)
{
    u16 nLmod = (nLmod0 + (sweepVarLen(nD) % HIST_SWEEP_LEN) + HIST_SWEEP_LEN - (sweepVarLen(nD0) % HIST_SWEEP_LEN)) % HIST_SWEEP_LEN; // never negative

    return HIST_PAD_MIN + ((nLmod <= 1) ? (1 - nLmod) : (HIST_SWEEP_LEN + 1 - nLmod));
}

// ---------------------------------------------------------------------------
// -n: does at least one of the k * 228 attempts at nDelay succeed?
// Returns the successes (0 or 1 with uStopOnSuccess), or HIST_LOCK_FAILED.
u16 sweepProbe(HistCombo* p, u16 nDelay, u16 nLmod0, u16 nD0)
{
    p->nDelay = nDelay;
    p->uGuardLine = (u8)(211 - (nDelay + HIST_OVERHEAD_MAX) / HIST_SWEEP_LEN - 2 - g_uGuardExtra); // an attempt started on this line ends by line 211

    g_uHitIdx = 0xFF;                       // stays so on the relock path (-v)

    u16 nCount;
    if(g_bRelock)                           // drifting clocks: a lock per attempt, no calibration
        nCount = histMeasureDelayRelock(p);
    else
    {
        if(nDelay < SWEEP_LINEAR_MIN)       // direct stubs: no linear model, calibrate this value
            histCalibrate(p);
        else
            p->nPad = sweepPad(nLmod0, nD0, nDelay);

        nCount = histMeasureDelay(p);
    }

    return nCount;                          // successes (stops at 1 with uStopOnSuccess), or HIST_LOCK_FAILED
}

// ---------------------------------------------------------------------------
// -n: smallest delay value at which at least one attempt succeeds, for the
// cell in *p (binary search, see the spec). nInit: initial delay of the cell
// (from establishInitialDelays, includes LEEWAY). Returns HIST_LOCK_FAILED if
// no lock was possible.
void sweepNoteHit(void)                     // -v: where in its frame was the success that sets the minimum?
{
    g_uMinHitIdx        = g_uHitIdx;
    g_uMinFrameAttempts = g_uFrameAttempts;
}

// ---------------------------------------------------------------------------
u16 sweepFindMin(HistCombo* p, u16 nInit)
{
    u16 nLmod0 = 0;
    if(!g_bRelock)                          // one calibration per cell (LINE takes another setup branch per size)
    {
        p->nDelay = nInit;
        histCalibrate(p);
        nLmod0 = g_nCalibLmod;
    }

    u16 nStep = LEEWAY + 2 * INITIAL_LOOP_CYCLES;
    u16 nHi = nInit;                        // must succeed
    u16 nLo = (nInit > nStep + HIST_MIN_DELAY) ? (nInit - nStep) : HIST_MIN_DELAY; // must fail
    u16 nRes;

    while(true)                             // widen upwards until hi succeeds
    {
        nRes = sweepProbe(p, nHi, nLmod0, nInit);
        if(nRes == HIST_LOCK_FAILED)
            return HIST_LOCK_FAILED;
        if(nRes)
        {
            sweepNoteHit();
            break;
        }
        nHi += LEEWAY;
    }

    while(true)                             // widen downwards until lo fails (or the floor succeeds)
    {
        nRes = sweepProbe(p, nLo, nLmod0, nInit);
        if(nRes == HIST_LOCK_FAILED)
            return HIST_LOCK_FAILED;
        if(!nRes)
            break;
        sweepNoteHit();
        if(nLo == HIST_MIN_DELAY)           // floor reached and it still succeeds
        {
            nHi = HIST_MIN_DELAY;
            nLo = HIST_MIN_DELAY - 1;       // skips the search below
            break;
        }
        nHi = nLo;
        nLo = (nLo > 2 * INITIAL_LOOP_CYCLES + HIST_MIN_DELAY) ? (nLo - 2 * INITIAL_LOOP_CYCLES) : HIST_MIN_DELAY;
    }

    while(nHi - nLo > 1)                    // invariant: probe(nLo) fails, probe(nHi) succeeds
    {
        u16 nMid = nLo + (nHi - nLo) / 2;
        nRes = sweepProbe(p, nMid, nLmod0, nInit);
        if(nRes == HIST_LOCK_FAILED)
            return HIST_LOCK_FAILED;
        if(nRes)
        {
            nHi = nMid;
            sweepNoteHit();
        }
        else
            nLo = nMid;
    }

    if(g_bVerifyCounts)                     // -v (diagnostic): all attempts at the minimum, how many succeed?
    {
        p->uStopOnSuccess = 0;
        nRes = sweepProbe(p, nHi, nLmod0, nInit);
        p->uStopOnSuccess = 0xFF;
        if(nRes == HIST_LOCK_FAILED)
            return HIST_LOCK_FAILED;
        g_nMinCount = nRes;
    }

    return nHi;
}

// ---------------------------------------------------------------------------
// -n: the whole CYCLES table with the sweep method. Results (incl. the T976x
// +1, as in runTest) go into g_anResultDelays.
bool runSweepTest(void)
{
    HistCombo oParams;

    oParams.nTries         = g_nSweeps * HIST_SWEEP_LEN;
    oParams.nSyncMisses    = 0;
    oParams.uStopOnSuccess = 0xFF;          // a probe only needs one success

    u16 nAdd = g_bHasT976xEngine ? 1 : 0;   // see runTest

    histInit(g_bHasT976xEngine);
    g_uVrMask = (g_uCondition == NO_DISPLAY) ? 0 : 0x40; // see hfRun: VR catches a lost FH (no faster border without display)
    g_uCmdColor = HIST_CMD_COLOR;

    g_iDrift  = histMeasureDrift(&oParams); // coupled clocks: free-run per frame, else a lock per attempt
    g_bRelock = (g_iDrift != 0) || g_bForceRelock; // also when the drift test failed (HIST_DRIFT_FAILED); -r forces it

    if(g_bRelock)
        histRelaxSyncCheck1();              // see histLock

    bool bOk = true;

    for(u8 c = 0; bOk && (c < NUM_TESTS); c++)
    {
        if(g_uOnlyTest && (c != g_uOnlyTest - 1))
            continue;               // -x c (diagnostic)

        oParams.uCmd = aEXECUTE_CMD[c];

        for(u8 v = 0; bOk && (v < NUM_VERTICALS); v++)
        {
            oParams.uH = aVERT_LEN[v];

            for(u8 u = 0; bOk && (u < NUM_HORIZONTALS); u++)
            {
                oParams.uW = aHORZ_LEN[u];

                u16 nMin = sweepFindMin(&oParams, g_anResultDelays[c][u][v]);
                if(nMin == HIST_LOCK_FAILED)
                {
                    ERROR_CODE = 6;
                    bOk = false;
                }
                else
                {
                    g_anResultDelays[c][u][v] = nMin + nAdd;
                    g_anMinCounts[c][u][v]    = g_nMinCount;
                    g_anMinHit[c][u][v]       = ((u16)g_uMinHitIdx << 8) | g_uMinFrameAttempts;
                }
            }
        }
    }

    g_uCmdColor = 0;
    g_nTotalSyncMisses = oParams.nSyncMisses;

    return bOk;
}

// ---------------------------------------------------------------------------
// Phase 2 of -g mode: for each test, walk down from W (phase 1) until no
// attempt succeeds, then up until all attempts succeed. Stores the CDF counts
// (successes per delay value) of the span in g_aoHistogram.
bool runHistTest(void)
{
    HistCombo oParams;

    oParams.uW          = aHORZ_LEN[g_uHistU];
    oParams.uH          = aVERT_LEN[g_uHistV];
    oParams.nTries      = g_nSweeps * HIST_SWEEP_LEN;
    oParams.nSyncMisses = 0;
    oParams.uStopOnSuccess = 0;             // the histogram needs every attempt

    u16 nAdd = g_bHasT976xEngine ? 1 : 0; // see runTest

    histInit(g_bHasT976xEngine);
    g_uVrMask = (g_uCondition == NO_DISPLAY) ? 0 : 0x40; // see hfRun: VR catches a lost FH (no faster border without display)
    g_uCmdColor = HIST_CMD_COLOR;

    g_iDrift  = histMeasureDrift(&oParams); // coupled clocks: free-run per frame, else a lock per attempt
    g_bRelock = (g_iDrift != 0) || g_bForceRelock; // also when the drift test failed (HIST_DRIFT_FAILED); -r forces it

    if(g_bRelock)
        histRelaxSyncCheck1(); // drift: sync check 1 gets one cycle of slack, else ~3% of good locks are rejected (see histLock)

    bool bOk = true;

    for(u8 c = 0; c < NUM_TESTS; c++)
    {
        oParams.uCmd = aEXECUTE_CMD[c];

        Histogram* pRecord = &g_aoHistogram[c][g_uHistU][g_uHistV];
        u16 nStart = g_anResultDelays[c][g_uHistU][g_uHistV] - nAdd; // raw W

        // Walk down: scratch[k] = successes for nStart - k
        u16 nLen   = 0;
        u16 nDelay = nStart;
        while(true)
        {
            u16 nCount = histMeasure(&oParams, nDelay);
            if(nCount == HIST_LOCK_FAILED)
            {
                bOk = false;
                break;
            }

            g_anScratchHistogram[nLen++] = nCount;

            if((nCount == 0) || (nDelay <= HIST_MIN_DELAY))
                break;

            if(nLen >= SCRATCH_HIST_SIZE)
            {
                g_abHistTruncated[c] = true;
                ERROR_CODE = 5;
                break;
            }

            nDelay--;
        }

        if(!bOk) // no HR lock possible, e.g. wrong T976x detection
        {
            ERROR_CODE = 6;
            memset(g_anScratchHistogram, 0, nLen * sizeof(u16));
            break;
        }

        u16 nLowest = nDelay;

        for(u16 a = 0, b = nLen - 1; a < b; a++, b--) // reverse: index 0 = nLowest
        {
            u16 nTmp = g_anScratchHistogram[a];
            g_anScratchHistogram[a] = g_anScratchHistogram[b];
            g_anScratchHistogram[b] = nTmp;
        }

        // Walk up until all attempts succeed
        nDelay = nStart;
        while(g_anScratchHistogram[nLen - 1] != oParams.nTries)
        {
            if(nLen >= SCRATCH_HIST_SIZE)
            {
                g_abHistTruncated[c] = true;
                ERROR_CODE = 5;
                break;
            }

            nDelay++;
            u16 nCount = histMeasure(&oParams, nDelay);
            if(nCount == HIST_LOCK_FAILED)
            {
                bOk = false;
                break;
            }

            g_anScratchHistogram[nLen++] = nCount;
        }

        if(!bOk) // no HR lock possible, e.g. wrong T976x detection
        {
            ERROR_CODE = 6;
            memset(g_anScratchHistogram, 0, nLen * sizeof(u16));
            break;
        }

        // The span starts at the first delay value with any success
        u16 nFirst = 0;
        while((nFirst < nLen) && (g_anScratchHistogram[nFirst] == 0))
            nFirst++;

        u16 nSpan = nLen - nFirst;

        if(nSpan == 0)
            pRecord->nHistogramLength = 0; // printed as "(?)"
        else
        {
            u16* pSpan = myMalloc(nSpan);

            if(pSpan == NULL)
            {
                ERROR_CODE = 3;
                bOk = false;
            }
            else
            {
                memcpy(pSpan, &g_anScratchHistogram[nFirst], nSpan * sizeof(u16));

                pRecord->panHistogram         = pSpan;
                pRecord->nHistogramLength     = nSpan;
                pRecord->nHistogramStartValue = nLowest + nFirst + nAdd;
            }
        }

        memset(g_anScratchHistogram, 0, nLen * sizeof(u16));

        if(!bOk)
            break;
    }

    g_uCmdColor = 0;
    g_nTotalSyncMisses = oParams.nSyncMisses;

    return bOk;
}

// ---------------------------------------------------------------------------
// -d (hidden diagnostic): lock to the HR edge, wait an exact number of lines
// counted in CPU cycles, measure where the edge is. With coupled CPU/VDP clocks
// every wait length gives the same result; otherwise the difference is the drift.
const u8 const          g_auDriftBlocks[DRIFT_STEPS] = { 1, 17, 65, 129 }; // 16-line blocks: 0, 256, 1024, 2048 lines more than the first

void runDriftTest(void)
{
    HistCombo oParams;
    memset(&oParams, 0, sizeof(oParams));

    histInit(g_bHasT976xEngine);

    for(u8 r = 0; r < DRIFT_RUNS; r++)
        for(u8 s = 0; s < DRIFT_STEPS; s++)
            g_auDrift[r][s] = histDriftPhase(&oParams, g_auDriftBlocks[s]);

    g_nTotalSyncMisses = oParams.nSyncMisses;
}

// ---------------------------------------------------------------------------
void printDriftReport(DateTime* pDateTime)
{
    sprintf(g_auBuffer, g_szTopLine1, g_szVersion, pDateTime->nYear, pDateTime->uMonth + 1, pDateTime->uDay + 1,
            pDateTime->uHours, pDateTime->uMinutes, pDateTime->uSeconds, g_auSysStr);
    print(g_auBuffer);
    sprintf(g_auBuffer, "* VDP detected: %s%s. Lock misses: %u\r\n", aVDP_NAME[g_uVDPModel], g_bHasT976xEngine ? " (T976x)" : "", g_nTotalSyncMisses);
    print(g_auBuffer);
    sprintf(g_auBuffer, "* T976x detection: %u loop passes in one frame, %s, threshold %u (fewer: T976x)\r\n\r\n",
            g_nT976xPasses, getPALRefreshRate() ? "PAL" : "NTSC", getPALRefreshRate() ? T976X_THRESHOLD_PAL : T976X_THRESHOLD_NTSC);
    print(g_auBuffer);

    print("## DRIFT (CPU clock vs VDP clock)\r\n\r\n");
    print("| Lines after first | run 1 | run 2 | run 3 | run 4 |\r\n");
    print("|-------------------|------:|------:|------:|------:|\r\n");

    for(u8 s = 0; s < DRIFT_STEPS; s++)
    {
        sprintf(g_auBuffer, "| %-17u |", (u16)(g_auDriftBlocks[s] - 1) * 16);
        print(g_auBuffer);

        for(u8 r = 0; r < DRIFT_RUNS; r++)
        {
            u8 uRef = g_auDrift[r][0];
            u8 uVal = g_auDrift[r][s];

            if((uRef == 0xff) || (uVal == 0xff))
                sprintf(g_auBuffer, "  ? |");
            else if(s == 0)
                sprintf(g_auBuffer, " (%3u) |", uVal); // raw vernier samples of the reference
            else
            {
                s16 iDrift = (s16)histPhaseDiff(uRef, uVal); // 0..227
                if(iDrift > HIST_SWEEP_LEN / 2)
                    iDrift -= HIST_SWEEP_LEN;
                sprintf(g_auBuffer, " %c%4d |", (iDrift < 0) ? '-' : '+', (iDrift < 0) ? -iDrift : iDrift);
            }
            print(g_auBuffer);
        }
        print(g_szEmptyLine);
    }

    print(g_szEmptyLine);
    print("Drift in CPU cycles, relative to the first row (whose raw vernier counts\r\n");
    print("are shown in parentheses). 0 everywhere: CPU and VDP clocks are coupled.\r\n");
    print("Positive: the HR edge comes earlier than the CPU expects.\r\n");
}

// ---------------------------------------------------------------------------
void runTests(void)
{
    switch(g_uCondition)
    {
        case ACTIVE_DISPLAY:
            vdpScreenEnabled(true);
            vdpSpritesEnabled(true);
            break;

        case ACTIVE_DISPLAY_NO_SPRITES:
            vdpScreenEnabled(true);
            vdpSpritesEnabled(false);
            break;

        case NO_DISPLAY:
            vdpScreenEnabled(false);
            vdpSpritesEnabled(false);
            break;
    }

    halt(); // to be sure. I have seen issues in measurement right after VDP writes as the above!
    
    establishInitialDelays();

    if(g_bSweepMode)
        runSweepTest();
    else
    {
        bool bOk = runTest();

        if(bOk && g_bHistogramMode)
            runHistTest();
    }

    vdpScreenEnabled(true);
    vdpSpritesEnabled(true);
}

// ---------------------------------------------------------------------------
// Outputs markdown (easy to paste into github or other markdown viewers)
void printReportTop(DateTime* pDateTime, bool bRealHistogram)
{
    u8* szT976x = g_bHasT976xEngine ? " (T976x)" : "";
    bool bSweep   = g_bSweepMode;
    bool bNewSync = bRealHistogram || bSweep; // HR vernier lock (-g and -n)

    // Top words first ------------------------
    sprintf(g_auBuffer,
            g_szTopLine1,
            g_szVersion,
            pDateTime->nYear,
            pDateTime->uMonth + 1,
            pDateTime->uDay + 1,
            pDateTime->uHours,
            pDateTime->uMinutes,
            pDateTime->uSeconds,
            g_auSysStr
           );

    print(g_auBuffer);

    if(bRealHistogram)
        sprintf(g_auBuffer,
                g_szTopLine2g,
                aVDP_NAME[g_uVDPModel],
                szT976x,
                SCREEN_MODE,
                g_aszConditionNames[g_uCondition],
                g_uInnerIterations==0?256: g_uInnerIterations,
                g_uOuterIterations,
                g_nSweeps
               );
    else if(bSweep)
        sprintf(g_auBuffer,
                g_szTopLine2n,
                aVDP_NAME[g_uVDPModel],
                szT976x,
                SCREEN_MODE,
                g_aszConditionNames[g_uCondition]
               );
    else
        sprintf(g_auBuffer,
                g_szTopLine2,
                aVDP_NAME[g_uVDPModel],
                szT976x,
                SCREEN_MODE,
                g_aszConditionNames[g_uCondition],
                g_uInnerIterations==0?256: g_uInnerIterations,
                g_uOuterIterations
               );

    print(g_auBuffer);

    if(bNewSync)
    {
        if(!g_bRelock)
            print(g_szTopLine2xg);
        else if(g_iDrift == HIST_DRIFT_FAILED)
            print(g_szTopLine2xgF);
        else
        {
            sprintf(g_auBuffer, g_szTopLine2xgR, g_iDrift);
            print(g_auBuffer);
        }
    }
    else
    {
        sprintf(g_auBuffer,
                g_szTopLine2x,
                g_bVBLANKTiming?g_szSyncMethod1:g_szSyncMethod0,
                g_szWaitMethod
               );

        print(g_auBuffer);
    }

    if(g_bHasT976xEngine)
        print(g_szTopLine3);

    if(bNewSync)
    {
        if(bRealHistogram)
        {
            print(g_szTopLine4_2);

            sprintf(g_auBuffer, g_szTopLine5, (u16)(g_nSweeps * HIST_SWEEP_LEN), g_nSweeps, HIST_SWEEP_LEN); // fits u16, k <= HIST_MAX_SWEEPS
            print(g_auBuffer);
        }
        else
        {
            sprintf(g_auBuffer, g_szTopLine4_3, g_nSweeps);
            print(g_auBuffer);

            u16 nFloor = HIST_MIN_DELAY + (g_bHasT976xEngine ? 1 : 0); // as printed (see runSweepTest)
            sprintf(g_auBuffer, g_szTopLine4_4, nFloor, nFloor, nFloor);
            print(g_auBuffer);
        }

        if(g_nTotalSyncMisses > 0) // only worth mentioning when something went wrong
        {
            sprintf(g_auBuffer, g_szTopLine6, g_nTotalSyncMisses, g_nMissVernier, g_nMissSync1, g_nMissSync2, g_nMissCalib, g_nMissPhase);
            print(g_auBuffer);
        }
    }
    else
        print(g_szTopLine4_1);

    print(g_szEmptyLine);
}

// ---------------------------------------------------------------------------
// Outputs markdown (easy to paste into github or other markdown viewers).
// bWithFakeHistogram: false in -n mode (there is no such data).
void printReportBody(bool bWithFakeHistogram)
{
    print(g_szTitle1);
    print(g_szEmptyLine);

    // Table top second -----------------------
    // Minimum cycle counts

    u32 ulTotal = 0;

    for(u8 t = 0; t < NUM_TESTS; t++)
    {
        sprintf(g_auBuffer,
                g_szHeader1,
                aTEST_NAME[t],
                aHORZ_LEN[0],
                aHORZ_LEN[1],
                aHORZ_LEN[2],
                aHORZ_LEN[3],
                aHORZ_LEN[4],
                aHORZ_LEN[5],
                aHORZ_LEN[6],
                aHORZ_LEN[7]
               );

        print(g_auBuffer);
        print(g_szHeader2);

        u32 ulSum = 0;

        // Table data -----------------------------
        for(u8 v = 0; v < NUM_VERTICALS; v++)
        {
            for(u8 u = 0; u < NUM_HORIZONTALS; u++)
                ulSum += (u32)g_anResultDelays[t][u][v];

            sprintf(g_auBuffer,
                    g_szResultLine,
                    aVERT_LEN[v],
                    g_anResultDelays[t][0][v],
                    g_anResultDelays[t][1][v],
                    g_anResultDelays[t][2][v],
                    g_anResultDelays[t][3][v],
                    g_anResultDelays[t][4][v],
                    g_anResultDelays[t][5][v],
                    g_anResultDelays[t][6][v],
                    g_anResultDelays[t][7][v]
                );

            print(g_auBuffer);
        }

        ulTotal += ulSum;

        print(g_szEmptyLine);
        sprintf(g_auBuffer, g_szSumLine, ulSum);
        print(g_auBuffer);
        print(g_szEmptyLine);
    }

    sprintf(g_auBuffer, g_szTotalLine, ulTotal);
    print(g_auBuffer);

    if(!bWithFakeHistogram)
        return;

    print(g_szEmptyLine);
    print(g_szTitle2);
    print(g_szEmptyLine);


    // Next table -----------------------
    // Histogram

    ulTotal = 0;

    u8 szShortBuff[10];
    const u8 szCyclesTemplate[] = "(%hu)%c";

    u8 szShortBuff2[10];
    const u8 szStub[] = "%hu ";

    for(u8 t = 0; t < NUM_TESTS; t++)
    {
        sprintf(g_auBuffer,
                g_szHistHeader1,
                aTEST_NAME[t]
               );

        print(g_auBuffer);
        print(g_szHistHeader2);

        u32 ulSum = 0;

        // Table data -----------------------------
        for(u8 z = 0; z < (NUM_VERTICALS * NUM_HORIZONTALS); z++)
        {
            u8 u = z % NUM_HORIZONTALS;
            u8 v = z / NUM_VERTICALS;

            u8 uX = aHORZ_LEN[u];
            u8 uY = aVERT_LEN[v];

            Histogram* pHist = &g_aoHistogram[t][u][v];

            u16 nHistogramStartValue;

            u8* pHistTail  = g_auHistStrBuf; // append position, so we never rescan the string
            *pHistTail = '\0';               // reset string buffer

            // Last position we may start an append at, leaving room for "..." + terminating zero
            u8* pHistLimit = g_auHistStrBuf + HIST_STR_BUF_SIZE - 4;

            if(pHist->nHistogramLength == 0)
            {
                nHistogramStartValue = 0;
                szShortBuff[0] = '(';
                szShortBuff[1] = '?';
                szShortBuff[2] = ')';
                szShortBuff[3] = '\0';
            }
            else
            {
                nHistogramStartValue = pHist->nHistogramStartValue;
                bool bTruncated = false;

                sprintf(szShortBuff, szCyclesTemplate, nHistogramStartValue, (char)'\0');
                for(u16 x = 0; x < pHist->nHistogramLength; x++)
                {
                    u16 nTimes = pHist->panHistogram[x];
                    ulSum += (u32)nTimes; // always, so the sums stay correct even when truncating

                    if(bTruncated)
                        continue;

                    sprintf(szShortBuff2, szStub, nTimes);

                    u8 uLen = (u8)strlen(szShortBuff2);

                    if(pHistTail + uLen > pHistLimit) // no room for this entry plus a later "..."
                    {
                        pHistTail[0] = '.';
                        pHistTail[1] = '.';
                        pHistTail[2] = '.';
                        pHistTail[3] = '\0';
                        bTruncated = true;
                        continue;
                    }

                    strcpy(pHistTail, szShortBuff2); // writes uLen chars + terminating zero
                    pHistTail += uLen;
                }
            }

            sprintf(g_auBuffer,
                    g_szHistResultLine,
                    uX,
                    uY,
                    szShortBuff
                );

            print(g_auBuffer);
            print(g_auHistStrBuf);
            print(g_szEmptyLine); // "\r\n"
        }

        ulTotal += ulSum;

        print(g_szEmptyLine);
        sprintf(g_auBuffer, g_szSumLine, ulSum);
        print(g_auBuffer);
        print(g_szEmptyLine);
    }

    sprintf(g_auBuffer, g_szTotalLine, ulTotal);
    print(g_auBuffer);


}

// ---------------------------------------------------------------------------
// -v (diagnostic, with -n): per cell, how many of the k * 228 attempts succeed
// at the minimum found. A real minimum repeats in every sweep (>= k), a false
// success shows as a count of about 1.
void printMinCounts(void)
{
    print(g_szEmptyLine);
    sprintf(g_auBuffer, "## SUCCESSES AT MINIMUM (-v, of %u attempts)\r\n", (u16)(g_nSweeps * HIST_SWEEP_LEN));
    print(g_auBuffer);
    print(g_szEmptyLine);

    for(u8 t = 0; t < NUM_TESTS; t++)
    {
        sprintf(g_auBuffer, g_szHeader1, aTEST_NAME[t],
                aHORZ_LEN[0], aHORZ_LEN[1], aHORZ_LEN[2], aHORZ_LEN[3],
                aHORZ_LEN[4], aHORZ_LEN[5], aHORZ_LEN[6], aHORZ_LEN[7]);
        print(g_auBuffer);
        print(g_szHeader2);

        for(u8 v = 0; v < NUM_VERTICALS; v++)
        {
            sprintf(g_auBuffer, g_szResultLine, aVERT_LEN[v],
                    g_anMinCounts[t][0][v], g_anMinCounts[t][1][v], g_anMinCounts[t][2][v], g_anMinCounts[t][3][v],
                    g_anMinCounts[t][4][v], g_anMinCounts[t][5][v], g_anMinCounts[t][6][v], g_anMinCounts[t][7][v]);
            print(g_auBuffer);
        }

        print(g_szEmptyLine);
    }

    // Where in its frame was the success that set the minimum: "index/attempts per frame"
    // (index 0 = first attempt after the lock; "-" = lock per attempt, no frames)
    print("## DECIDING SUCCESS: ATTEMPT IN FRAME (-v, index/attempts per frame)\r\n");
    print(g_szEmptyLine);

    for(u8 t = 0; t < NUM_TESTS; t++)
    {
        sprintf(g_auBuffer, g_szHeader1, aTEST_NAME[t],
                aHORZ_LEN[0], aHORZ_LEN[1], aHORZ_LEN[2], aHORZ_LEN[3],
                aHORZ_LEN[4], aHORZ_LEN[5], aHORZ_LEN[6], aHORZ_LEN[7]);
        print(g_auBuffer);
        print(g_szHeader2);

        for(u8 v = 0; v < NUM_VERTICALS; v++)
        {
            u8* p = g_auBuffer + sprintf(g_auBuffer, "| %d                  |", aVERT_LEN[v]);
            for(u8 u = 0; u < NUM_HORIZONTALS; u++)
            {
                u16 n = g_anMinHit[t][u][v];
                if((n >> 8) == 0xFF)
                    p += sprintf(p, "    - |");
                else
                    p += sprintf(p, " %u/%u |", n >> 8, n & 0xFF);
            }
            sprintf(p, "\r\n");
            print(g_auBuffer);
        }

        print(g_szEmptyLine);
    }
}

// ---------------------------------------------------------------------------
// Outputs markdown. -g mode: per test the histogram H(d) = F(d) - F(d-1), where
// F(d) is the number of successes at delay value d (F = 0 below the minimum).
// One table with counts, one normalized to % of all attempts per delay value.
void printReportBodyHist(void)
{
    print(g_szTitle3);
    sprintf(g_auBuffer, g_szHistSize, aHORZ_LEN[g_uHistU], aVERT_LEN[g_uHistV]);
    print(g_auBuffer);
    print(g_szEmptyLine);

    s32 lTotal = (s32)g_nSweeps * HIST_SWEEP_LEN;
    u8  szMin[8];

    for(u8 uTable = 0; uTable < 2; uTable++)
    {
        sprintf(g_auBuffer, g_szHistTabHeader1, (uTable == 0) ? g_szHistColCount : g_szHistColNorm);
        print(g_auBuffer);
        print(g_szHistTabHeader2);

        for(u8 t = 0; t < NUM_TESTS; t++)
        {
            Histogram* pHist = &g_aoHistogram[t][g_uHistU][g_uHistV];

            if(pHist->nHistogramLength == 0)
                strcpy(szMin, "(?)");
            else
                sprintf(szMin, "%u", pHist->nHistogramStartValue);

            sprintf(g_auBuffer, g_szHistTabRow, aTEST_NAME[t], szMin);
            print(g_auBuffer);

            u16 nPrev = 0;
            for(u16 x = 0; x < pHist->nHistogramLength; x++) // printed one by one: no row length limit
            {
                u16 nCdf = pHist->panHistogram[x];
                s32 lH   = (s32)nCdf - (s32)nPrev; // can be negative on real HW (noise)
                nPrev    = nCdf;

                if(uTable == 0)
                    sprintf(g_auBuffer, "%ld ", lH);
                else
                {
                    s32 lHundredths = lH * 10000 / lTotal; // % with 2 decimals, integer math
                    u32 ulAbs = (lHundredths < 0) ? (u32)(-lHundredths) : (u32)lHundredths;
                    sprintf(g_auBuffer, "%s%lu.%02lu ", (lHundredths < 0) ? "-" : "", ulAbs / 100, ulAbs % 100);
                }

                print(g_auBuffer);
            }

            if(g_abHistTruncated[t])
                print("...");

            print(g_szEmptyLine);
        }

        print(g_szEmptyLine);
    }
}

// ---------------------------------------------------------------------------
void printHelp(void)
{
    sprintf(g_auBuffer, g_szHelptext, g_szVersion, g_szWaitMethod, (u8)DEFAULT_OUTER_LOOPS, (u16)(DEFAULT_INNER_LOOPS==0 ? 256 : DEFAULT_INNER_LOOPS));
    print(g_auBuffer);
}

// ---------------------------------------------------------------------------
void printHelpHist(void)
{
    sprintf(g_auBuffer, g_szHelptextHist, NUM_HORIZONTALS, NUM_VERTICALS, HIST_MAX_SWEEPS);
    print(g_auBuffer);
}

// ---------------------------------------------------------------------------
void printHelpSweep(void)
{
    sprintf(g_auBuffer, g_szHelptextSweep, HIST_MAX_SWEEPS);
    print(g_auBuffer);
}

// ---------------------------------------------------------------------------
// Returns the index of iVal in pTable, or 0xff if not found
u8 findLenIndex(const u8* pTable, u8 uLen, s16 iVal)
{
    for(u8 n = 0; n < uLen; n++)
    {
        if((s16)pTable[n] == iVal)
            return n;
    }

    return 0xff;
}

// ---------------------------------------------------------------------------
void calcDuration(DateTime* p1, DateTime* p2, DateTime* pDuration)
{
    u32 lSecondsStart = (u32)(p1->nYear-1980) * 365ul * 24ul * 60ul * 60ul + (u32)p1->uMonth * 30ul * 24ul * 60ul * 60ul + (u32)p1->uDay * 24ul * 60ul * 60ul + (u32)p1->uHours * 60ul * 60ul + (u32)p1->uMinutes * 60ul + (u32)p1->uSeconds;
    u32 lSecondsEnd =   (u32)(p2->nYear-1980) * 365ul * 24ul * 60ul * 60ul + (u32)p2->uMonth * 30ul * 24ul * 60ul * 60ul + (u32)p2->uDay * 24ul * 60ul * 60ul + (u32)p2->uHours * 60ul * 60ul + (u32)p2->uMinutes * 60ul + (u32)p2->uSeconds;
    u32 lSecondsDuration = lSecondsEnd - lSecondsStart;

    pDuration->uMinutes = (u8)(lSecondsDuration / 60);
    pDuration->uSeconds = (u8)(lSecondsDuration % 60);
}

// ---------------------------------------------------------------------------
u8 commandLineParameters(char** argv, u8 argc)
{
    // ------------------------------------
    bool bInnerSet = false;             // -i / -o given (not allowed with -n)
    bool bOuterSet = false;

    // Check params
    if(argc > 0)
    {
        for(u8 n=0; n < argc; n++)
        {
            s16 iVal;

            if(strcmp(argv[n], "-h") == 0 || strcmp(argv[n], "--help") == 0)
            {
                if((n + 1 < argc) && (strcmp(argv[n+1], "-g") == 0))
                    printHelpHist();
                else if((n + 1 < argc) && (strcmp(argv[n+1], "-n") == 0))
                    printHelpSweep();
                else
                    printHelp();
                return 0;
            }
            else if(strcmp(argv[n], "-1") == 0)
            {
                g_uCondition = 0;
            }
            else if(strcmp(argv[n], "-2") == 0)
            {
                g_uCondition = 1;
            }
            else if(strcmp(argv[n], "-3") == 0)
            {
                g_uCondition = 2;
            }
            else if(strcmp(argv[n], "-d") == 0) // hidden diagnostic, not in the help
            {
                g_bDriftMode = true;
            }
            else if(strcmp(argv[n], "-r") == 0) // hidden diagnostic: force the turbo R path (lock per attempt), not in the help
            {
                g_bForceRelock = true;
            }
            else if(strcmp(argv[n], "-v") == 0) // hidden diagnostic for -n: success counts at the minima, not in the help
            {
                g_bVerifyCounts = true;
            }
            else if((strcmp(argv[n], "-m") == 0) && (n + 1 < argc)) // hidden diagnostic: guard line n lines higher
            {
                g_uGuardExtra = (u8)atoi(argv[++n]);
            }
            else if((strcmp(argv[n], "-x") == 0) && (n + 1 < argc)) // hidden diagnostic for -n: only test c (1..6)
            {
                g_uOnlyTest = (u8)atoi(argv[++n]);
            }
            else if(strcmp(argv[n], "-t") == 0)
            {
                g_bVBLANKTiming = true;
            }
            else if(strcmp(argv[n], "-g") == 0)
            {
                if(n + 3 >= argc)
                {
                    print("ERROR:Missing value(s) for -g");
                    return 1;
                }

                iVal = atoi(argv[++n]);
                u8 uU = findLenIndex(aHORZ_LEN, NUM_HORIZONTALS, iVal);
                if((iVal < 1) || (iVal > NUM_HORIZONTALS) || (uU == 0xff))
                {
                    sprintf(g_auBuffer, "ERROR:x for -g must be in range 1-%d", NUM_HORIZONTALS);
                    print(g_auBuffer);
                    return 1;
                }

                iVal = atoi(argv[++n]);
                u8 uV = findLenIndex(aVERT_LEN, NUM_VERTICALS, iVal);
                if((iVal < 1) || (iVal > NUM_VERTICALS) || (uV == 0xff))
                {
                    sprintf(g_auBuffer, "ERROR:y for -g must be in range 1-%d", NUM_VERTICALS);
                    print(g_auBuffer);
                    return 1;
                }

                iVal = atoi(argv[++n]);
                if((iVal < 1) || (iVal > HIST_MAX_SWEEPS))
                {
                    sprintf(g_auBuffer, "ERROR:k for -g must be in range 1-%d", HIST_MAX_SWEEPS);
                    print(g_auBuffer);
                    return 1;
                }
                g_nSweeps = (u16)iVal;

                g_bHistogramMode = true;
                g_uHistU  = uU;
                g_uHistV  = uV;
                g_uFirstU = uU;
                g_uEndU   = uU + 1;
                g_uFirstV = uV;
                g_uEndV   = uV + 1;
            }
            else if(strcmp(argv[n], "-n") == 0)
            {
                if(n + 1 >= argc)
                {
                    print("ERROR:Missing value for -n");
                    return 1;
                }

                iVal = atoi(argv[++n]);
                if((iVal < 1) || (iVal > HIST_MAX_SWEEPS))
                {
                    sprintf(g_auBuffer, "ERROR:k for -n must be in range 1-%d", HIST_MAX_SWEEPS);
                    print(g_auBuffer);
                    return 1;
                }
                g_nSweeps    = (u16)iVal;
                g_bSweepMode = true;
            }

            else if(strcmp(argv[n], "-i") == 0)
            {
                if(++n >= argc)
                {
                    print("ERROR:Missing value for -i");
                    return 1;
                }

                iVal = atoi(argv[n]);

                if((iVal < 1 ) || (iVal > 256))
                {
                    print("ERROR:Value for -i must be in range 1-256");
                    return 1;
                }
                g_uInnerIterations = (u8)iVal;
                bInnerSet = true;
            }
            else if(strcmp(argv[n], "-o") == 0)
            {
                if(++n >= argc)
                {
                    print("ERROR:Missing value for -o");
                    return 1;
                }

                iVal = atoi(argv[n]);
                if((iVal < 1) || (iVal > 255))
                {
                    print("ERROR:Value for -o must be in range 1-255");
                    return 1;
                }
                g_uOuterIterations = (u8)iVal;
                bOuterSet = true;
            }
            else
            {
                if(strlen(argv[n]) > MAX_SYS_LEN-1) // minus the zero terminator
                {
                    sprintf(g_auBuffer, "ERROR:Sys name too long (>%d)", MAX_SYS_LEN-1);
                    print(g_auBuffer);
                    return 1;
                }

                strcpy(g_auSysStr, argv[n]);
            }
        }

        if(g_bHistogramMode && g_bVBLANKTiming) // the histogram needs exact timing with interrupts off
        {
            print("ERROR:-t can't be used with -g");
            return 1;
        }

        if(g_bSweepMode) // the old method's options do not apply to -n
        {
            if(g_bHistogramMode)
            {
                print("ERROR:-g can't be used with -n");
                return 1;
            }
            if(g_bVBLANKTiming)
            {
                print("ERROR:-t can't be used with -n");
                return 1;
            }
            if(bInnerSet || bOuterSet)
            {
                print("ERROR:-i/-o can't be used with -n");
                return 1;
            }
        }
    }

    return 2;
}

// ---------------------------------------------------------------------------
// Do the test. If interactive/screen mode, enable 26,5 lines for output +
// wait for keypress before returning. When output is redirected to file,
// we don't do this.
u8 main(char** argv, u8 argc)
{
    DateTime oTimestamp0;
    DateTime oTimestamp1;
    DateTime oTimestamp2;
    DateTime oDurationResult = {0,0,0,0,0,0};

    initVarsAndRig();

    if(argc > 0)
    {
        u8 ret = commandLineParameters(argv, argc);
        if(ret < 2)
            return ret;
    }

    // ------------------------------------
    // Enough memory between the image and the stack? The rest is the heap.
    u16 nTop = *(u16*)HIMEM;  // highest usable address (stack lives below it)
    u16 nEnd = getImageEnd();
    if((nTop < nEnd) || ((u16)(nTop - nEnd) < STACK_RESERVE + HEAP_MIN_SIZE))
    {
        print("ERROR:Not enough memory");
        return 1;
    }
    g_nHeapSize = (u16)(nTop - nEnd - STACK_RESERVE);

    // ------------------------------------
    // Initialize
    u8 uType = getMSXType();
    s8 sOrgCPU = -1;
    bool bRestoreTurbo = false;

    if(uType == 0)
    {
        print(g_szErrorMSX);
        return 1;
    }
    else if(uType == 3) // MSX turbo R
    {
        u8 uCPU = getCPU();
        if(uCPU != 0)
        {
            sOrgCPU = (s8)uCPU;
            changeCPU(0); // 0=Z80 (ROM) mode, 1=R800 ROM mode, 2=R800 DRAM mode
        }
    }

    if(hasTurboFeature())
    {
        if(isTurboEnabled())
        {
            bRestoreTurbo = true;
            enableTurbo(false);
        }
    }

    disableInterrupt();
    vdpEnableLineInterruptNI(false); // to be sure
    enableInterrupt();

    bool bTimingOk = detectT976xEngine(); // only to be called i z80 mode and no line interrupts

    if(!bTimingOk && !g_bDriftMode) // -d still runs: it shows the count
    {
        if(sOrgCPU != -1)
            changeCPU(sOrgCPU);

        if(bRestoreTurbo)
            enableTurbo(true);

        sprintf(g_auBuffer, "ERROR:Unexpected CPU timing (cartridge wait states?) %u passes/frame", g_nT976xPasses);
        print(g_auBuffer);
        return 1;
    }

    vdpSet212Lines(true); // due to using line interrupts, we need as many lines as possible

    bool hasScreenOutput = userOutputsToScreen();

    u8 uPrevScrMode = changeMode(SCREEN_MODE); // BIOS returns in DI
    enableInterrupt();

    // ---------------------------------------------
    // READY! Set conditions for tests and run tests
    getTime(&oTimestamp0);
    if(g_bDriftMode)
        runDriftTest();
    else
        runTests();
    getTime(&oTimestamp1);

    // ----------------------------------
    // Start cleanup before returning to DOS
    vdpSet212Lines(false);

    if(hasScreenOutput) // prepare wide and large screen when not redirecting to file
    {
        setLineWidth(80);
        changeMode(0);
    }
    else
    {
        changeMode(uPrevScrMode);
    }

    if(sOrgCPU != -1)
        changeCPU(sOrgCPU);

    if(bRestoreTurbo)
        enableTurbo(true);

    // ----------------------------------
    // Show summary
    if(g_bDriftMode)
        printDriftReport(&oTimestamp0);
    else
    {
        printReportTop(&oTimestamp0, g_bHistogramMode);

        if(g_bHistogramMode)
            printReportBodyHist();
        else
            printReportBody(!g_bSweepMode);

            if(g_bSweepMode && g_bVerifyCounts)
                printMinCounts();
    }

    // ----------------------------------
    // Show duration of test and print
    getTime(&oTimestamp2);

    calcDuration(&oTimestamp0, &oTimestamp1, &oDurationResult);
    sprintf(g_auBuffer, g_szDurationTest, oDurationResult.uMinutes, oDurationResult.uSeconds);
    print(g_auBuffer);

    calcDuration(&oTimestamp1, &oTimestamp2, &oDurationResult);
    sprintf(g_auBuffer,g_szDurationPrint,oDurationResult.uMinutes,oDurationResult.uSeconds);
    print(g_auBuffer);

    // // debugging timestamps
    // const u8 const template[] = "\r\nTimestamp (YYYY-DD-MM HH:MM:SS) %d: %04hu-%02d-%02d %02d:%02d:%02d  \r\n";
    // sprintf(g_auBuffer, template, 0, oTimestamp0.nYear, oTimestamp0.uMonth, oTimestamp0.uDay, oTimestamp0.uHours, oTimestamp0.uMinutes, oTimestamp0.uSeconds);
    // print(g_auBuffer);
    // sprintf(g_auBuffer, template, 1, oTimestamp1.nYear, oTimestamp1.uMonth, oTimestamp1.uDay, oTimestamp1.uHours, oTimestamp1.uMinutes, oTimestamp1.uSeconds);
    // print(g_auBuffer);
    // sprintf(g_auBuffer, template, 2, oTimestamp2.nYear, oTimestamp2.uMonth, oTimestamp2.uDay, oTimestamp2.uHours, oTimestamp2.uMinutes, oTimestamp2.uSeconds);
    // print(g_auBuffer);

    if(ERROR_CODE != 0xff)
    {
        sprintf(g_auBuffer, "ERROR: %d\r\n", ERROR_CODE);
        print(g_auBuffer);
        break();
    }

    return 0;
}