// ---------------------------------------------------------------------------
// Assumptions:
//  * We start in DOS, hence 0x0038 already contains 0xC3 (jp)
//  * There are no active line (or other non-VBLANK-) interrupts enabled
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

#define NUM_TESTS           5
#define NUM_HORIZONTALS     8
#define NUM_VERTICALS       8
#define INITIAL_LOOP_CYCLES 33

#define SCREEN_MODE         8 // 5? 8?

// enum raster_location { VBLANK, ACTIVE_AREA, RASTER_LOCATION_COUNT };
// enum orientation { LANDSCAPE, PORTRAIT, ORIENTATION_COUNT };
// enum condition { NORMAL, NO_SPRITES, NO_SCREEN, NORMAL_CPU, NO_SCREEN_CPU, CONDITION_COUNT };
// enum freq_variant { NTSC, PAL, FREQ_COUNT };
// enum line_variant { NORMAL192, EXTENDED212, LINE_VARIANT_COUNT };

typedef signed char         s8;
typedef unsigned char       u8;
typedef signed short        s16;
typedef unsigned short      u16;
typedef signed long         s32;
typedef unsigned long       u32;

#define MAX_SYS_LEN         28

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

extern void     runTestCombo(RunCombo* p, u16* paResultx2);
extern u8       getInitialDelayNI(u8 uVDP_CMD);

// from vdp.s
extern void     setVDPCmdParamsNI(u8 w, u8 h);
extern void     executeCmdWithPreppedParamsNI(u8 uCmd);
extern void     waitForVDPCmd(void);
extern u16      countWrittenPixelsNI(u16 nNumPixels); // Assume NI
extern bool     getPALRefreshRate(void);
extern void     setPALRefreshRate(bool bEnabled);
extern void     setVRAMAddress(u8 uBitCodes, u16 nVRAMAddress);
extern void     vdpSpritesEnabled(bool bEnabled);
extern void     vdpScreenEnabled(bool bEnabled);
extern void     vdpSet212Lines(bool b212);
extern void     vdpEnableLineInterruptNI(bool bEnable);
extern void     vdpSetInterruptLine(u8 uLine);

// from cycle-test.z80.s
extern u8       dispatch_vdp_test(u8 uVDP_CMD, u16 nTargetDelay); // returns CMD status after wait (bit 0 is set if busy)


// Consts --------------------------------------------------------------------
//
const u8                g_szVersion[]       = "2.0";
const u8                g_szErrorMSX[]      = "MSX2 or higher is required";
const u8                g_szTopLine[]       = "VDPCMDX v%s. screen 8, %s lines, %dHz%s, %s\r\n";
                                            //"                             " // 29 chars (turbo r)
const u8                g_szFullLine[]      = "-------------------------------------------------------------------------------\r\n";
const u8                g_szHeader1[]       = "                   NORMAL   |   NO SPR   |   NO SCR   | NORMAL+CPU | NO SCR+CPU\r\n";
// const u8                g_szHeader2[]       = " # OPERATION     THIS  REAL | THIS  REAL | THIS  REAL | THIS  REAL | THIS  REAL\r\n";
const u8                g_szHeader2[]       = " # OPERATION     THIS  %s | THIS  %s | THIS  %s | THIS  %s | THIS  %s\r\n";
const u8                g_szLastwords[]     = "1-5:landscape, 6-10:portrait, first 10:raster beam in VBLANK, last 10:ACTIVE ";
const u8                g_szResultLine[]    = "%2d %s    %5hu %5ld |%5hu %5ld |%5hu %5ld |%5hu %5ld |%5hu %5ld\r\n";
const u8                g_szREAL[]          = "REAL";
const u8                g_szDIFF[]          = "DIFF";
                                          //"                             " // 29 chars (turbo r)
const u8                g_szHelptext[]      = "Usage:vdpcmdx.com [opt][sys]\r\n"
                                            "\r\n"
                                            "Counts pixels handled by the\r\n"
                                            "VDP CMD Engine in one frame.\r\n"
                                            "Output is written to stdout.\r\n"
                                            "Unless output is redirected,\r\n"
                                            "program exits in width 80.\r\n"
                                            "\r\n"
                                            "Version: %s\r\n"
                                            "\r\n"
                                            "Options (opt):\r\n"
                                            " -h Show this help message\r\n"
                                            // " -5 Screen 5 (default: 8)\r\n"
                                            " -p PAL (default: current)\r\n"
                                            " -n NTSC (default: current)\r\n"
                                            " -l 212 lines (default: 192)\r\n"
                                            " -d Show data as diff vs REAL\r\n"
                                            "\r\n"
                                            "sys: Show sys name in report\r\n";

const u8* const         aTEST_NAME[NUM_TESTS] = \
                        {
                             "Copy LMMM"
                            ,"Copy HMMM"
                            ,"Copy YMMM"
                            ,"Fill HMMV"
                            ,"Fill LMMV"
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
                            // ,VDPCMD_LINE | LOGICAL_OP_EOR   // just random logical op (which does not become 0)
                        };

// RAM variables -------------------------------------------------------------
//
u8                      g_auBuffer[ 256 ];      // temp/general buffer here to avoid stack explosion
void* __at(0x0039)      g_pInterrupt;           // We assume that 0x0038 already holds 0xC3 (JP) in dos mode at startup
void*                   g_pInterruptOrg;
u8                      g_uFreqVariantOrg;      // NTSC or PAL
u8                      g_uFreqVariant;         // NTSC or PAL

u8                      g_auSysStr[MAX_SYS_LEN];// name of system
// u16                     g_anResult[RASTER_LOCATION_COUNT][ORIENTATION_COUNT][CONDITION_COUNT][NUM_TESTS];

u16                     g_anStartDelays[NUM_TESTS][NUM_HORIZONTALS][NUM_VERTICALS];


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
void initVarsAndRig(void)
{
    strcpy(g_auSysStr,"<system/model name not set>");

    // g_uFreqVariantOrg = getPALRefreshRate()? PAL : NTSC;
    // g_uFreqVariant = g_uFreqVariantOrg;
    // memset(g_anResult, 0, sizeof(g_anResult));
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
                g_anStartDelays[c][u][v] = (u16)getInitialDelayNI(aEXECUTE_CMD[c]) * INITIAL_LOOP_CYCLES;
            }
        }
    }

    enableInterrupt();
}

// ---------------------------------------------------------------------------
// u16 runTestSingle(u8 uOrientation, u8 nTest, bool bUseCPU, bool bVBlank)
u16 runTestSingle(u8 uTest)
{
    // PREPARE PARAMS
    // disableInterrupt();
    // setVDPCmdParamsNI(8,8);
    // enableInterrupt();



    vdpScreenEnabled(false);







    // u16 uCMD_status = dispatch_vdp_test(aEXECUTE_CMD[0], 33);



    // runTestCombo();

    vdpScreenEnabled(true);

    
    // return uCMD_status;
    return 0;
}

// ---------------------------------------------------------------------------
void runTests(void)
{
    vdpScreenEnabled(false);

    establishInitialDelays();

    runTestSingle(0);

    // for(u8 uOrientation = 0; uOrientation < ORIENTATION_COUNT; uOrientation++)
    //     for(u8 n = 0; n < NUM_TESTS; n++)
    //         for(u8 v = 0; v < RASTER_LOCATION_COUNT; v++)
    //             g_anResult[v][uOrientation][n][uCondition] = runTestSingle(uOrientation, n, uCondition >= NORMAL_CPU, v == VBLANK);
}

// ---------------------------------------------------------------------------
void printReport(void)
{
    // sprintf(g_auBuffer,
    //         g_szTopLine,
    //         g_szVersion,
    //         b212Set? "212" : "192",
    //         g_uFreqVariant==PAL ? 50 : 60,
    //         (!bPALSet && !bNTSCSet) ? " (detected)" : "",
    //         g_auSysStr
    //        );

    // print(g_auBuffer);

    // print(g_szHeader1);

    // const u8* p = bDiffMode? g_szDIFF : g_szREAL;
    // sprintf(g_auBuffer, g_szHeader2, p, p, p, p, p);
    // print(g_auBuffer);

    // print(g_szFullLine);


    // s32 lShowNumber[CONDITION_COUNT];

    // u8 uLineVariant = b212Set? EXTENDED212 : NORMAL192;

    // for(u8 r = 0; r<RASTER_LOCATION_COUNT; r++)
    // {
    //     u8 uLine = 1;

    //     for(u8 o = 0; o < ORIENTATION_COUNT; o++)
    //     {
    //         for(u8 t = 0; t < NUM_TESTS; t++)
    //         {
    //             for(u8 c = 0; c < CONDITION_COUNT; c++) // set the correct number
    //             {
    //                 if(bDiffMode)
    //                     lShowNumber[c] = (s32)(g_anResult[r][o][t][c]) - (s32)(aTARGETS[uLineVariant][r][g_uFreqVariant][o][t][c]);
    //                 else
    //                     lShowNumber[c] = (s32)(aTARGETS[uLineVariant][r][g_uFreqVariant][o][t][c]);
    //             }

    //             sprintf(g_auBuffer,
    //                     g_szResultLine,
    //                     uLine,
    //                     aTEST_NAME[t],
    //                     g_anResult[r][o][t][NORMAL],
    //                     lShowNumber[NORMAL],
    //                     g_anResult[r][o][t][NO_SPRITES],
    //                     lShowNumber[NO_SPRITES],
    //                     g_anResult[r][o][t][NO_SCREEN],
    //                     lShowNumber[NO_SCREEN],
    //                     g_anResult[r][o][t][NORMAL_CPU],
    //                     lShowNumber[NORMAL_CPU],
    //                     g_anResult[r][o][t][NO_SCREEN_CPU],
    //                     lShowNumber[NO_SCREEN_CPU]
    //                 );

    //             print(g_auBuffer);
    //             uLine++;
    //         }
    //     }
    // }

    // print(g_szFullLine);
    // print(g_szLastwords);
}

// ---------------------------------------------------------------------------
void printHelp(void)
{
    sprintf(g_auBuffer, g_szHelptext, g_szVersion);
    print(g_auBuffer);
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
            if(strcmp(argv[n], "-h") == 0 || strcmp(argv[n], "--help") == 0)
            {
                printHelp();
                return 0;
            }
            else if(strcmp(argv[n], "-p") == 0)
            {
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
            changeCPU(0); // 0=Z80 (ROM) mode, 1=R800 ROM  mode, 2=R800 DRAM mode
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

    bool hasScreenOutput = userOutputsToScreen();

    setCustomISR(); 

    u8 uPrevScrMode = changeMode(SCREEN_MODE);

    vdpSet212Lines(false); // just to make sure
    // vdpSetInterruptLine(0); // 
    vdpEnableLineInterruptNI(false); // to be sure

    // ---------------------------------------------
    // READY! Set conditions for tests and run tests
    vdpSpritesEnabled(true);
    vdpScreenEnabled(true);
    runTests();


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

    // ----------------------------------
    // Show summary
    printReport();

    if(sOrgCPU != -1)
        changeCPU(sOrgCPU);

    if(bRestoreTurbo)
        enableTurbo(true);

    return 0;
}