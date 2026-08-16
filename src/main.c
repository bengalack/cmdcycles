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
#define METHOD_WAIT         2           //; 0: No wait, 1: Counter, 2: Random (R-reg)
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

// #define NUM_TESTS           1
#define NUM_TESTS           6
#define NUM_HORIZONTALS     8
#define NUM_VERTICALS       8

#define INITIAL_LOOP_CYCLES 33      // max size: 63 (!)
#define SCRATCH_HIST_SIZE   1024 
#define MY_HEAP_SIZE        28000   // size in bytes
#define LEEWAY              150     // add a buffer to the initial delay to avoid having the real test MISS hitting the initial measurement (happens on turboR in emulator)
#define METHOD_SYNC         1       // 0: ISR, 1: LINE INTS

// #define DEFAULT_INNER_LOOPS 32  // this one is tested 16 times (16 x 16 = 256)
#define DEFAULT_INNER_LOOPS 64  // 0 = 256
#define DEFAULT_OUTER_LOOPS 3

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
// extern void     customISR(void);
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
// extern void     executeCmdWithPreppedParamsNI(u8 uCmd);
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

// from vdpwaitasm.s
extern u16      measureVDPCommandsInOneFrame(void);


// Consts --------------------------------------------------------------------
//
const u8                g_szVersion[]           = "0.79";
const u8                g_szErrorMSX[]          = "MSX2 or higher is required";

const u8                g_szTopLine1[]          = "cmdcycle v%s - %04hu-%02d-%02d %02d:%02d:%02d - %s  \r\n"; // markdown needs two spaces at end of line to force line break
const u8                g_szTopLine2[]          = "* VDP detected: %s%s, screen %d, mode: %s. (-i:%d -o:%d)\r\n";
const u8                g_szTopLine2x[]         = "* Sync method: %s, wait method: %s\r\n";
const u8                g_szTopLine3[]          = "* Result data on this T976x based system have a +1 cycle included\r\n";
const u8                g_szEmptyLine[]         = "\r\n";
//                                              = "                                                                               "; // 79!
const u8                g_szTitle1[]            = "## CYCLES\r\n";
const u8                g_szHeader1[]           = "| %-15s    | %d    | %d    | %d    | %d    | %d    | %d    | %d    | %d    |\r\n";
const u8                g_szHeader2[]           = "|--------------------|-----:|-----:|-----:|-----:|-----:|-----:|-----:|-----:|\r\n";
const u8                g_szResultLine[]        = "| %d                  | %4hu | %4hu | %4hu | %4hu | %4hu | %4hu | %4hu | %4hu |\r\n";
const u8                g_szHistHeader1[]       = "| %-15s    |                                                       |\r\n";
const u8                g_szHistHeader2[]       = "|--------------------|:------------------------------------------------------|\r\n";
const u8                g_szHistResultLine[]    = "| %dx%d %-6s         | %s\r\n"; // %6s is like "(32)  " or "(1440)"

const u8                g_szSumLine[]           = "Sum: %lu  \r\n";
const u8                g_szTotalLine[]         = "Grand total: %lu  \r\n";
const u8                g_szDurationTest[]      = "Duration test:  %d minutes %d seconds  \r\n"; // markdown needs two spaces at end of line to force line break
const u8                g_szDurationPrint[]     = "Duration print: %d minutes %d seconds  \r\n"; // markdown needs two spaces at end of line to force line break

const u8                g_szTitle2[]            = "## HISTOGRAM\r\n";

const u8* const         g_aszConditionNames[] = {
                                                    "Active display",
                                                    "No sprites",
                                                    "No display"
                                                };

#if METHOD_SYNC==1
const u8                g_szSyncMethod[]         = "Line interrupts";
#else
const u8                g_szSyncMethod[]         = "VBLANK interrupt";
#endif

#if METHOD_WAIT==1
const u8                g_szWaitMethod[]         = "Incremental";
#elif METHOD_WAIT==2
const u8                g_szWaitMethod[]         = "Random (R-reg)";
#else
const u8                g_szWaitMethod[]         = "No wait";
#endif

                                                //"                             " // 29 chars (turbo r)
const u8                g_szHelptext[]          = "Usage: cmdcycle [opt][sys]\r\n"
                                                  "\r\n"
                                                  "Measure the duration of VDP\r\n"
                                                  "command in MSX Z80 cycles\r\n"
                                                  "(outside VBLANK area).\r\n"
                                                  "Format is markdown, written\r\n"
                                                  "to stdout unless redirected.\r\n"
                                                  "\r\n"
                                                  "Version: %s\r\n"
                                                  "Sync: %s\r\n"
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
                                                  "\r\n"
                                                  "sys: Show sys name in report";

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
u8                      g_auBuffer[ 256 ];      // temp/general buffer here to avoid stack explosion
u8                      g_auHistStrBuf[ 256 ];  // temp/general buffer here to avoid stack explosion
void* __at(0x0039)      g_pInterrupt;           // We assume that 0x0038 already holds 0xC3 (JP) in dos mode at startup
void*                   g_pInterruptOrg;
u8                      g_uCurrentInterruptLine;
bool                    g_bACTIVE_AREA;         // raster currently in VBLANK (false) or not (true)

u8                      g_uVDPModel;
u8                      g_uInnerIterations;
u8                      g_uOuterIterations;
bool                    g_bHasT976xEngine;
u8                      g_uCondition;

u16*                    g_pHeapHead;            // for MY_HEAP, jalla-malloc

u8                      DBUG_N;
u8                      DBUG_X;
u8                      DBUG_Y;
u8                      ERROR_CODE;

u8                      DBUG_LEN1;
u8                      DBUG_LEN2;

u8                      g_auSysStr[MAX_SYS_LEN];// name of system

u16                     g_anScratchHistogram[SCRATCH_HIST_SIZE];                        // scratch for sampling
u16                     g_anResultDelays[NUM_TESTS][NUM_HORIZONTALS][NUM_VERTICALS];    // Initial and results delays
Histogram               g_aoHistogram[NUM_TESTS][NUM_HORIZONTALS][NUM_VERTICALS];       // Condensed and tidy
u8                      MY_HEAP[MY_HEAP_SIZE];                                          // Sadly, SDCC supports only 1024 kB from malloc...

// ---------------------------------------------------------------------------
// Must only be called when tR runs in z80 mode, normal ROM/RAM.
// Approximation; if 1 extra cycle, we say that we have a T976x engine
void detectT976xEngine(void)
{
    u16 nCmds = measureVDPCommandsInOneFrame();
    u16 nComparableNumber = getPALRefreshRate()? (0x948-2) : (0x7C4-2); // setting a delta of 2 cycle to avoid false negatives
    g_bHasT976xEngine = !(nCmds >= nComparableNumber);
}

// ---------------------------------------------------------------------------
void initVarsAndRig(void)
{
    strcpy(g_auSysStr,"<system/model name not set>");

    g_uVDPModel = getVDPModel();

    g_uInnerIterations = DEFAULT_INNER_LOOPS;
    g_uOuterIterations = DEFAULT_OUTER_LOOPS;

    g_uCurrentInterruptLine = 200; // start value needs to non-zero
    g_bACTIVE_AREA = false;

    g_uCondition = ACTIVE_DISPLAY; // default
    // g_uCondition = NO_DISPLAY;

    g_pHeapHead = (u16*)MY_HEAP;

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
        for(u8 u = 0; u < NUM_HORIZONTALS; u++)
        {
            COMBO2BYTES oWH;
            oWH.w = aHORZ_LEN[u];
            for(u8 v = 0; v < NUM_VERTICALS; v++)
            {
                oWH.h = aVERT_LEN[v];
                u8 cCMD = aEXECUTE_CMD[c];

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
u16* myMalloc(u16 nSize)
{
    u16* p = g_pHeapHead;

    if((u16)g_pHeapHead - (u16)MY_HEAP + nSize > MY_HEAP_SIZE)
        return NULL; // out of memory

    g_pHeapHead += nSize;

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
    RunCombo* pParams = &oParams;

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
DBUG_N = c;
        oParams.uCmd = aEXECUTE_CMD[c];

        for(u8 u = 0; u < NUM_HORIZONTALS; u++)
        {
DBUG_X = u;
            oParams.uW = aHORZ_LEN[u];

            for(u8 v = 0; v < NUM_VERTICALS; v++)
            {
DBUG_Y = v;
                u16 nStartDelay = g_anResultDelays[c][u][v];

                pParams->uH = aVERT_LEN[v];
                pParams->nStartDelay = nStartDelay; // note this value will be changed by the function
                pParams->pHistogramWinner = (u16*)0x0000;

#if METHOD_SYNC==1
                u16 nBestDelay = runTestComboNoHalts(&oParams, panHistogramLastIndex) + nAdd;
#else
                u16 nBestDelay = runTestCombo(&oParams, panHistogramLastIndex) + nAdd;
#endif

                g_anResultDelays[c][u][v] = nBestDelay;

                if(!recordAndClearHistogram(pParams, panHistogramLastIndex, nBestDelay, &g_aoHistogram[c][u][v]))
                    return false;
            }
        }
    }

    return true;
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

    runTest();

    vdpScreenEnabled(true);
    vdpSpritesEnabled(true);
}

// ---------------------------------------------------------------------------
// Outputs markdown (easy to paste into github or other markdown viewers)
void printReport(DateTime* pDateTime)
{
    u8* szT976x = g_bHasT976xEngine ? " (T976x)" : "";

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

    sprintf(g_auBuffer,
            g_szTopLine2x,
            g_szSyncMethod,
            g_szWaitMethod
           );

    print(g_auBuffer);

    if(g_bHasT976xEngine)
    {
        sprintf(g_auBuffer, g_szTopLine3);
        print(g_auBuffer);
    }

    print(g_szEmptyLine);
    print(g_szTitle1);
    print(g_szEmptyLine);

    // Table top second -----------------------

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


    print(g_szEmptyLine);
    print(g_szTitle2);
    print(g_szEmptyLine);


    // Next table -----------------------

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
            g_auHistStrBuf[0] = '\0'; // reset string buffer

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

                sprintf(szShortBuff, szCyclesTemplate, nHistogramStartValue, (char)'\0');
                for(u16 x = 0; x < pHist->nHistogramLength; x++)
                {
                    u16 nTimes = pHist->panHistogram[x];
                    ulSum += (u32)nTimes;
                    sprintf(szShortBuff2, szStub, nTimes);
                    strcat(g_auHistStrBuf, szShortBuff2);
                }
            }

            sprintf(g_auBuffer,
                    g_szHistResultLine,
                    uX,
                    uY,
                    szShortBuff,
                    g_auHistStrBuf
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


}

// ---------------------------------------------------------------------------
void printHelp(void)
{
    sprintf(g_auBuffer, g_szHelptext, g_szVersion, g_szSyncMethod, g_szWaitMethod, DEFAULT_OUTER_LOOPS, DEFAULT_INNER_LOOPS==0 ? 256 : DEFAULT_INNER_LOOPS);
    print(g_auBuffer);
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
    // Check params
    if(argc > 0)
    {
        for(u8 n=0; n < argc; n++)
        {
            s16 iVal;

            if(strcmp(argv[n], "-h") == 0 || strcmp(argv[n], "--help") == 0)
            {
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

    detectT976xEngine(); // only to be called i z80 mode and no line interrupts
    vdpSet212Lines(true); // due to using line interrupts, we need as many lines as possible

    bool hasScreenOutput = userOutputsToScreen();

    u8 uPrevScrMode = changeMode(SCREEN_MODE); // BIOS returns in DI
    enableInterrupt();

    // ---------------------------------------------
    // READY! Set conditions for tests and run tests
    getTime(&oTimestamp0);
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
    printReport(&oTimestamp0);

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