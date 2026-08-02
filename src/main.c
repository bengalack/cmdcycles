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

// Typedefs & defines --------------------------------------------------------
//
#define halt()				{__asm halt __endasm;}
#define enableInterrupt()	{__asm ei __endasm;}
#define disableInterrupt()	{__asm di __endasm;}
#define break()				{__asm in a,(0x2e) __endasm;} // for debugging. may be risky to use as it trashes A
#define arraysize(arr)      (sizeof(arr)/sizeof((arr)[0]))

#define VDPCMD_LMMM		    0b10010000 // LOGICAL COPY BLOCK
#define VDPCMD_LMMV		    0b10000000 // LOGICAL FILL
#define VDPCMD_LMCM         0b10100000 // "LOGICAL" (PIXEL) MOVE VRAM > CPU/RAM

#define VDPCMD_HMMC		    0b11110000 // FAST COPY BLOCK FROM MEM (2 and 2 pix horz)
#define VDPCMD_HMMM		    0b11010000 // FAST COPY BLOCK (2 and 2 pix horz)
#define VDPCMD_YMMM         0b11100000 // FASTEST COPY BLOCK (only Y differs)
#define VDPCMD_HMMV		    0b11000000 // FAST FILL (2 and 2 pix horz)

// #define VDPCMD_LINE		    0b01110000 // LINE

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

// #define NUM_TESTS           2
#define NUM_TESTS           5
#define NUM_HORIZONTALS     8
#define NUM_VERTICALS       8
#define INITIAL_LOOP_CYCLES 33

#define SCREEN_MODE         8 // 5? 8?

typedef signed char         s8;
typedef unsigned char       u8;
typedef signed short        s16;
typedef unsigned short      u16;
typedef signed long         s32;
typedef unsigned long       u32;

#define MAX_SYS_LEN         (61+1)

// -------------------------------------------------------------------------
// typedef union {
// 	struct {
// 		u8  w,h;
// 	};
// 	u16 wh;
// } COMBO2BYTES;

typedef struct {
    u8                      uW;
    u8                      uH;
    u8                      uCmd;
    u8                      uIterations;
    u16                     nStartDelay;
    u8                      uFirstWait;
} RunCombo;

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
extern void     customISR(void);
extern void     print(u8* szMessage);
extern u8       waitForKey(void);
extern bool     userOutputsToScreen(void);
extern void     getTime(DateTime* pDateTime);


extern u16      runTestCombo(RunCombo* p);
extern u8       getInitialDelayNI(u8 uVDP_CMD);

// from vdp.s
extern void     setVDPCmdParamsNI(u8 w, u8 h);
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
const u8                g_szVersion[]       = "0.6";
const u8                g_szErrorMSX[]      = "MSX2 or higher is required";

const u8                g_szTopLine1[]      = "cmdcycle v%s - %s  \r\n"; // markdown needs two spaces at end of line to force line break
const u8                g_szTopLine2[]      = "* VDP detected: %s%s. Screen %d. Screen off. (-i:%d -o:%d -w:%d)\r\n";
const u8                g_szTopLine3[]      = "* Result data on this T976x based system have a +1 cycle included\r\n";
const u8                g_szEmptyLine[]     = "\r\n";
//                                          = "                                                                               "; // 79!
const u8                g_szHeader1[]       = "| %-15s    | %d    | %d    | %d    | %d    | %d    | %d    | %d    | %d    |\r\n";
const u8                g_szHeader2[]       = "|--------------------|------|------|------|------|------|------|------|------|\r\n";
const u8                g_szResultLine[]    = "| %d                  | %4hu | %4hu | %4hu | %4hu | %4hu | %4hu | %4hu | %4hu |\r\n";
const u8                g_szSumLine[]       = "Sum cycles: %d  \r\n";
const u8                g_szTotalLine[]     = "Grand total cycles: %lu  \r\n";
const u8                g_szDurationTest[]  = "Duration test:  %d minutes %d seconds  \r\n"; // markdown needs two spaces at end of line to force line break
const u8                g_szDurationPrint[] = "Duration print: %d minutes %d seconds  \r\n"; // markdown needs two spaces at end of line to force line break

//"                             " // 29 chars (turbo r)
const u8                g_szHelptext[]      = "Usage: cmdcycle [opt][sys]\r\n"
                                            "\r\n"
                                            "Measure the duration of VDP\r\n"
                                            "command in MSX Z80 cycles.\r\n"
                                            "Output format is markdown.\r\n"
                                            "Use 80 char width. Written\r\n"
                                            "to stdout unless redirected.\r\n"
                                            "\r\n"
                                            "Version: %s\r\n"
                                            "\r\n"
                                            "Options (opt):\r\n"
                                            " -h Show this help message\r\n"
                                            // " -5 Screen 5 (default: 8)\r\n"
                                            " -o n Outer loops(6)\r\n"
                                            " -i n Inner loops(255)\r\n"
                                            " -w n Init 69-cycle waits(1)\r\n"
                                            "\r\n"
                                            "sys: Show sys name in report\r\n";

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
                             "Copy: LMMM-TEOR"
                            ,"Copy: HMMM"
                            ,"Copy: YMMM"
                            ,"Fill: HMMV"
                            ,"Fill: LMMV"
                            // ,"Line     "
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

const u8 const          aEXECUTE_CMD[NUM_TESTS] = \
                        {
                             VDPCMD_LMMM | LOGICAL_OP_TEOR  // just random logical op (which does not become 0)
                            ,VDPCMD_HMMM
                            ,VDPCMD_YMMM
                            ,VDPCMD_HMMV
                            ,VDPCMD_LMMV | LOGICAL_OP_TOR   // just random logical op  (which does not become 0)
                            // ,VDPCMD_LINE | LOGICAL_OP_EOR   // just random logical op
                        };

// RAM variables -------------------------------------------------------------
//
u8                      g_auBuffer[ 256 ];      // temp/general buffer here to avoid stack explosion
void* __at(0x0039)      g_pInterrupt;           // We assume that 0x0038 already holds 0xC3 (JP) in dos mode at startup
void*                   g_pInterruptOrg;

u8                      g_uVDPModel;
u8                      g_uInnerIterations;
u8                      g_uOuterIterations;
u8                      g_uStartWaits;
bool                    g_bHasT976xEngine;
DateTime                g_oTimestamp0;
DateTime                g_oTimestamp1;
DateTime                g_oTimestamp2;

u8                      g_auSysStr[MAX_SYS_LEN];// name of system

u16                     g_anDelays[NUM_TESTS][NUM_HORIZONTALS][NUM_VERTICALS];


// ---------------------------------------------------------------------------
void setCustomISR(void)
{
    disableInterrupt();
    g_pInterruptOrg = g_pInterrupt;
    g_pInterrupt    = &customISR;
    enableInterrupt();
}

// ---------------------------------------------------------------------------
void restoreOriginalISR(void)
{
    disableInterrupt();
    g_pInterrupt = g_pInterruptOrg;
    enableInterrupt();
}

// ---------------------------------------------------------------------------
// Must only be called when tR runs in z80 mode, normal ROM/RAM.
// Approximation; if 1 extra cycle, we say that we have a T976x engine
void detectT976xEngine(void)
{
    u16 nCmds = measureVDPCommandsInOneFrame();
    u16 nComparableNumber = getPALRefreshRate()? (0x948-1) : (0x7C4-1); // setting a delta of 1 cycle to avoid false negatives
    g_bHasT976xEngine = !(nCmds >= nComparableNumber); // adding 10 cycles 
}

// ---------------------------------------------------------------------------
void initVarsAndRig(void)
{
    strcpy(g_auSysStr,"<system/model name not set>");

    g_uVDPModel = getVDPModel();

    g_uInnerIterations = 255;
    g_uOuterIterations = 6;
    g_uStartWaits = 1;
}

// ---------------------------------------------------------------------------
//
void establishInitialDelays(void)
{
    disableInterrupt();

    for(u8 c = 0; c < NUM_TESTS; c++)
    {
        for(u8 u = 0; u < NUM_HORIZONTALS; u++)
        {
            for(u8 v = 0; v < NUM_VERTICALS; v++)
            {
                setVDPCmdParamsNI(aHORZ_LEN[u], aVERT_LEN[v]);
                g_anDelays[c][u][v] = (u16)getInitialDelayNI(aEXECUTE_CMD[c]) * INITIAL_LOOP_CYCLES;
            }
        }
    }

    enableInterrupt();
}

// ---------------------------------------------------------------------------
//
void runTest(void)
{
    RunCombo oParams;

    oParams.uIterations = g_uInnerIterations;
    oParams.uFirstWait = g_uStartWaits; // one-third-line's
    
    // halt(); // We need a common starting point for comparing numbers

    // disableInterrupt();

    for(u8 c = 0; c < NUM_TESTS; c++)
    {
        oParams.uCmd = aEXECUTE_CMD[c];
        for(u8 u = 0; u < NUM_HORIZONTALS; u++)
        {
            oParams.uW = aHORZ_LEN[u];
            for(u8 v = 0; v < NUM_VERTICALS; v++)
            {
                oParams.uH = aVERT_LEN[v];
                oParams.nStartDelay = g_anDelays[c][u][v];
                g_anDelays[c][u][v] = runTestCombo(&oParams);
            }
        }
    }
    // break();

    // enableInterrupt();
}

// ---------------------------------------------------------------------------
void runTests(void)
{
    vdpScreenEnabled(false);

    establishInitialDelays();

    runTest();

    vdpScreenEnabled(true);
}

// ---------------------------------------------------------------------------
// Outputs markdown (easy to paste into github or other markdown viewers)
void printReport(void)
{
    u8* szT976x = g_bHasT976xEngine ? " (T976x)" : "";

    // Top words first ------------------------
    sprintf(g_auBuffer,
            g_szTopLine1,
            g_szVersion,
            g_auSysStr
           );

    print(g_auBuffer);

    sprintf(g_auBuffer,
            g_szTopLine2,
            aVDP_NAME[g_uVDPModel],
            szT976x,
            SCREEN_MODE,
            g_uInnerIterations,
            g_uOuterIterations,
            g_uStartWaits
           );

    print(g_auBuffer);

    if(g_bHasT976xEngine)
    {
        sprintf(g_auBuffer, g_szTopLine3);
        print(g_auBuffer);
    }

    print(g_szEmptyLine);

    // Table top second -----------------------

    u16 nAdd = g_bHasT976xEngine ? 1 : 0;
    u32 ulSum = 0;

    for(u8 t = 0; t < arraysize(aTEST_NAME); t++)
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

        u16 nSum = 0;

        // Table data -----------------------------
        for(u8 v = 0; v < arraysize(aVERT_LEN); v++)
        {
            for(u8 u = 0; u < arraysize(aHORZ_LEN); u++)
                nSum += g_anDelays[t][u][v] + nAdd;

            sprintf(g_auBuffer,
                    g_szResultLine,
                    aVERT_LEN[v],
                    g_anDelays[t][0][v] + nAdd,
                    g_anDelays[t][1][v] + nAdd,
                    g_anDelays[t][2][v] + nAdd,
                    g_anDelays[t][3][v] + nAdd,
                    g_anDelays[t][4][v] + nAdd,
                    g_anDelays[t][5][v] + nAdd,
                    g_anDelays[t][6][v] + nAdd,
                    g_anDelays[t][7][v] + nAdd
                );

            print(g_auBuffer);
        }

        ulSum += (u32)nSum;

        print(g_szEmptyLine);
        sprintf(g_auBuffer, g_szSumLine,nSum);
        print(g_auBuffer);
        print(g_szEmptyLine);
    }

    sprintf(g_auBuffer, g_szTotalLine,ulSum);
    print(g_auBuffer);    
}

// ---------------------------------------------------------------------------
void printHelp(void)
{
    sprintf(g_auBuffer, g_szHelptext, g_szVersion);
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
// Do the test. If interactive/screen mode, enable 26,5 lines for output +
// wait for keypress before returning. When output is redirected to file,
// we don't do this.
u8 main(char** argv, u8 argc)
{
    initVarsAndRig();

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
            else if(strcmp(argv[n], "-i") == 0)
            {
                if(++n >= argc)
                {
                    print("ERROR:Missing value for -i");
                    return 1;
                }

                iVal = atoi(argv[n]);

                if((iVal < 1 ) || (iVal > 255))
                {
                    print("ERROR:Value for -i must be in range 1-255");
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
            else if(strcmp(argv[n], "-w") == 0)
            {
                if(++n >= argc)
                {
                    print("ERROR:Missing value for -w");
                    return 1;
                }

                iVal = atoi(argv[n]);
                if((iVal < 0) || (iVal > 255))
                {
                    print("ERROR:Value for -w must be in range 0-255");
                    return 1;
                }
                g_uStartWaits = (u8)iVal;
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

    vdpSet212Lines(false); // just to make sure
    disableInterrupt();
    vdpEnableLineInterruptNI(false); // to be sure
    enableInterrupt();
    detectT976xEngine(); // only to be called i z80 mode and no line interrupts

    bool hasScreenOutput = userOutputsToScreen();

    setCustomISR(); 

    u8 uPrevScrMode = changeMode(SCREEN_MODE);

    // ---------------------------------------------
    // READY! Set conditions for tests and run tests
    vdpSpritesEnabled(true);

    getTime(&g_oTimestamp0);
    runTests();
    getTime(&g_oTimestamp1);

    // ----------------------------------
    // Start cleanup before returning to DOS
    restoreOriginalISR();

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
    printReport();

    // ----------------------------------
    // Show duration of test and print
    getTime(&g_oTimestamp2);

    DateTime oDuration = {0,0,0,0,0,0};

    calcDuration(&g_oTimestamp0, &g_oTimestamp1, &oDuration);
    sprintf(g_auBuffer, g_szDurationTest, oDuration.uMinutes, oDuration.uSeconds);
    print(g_auBuffer);

    calcDuration(&g_oTimestamp1, &g_oTimestamp2, &oDuration);
    sprintf(g_auBuffer,g_szDurationPrint,oDuration.uMinutes,oDuration.uSeconds);
    print(g_auBuffer);

    return 0;
}