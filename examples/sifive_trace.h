/* Copyright 2021 SiFive, Inc */
/* This is a C macro library to control trace on the target */
/* Supports single/multi cores, TE/CA/TF traces */

#include <stdint.h>
#include <stdio.h>

#ifndef SIFIVE_TRACE_H_
#define SIFIVE_TRACE_H_

/*==========================================================================================
Value Replacement Macros
==========================================================================================*/

// Values for intruction selection
#define TE_INSTRUCTION_NONE        0
#define TE_INSTRUCTION_EVENT       1
#define TE_INSTRUCTION_BTMSYNC     3
#define TE_INSTRUCTION_HTM         6
#define TE_INSTRUCTION_HTMSYNC     7

// Values for instrumentation selection
#define TE_INSTRUMENTATION_NONE               0
#define TE_INSTRUMENTATION_ITC                1
#define TE_INSTRUMENTATION_OWNERSHIP          2
#define TE_INSTRUMENTATION_OWNERSHIPALL       3

// Macros for the sync max BTM value assignment
#define TE_SYNCMAXBTM_32           0
#define TE_SYNCMAXBTM_64           1
#define TE_SYNCMAXBTM_128          2
#define TE_SYNCMAXBTM_256          3
#define TE_SYNCMAXBTM_512          4
#define TE_SYNCMAXBTM_1024         5
#define TE_SYNCMAXBTM_2048         6
#define TE_SYNCMAXBTM_4096         7
#define TE_SYNCMAXBTM_8192         8
#define TE_SYNCMAXBTM_16348        9
#define TE_SYNCMAXBTM_32768        10
#define TE_SYNCMAXBTM_65536        11
#define TE_SYNCMAXBTM_OFF          15

// Macros for the sync max instruction value assignment
#define TE_SYNCMAXINST_32          0
#define TE_SYNCMAXINST_64          1
#define TE_SYNCMAXINST_128         2
#define TE_SYNCMAXINST_256         3
#define TE_SYNCMAXINST_512         4
#define TE_SYNCMAXINST_1024        5
#define TE_SYNCMAXINST_OFF         15

// Values for selecting a core's sink
#define TE_SINK_DEFAULT            0
#define TE_SINK_SRAM               4
#define TE_SINK_ATB                5
#define TE_SINK_PIB                6
#define TE_SINK_SBA                7
#define TE_SINK_FUNNEL             8

//  Macros for the timestamp prescale values assignemnt
#define TS_PRESCL_1                0
#define TS_PRESCL_4                1
#define TS_PRESCL_16               2
#define TS_PRESCL_64               3

// values for selecting a core's branch messaging
#define BRNCH_OFF                  0
#define BRNCH_INDT                 1
#define BRNCH_EXPT                 1
#define BRNCH_ALL                  3

// Values to enable/dissable trace functions
#define TRACE_ENABLE               1
#define TRACE_DISABLE              0

// vales to define ITC functionality
#define ITC_OFF                    0
#define ITC_ALL                    1
#define ITC_OWN                    2
#define ITC_ALL_OWN                3

// Values to turn tracing on/off or reset
#define TRACE_ON                   0
#define TRACE_OFF                  1
#define TRACE_RESET                2

// Used for setting the maximnum number of BTMs between sync messages
#define SYNC_MAX_BTM_32            0
#define SYNC_MAX_BTM_64            1
#define SYNC_MAX_BTM_128           2
#define SYNC_MAX_BTM_256           3
#define SYNC_MAX_BTM_512           4
#define SYNC_MAX_BTM_1024          5
#define SYNC_MAX_BTM_2048          6
#define SYNC_MAX_BTM_4096          7
#define SYNC_MAX_BTM_8192          8
#define SYNC_MAX_BTM_16384         9
#define SYNC_MAX_BTM_32768         10
#define SYNC_MAX_BTM_65536         11

// Used for setting the maximnum number of I-CNTs between sync messages
#define SYNC_MAX_INST_32           0
#define SYNC_MAX_INST_64           1
#define SYNC_MAX_INST_128          2
#define SYNC_MAX_INST_256          3
#define SYNC_MAX_INST_512          4
#define SYNC_MAX_INST_1024         5
#define SYNC_MAX_INST_2048         6
#define SYNC_MAX_INST_4096         7

// Used for selecting the slice format (one option)
#define SLICE_FORMAT_6MDO_2MSEO    1

// Used for selecting the time stamp clock source
#define TS_NONE                    0
#define TS_CLK_EXTERN              1
#define TS_CLK_BUS                 2
#define TS_CLK_CORE                3
#define TS_CLK_SLAVE               4

// Used for selecting XTI actions
#define XTI_ACTION_NONE            0
#define XTI_ACTION_START           2
#define XTI_ACTION_STOP            3
#define XTI_ACTION_SYNC            4

// Used for selecting XTO events
#define XTO_ACTION_STARTING        0
#define XTO_ACTION_STOPPING        1
#define XTO_ACTION_ITC             2
#define XTO_ACTION_WATCHPOINT      3

// Used for addressing all mapped cores with one macro call
#define CORE_COUNT                 (sizeof(tmm)/sizeof(tmm[0]))
#define TRACE_CORES_ALL            CORE_COUNT

// Values used for enabling/disabling or clearing bits in various registesr
#define TE_ENABLE                  1
#define TE_DISABLE                 0
#define TE_CLEAR                   0

// Internal Use Only
// Used for referencing PC registers
#define _PC_CONTROL                0
#define _PC_CAPTURE                15
#define _PC_CAPTURE_HIGH           14
#define _PC_SAMPLE                 31
#define _PC_SAMPLE_HIGH            30



/*==========================================================================================
Create Memory Map for Register Structures
==========================================================================================*/

union ITC_Stimulus{
    uint32_t reg_size_32;
    uint16_t reg_size_16;
    uint8_t reg_size_8;
};

struct TraceRegMemMap {
    uint32_t te_control_register;                   //   (0x00)
    uint32_t te_impl_register;                      //   (0x04)
    uint32_t reserved_08;                           //   (0x08)
    uint32_t ev_control_register;                   //   (0x0c)
    uint32_t te_sinkbase_register;                  //   (0x10)
    uint32_t te_sinkbasehigh_register;              //   (0x14)
    uint32_t te_sinklimit_register;                 //   (0x18)
    uint32_t te_sink_wp_register;                   //   (0x1c)
    uint32_t te_sink_rp_register;                   //   (0x20)
    uint32_t te_sink_data_register;                 //   (0x24)
    uint32_t reserved_28_2c[2];                     //   (0x28-0x2c)
    uint32_t te_fifo_register;                      //   (0x30)
    uint32_t te_btmcount_register;                  //   (0x34)
    uint32_t te_wordcount_register;                 //   (0x38)
    uint32_t reserved_3c;                           //   (0x3c)
    uint32_t ts_control_register;                   //   (0x40)
    uint32_t ts_lower_register;                     //   (0x44)
    uint32_t ts_upper_register;                     //   (0x48)
    uint32_t reserved_4c;                           //   (0x4c)
    uint32_t xti_control_register;                  //   (0x50)
    uint32_t xto_control_register;                  //   (0x54)
    uint32_t wp_control_register;                   //   (0x58)
    uint32_t reserved_5c;                           //   (0x5c)
    uint32_t itc_traceenable_register;              //   (0x60)
    uint32_t itc_trigenable_register;               //   (0x64)
    uint32_t reserved_68_7c[6];                     //   (0x68-0x7c)
    union {
        uint32_t reg_size_32;
        struct {
            uint16_t reserved16;
            uint16_t reg_size_16;
        };
        struct {
            uint8_t reserved8[3];
            uint8_t reg_size_8;
        };
    } itc_stimulus_register[32];                    //   (0x80-0xFF)
    uint32_t pc_sampling_register[32];              //   (0x100-0x1ff)
    uint32_t reserved_200_EFF[767];                 //   (0x200-0xDFF)
    uint32_t atb_control_register;                  //   (0xE00)
    uint32_t reserved_E04_Eff[63];                  //   (0xE04-0xEFF)
    uint32_t pib_control_register;                  //   (0xF00)
};

struct CaTraceRegMemMap{
    uint32_t ca_control_register;                   //  (0x00)
    uint32_t ca_impl_register;                      //  (0x04)
    uint32_t reserved_08;                           //  (0x08)
    uint32_t reserved_0c;                           //  (0x0c)
    uint32_t reserved_10;                           //  (0x10)
    uint32_t reserved_14;                           //  (0x14)
    uint32_t reserved_18;                           //  (0x18)
    uint32_t ca_sink_wp_register;                   //  (0x1c)
    uint32_t ca_sink_rp_register;                   //  (0x20)
    uint32_t ca_sink_data_register;                 //  (0x24)
};

struct TfTraceRegMemMap{
    uint32_t tf_control_register;                   //  (0x00)
    uint32_t tf_impl_register;                      //  (0x04)
    uint32_t reserved_08;                           //  (0x08)
    uint32_t reserved_0c;                           //  (0x0c)
    uint32_t tf_sinkbase_register;                  //  (0x10)
    uint32_t tf_sinkbasehigh_register;              //  (0x14)
    uint32_t tf_sinklimit_register;                 //  (0x18)
    uint32_t tf_sink_wp_register;                   //  (0x1c)
    uint32_t tf_sink_rp_register;                   //  (0x20)
    uint32_t tf_sink_data_register;                 //  (0x24)
    uint32_t reserved_28_DFF[885];                  //  (0x28-0xDFF)
    uint32_t atb_control_register;                  //  (0xE00)
    uint32_t reserved_E04_Eff[63];                  //  (0xE04-0xEFF)
    uint32_t pib_control_register;                  //  (0xF00)
};


/*==========================================================================================
Create the register map objects.

TraceRegMemMap, CaTraceRegMemMap, and tfTraceRefMemMap need to be declared in the
user program. They are shown below as a comment for an example.
==========================================================================================*/
/*
#define traceBaseAddress 0
// create the trace memory map object
struct TraceRegMemMap volatile * const tmm[] = {(struct TraceRegMemMap*)traceBaseAddress};

#define caBaseAddress 0
// create the cycle accurate trace memory map object
struct CaTraceRegMemMap volatile * const cmm[] = {(struct CaTraceRegMemMap*)caBaseAddress};

#define tfBaseAddress 0
// create the trace funnel memory map object
struct TfTraceRegMemMap volatile * const fmm = (struct TfTraceRegMemMap*)tfBaseAddress;
*/

/*==========================================================================================
Register Manipulation
==========================================================================================*/
// some generic, entire register macros
// might not be needed based on other macros written below
#define _writeTEReg(core, register, value)  (tmm[core]->register = (value))
#define _writeCAReg(core, register, value)  (cmm[core]->register = (value))
#define _writeTFReg(register, value)        (fmm->register = (value))

#define _readTEReg(core, register)          (tmm[core]->register)
#define _readCAReg(core, register)          (cmm[core]->register)
#define _readTFReg(register)                (fmm->register)

// The _get and _set macros below all follow the same format
// thus these generic macros allow just the bit range and value
// to be provided, simplyfying the below macros
#define _setGeneric(register, andMask, opt, bit)    ((register) = ((register) & ~(andMask)) | (opt) << (bit))

#define _getGeneric(register, andMask, bit)         (((register) & (andMask)) >> (bit))

// Create the set of macros to set any bit in the Te_control register
#define setTeControl(core, value)       (tmm[core]->te_control_register = (value))
#define getTeControl(core)              (tmm[core]->te_control_register)

#define setTeActive(core, opt)          (_setGeneric(getTeControl(core), 0x1, (opt), 0x0))
#define getTeActive(core)               (_getGeneric(getTeControl(core), 0x1, 0x0))

#define setTeEnable(core, opt)          (_setGeneric(getTeControl(core), 0x2, (opt), 0x1))
#define getTeEnable(core)               (_getGeneric(getTeControl(core), 0x2, 0x1))

#define setTeTracing(core, opt)         (_setGeneric(getTeControl(core), 0x4, (opt), 0x2))
#define getTeTracing(core)              (_getGeneric(getTeControl(core), 0x4, 0x2))

#define setTeEmpty(core, opt)           (_setGeneric(getTeControl(core), 0x8, (opt), 0x3))
#define getTeEmpty(core)                (_getGeneric(getTeControl(core), 0x8, 0x3))

#define setTeInstruction(core, opt)     (_setGeneric(getTeControl(core), 0x70, (opt), 0x4))
#define getTeInstruction(core)          (_getGeneric(getTeControl(core), 0x70, 0x4))

#define setTeInstrumentation(core, opt)  (_setGeneric(getTeControl(core), 0x180, (opt), 0x7))
#define getTeInstrumentation(core)       (_getGeneric(getTeControl(core), 0x180, 0x7))

#define setTeStallOrOverflow(core, opt) (_setGeneric(getTeControl(core), 0x1000, (opt), 0xC))
#define getTeStallOrOverflow(core)      (_getGeneric(getTeControl(core), 0x1000, 0xC))

#define setTeStallEnable(core, opt)     (_setGeneric(getTeControl(core), 0x2000, (opt), 0xD))
#define getTeStallEnable(core)          (_getGeneric(getTeControl(core), 0x2000, 0xD))

#define setTeStopOnWrap(core, opt)      (_setGeneric(getTeControl(core), 0x4000, (opt), 0xE))
#define getTeStopOnWrap(core)           (_getGeneric(getTeControl(core), 0x4000, 0xE))

#define setTeInhibitSrc(core, opt)      (_setGeneric(getTeControl(core), 0x8000, (opt), 0xF))
#define getTeInhibitSrc(core)           (_getGeneric(getTeControl(core), 0x8000, 0xF))

#define setTeSyncMaxBtm(core, opt)      (_setGeneric(getTeControl(core), 0xF0000, (opt), 0x10))
#define getTeSyncMaxBtm(core)           (_getGeneric(getTeControl(core), 0xF0000, 0x10))

#define setTeSyncMaxInst(core, opt)     (_setGeneric(getTeControl(core), 0xF00000, (opt), 0x14))
#define getTeSyncMaxInst(core)          (_getGeneric(getTeControl(core), 0xF00000, 0x14))

#define setTeSliceFormat(core, opt)     (_setGeneric(getTeControl(core), 0x7000000, (opt), 0x18))
#define getTeSliceFormat(core)          (_getGeneric(getTeControl(core), 0x7000000, 0x18))

#define setTeSinkError(core, opt)       (_setGeneric(getTeControl(core), 0x8000000, (opt), 0x1B))
#define getTeSinkError(core)            (_getGeneric(getTeControl(core), 0x8000000, 0x1B))

#define setTeSink(core, opt)            (_setGeneric(getTeControl(core), 0xF0000000, (opt), 0x1C))
#define getTeSink(core)                 (_getGeneric(getTeControl(core), 0xF0000000, 0x1C))

// Misc TE registers -> Read Only
#define getTeFifo(core)                 (tmm[core]->te_fifo_register)

#define getTeBtmCnt(core)               (tmm[core]->te_btmcount_register)

#define getTeWordCnt(core)              (tmm[core]->te_wordcount_register)

#define getTeWp(core)                   (tmm[core]->wp_control_register)

// The te_impl register
// Read Only
#define getTeImpl(core)(tmm[core]->te_impl_register)

#define getTeImplVersion(core)          (_getGeneric(getTeImpl(core), 0xF, 0x0))

#define getTeImplHasSRAMSink(core)      (_getGeneric(getTeImpl(core), 0x10, 0x4))

#define getTeImplHasATBSink(core)       (_getGeneric(getTeImpl(core), 0x20, 0x5))

#define getTeImplHasPIBSink(core)       (_getGeneric(getTeImpl(core), 0x40, 0x6))

#define getTeImplHasSBASink(core)       (_getGeneric(getTeImpl(core), 0x80, 0x7))

#define getTeImplHasFunnelSink(core)    (_getGeneric(getTeImpl(core), 0x100, 0x8))

#define getTeImplSinkBytes(core)        (_getGeneric(getTeImpl(core), 0x30000, 0x10))

#define getTeImplCrossingType(core)     (_getGeneric(getTeImpl(core), 0xC0000, 0x12))

#define getTeImplNSrcBits(core)         (_getGeneric(getTeImpl(core), 0x7000000, 0x18))

#define getTeImplHartId(core)           (_getGeneric(getTeImpl(core), 0xF0000000, 0x1C))

#define getTeImplSrcId(core)            (_getGeneric(getTeImpl(core), 0xF00000, 0x14))

// The te_SinkBase register
#define setTeSinkBase(core, value)      (tmm[core]->te_sinkbase_register = (value))
#define getTeSinkBase(core)             (tmm[core]->te_sinkbase_register)

// The te_SinkBaseHigh register
#define setTeSinkBaseHigh(core, value)  (tmm[core]->te_sinkbasehigh_register = (value))
#define getTeSinkBaseHigh(core)         (tmm[core]->te_sinkbasehigh_register)

// The te_SinkLimit register
#define setTeSinkLimit(core, value)     (tmm[core]->te_sinklimit_register = (value))
#define getTeSinkLimit(core)            (tmm[core]->te_sinklimit_register)

// The te_SinkWP register
#define getTeSinkWp(core)            (tmm[core]->te_sink_wp_register)
#define setTeSinkWp(core, opt)       (tmm[core]->te_sink_wp_register = (opt))

#define getTeSinkWrap(core)             (_getGeneric(tmm[core]->te_sink_wp_register,    \
                                         0x1, 0x0)
#define setTeSinkWrap(core, opt)        (_setGeneric(tmm[core]->te_sink_wp_register,    \
                                         0x1, opt, 0x0))

// The te_SinkRP register
#define getTeSinkRp(core)            (tmm[core]->te_sink_rp_register)
#define setTeSinkRp(core, opt)       (tmm[core]->te_sink_wp_register = (opt))

// The te_SinkData register
#define setTeSinkData(core, value)      (tmm[core]->te_sink_data_register = (value))
#define getTeSinkData(core)             (tmm[core]->te_sink_data_register)

// The itc_traceenable register
#define setITCTraceEnable(core, value)  (tmm[core]->itc_traceenable_register = (value))
#define getITCTraceEnable(core)         (tmm[core]->itc_traceenable_register)

// The itc_trigenable register
#define setITCTrigEnable(core, value)   (tmm[core]->itc_trigenable_register = (value))
#define getITCTrigEnable(core)          (tmm[core]->itc_trigenable_register)

// The  ITC Stimulus register
#define getITCStimulus(core, regnum)    (tmm[core]->itc_stimulus_register[(regnum)].reg_size_32)

#define setITCStimulus32(core, regnum, value){                              \
    while(getITCStimulus(core, regnum) == 0){}                              \
    tmm[core]->itc_stimulus_register[(regnum)].reg_size_32 = (value);       \
}

#define setITCStimulus16(core, regnum, value){                              \
    while(getITCStimulus(core, regnum) == 0){}                              \
    tmm[core]->itc_stimulus_register[(regnum)].reg_size_16 = (value);       \
}

#define setITCStimulus8(core, regnum, value){                               \
    while(getITCStimulus(core, regnum) == 0){}                              \
    tmm[core]->itc_stimulus_register[(regnum)].reg_size_8 = (value);        \
}

#define setITCMask(core, mask)          (tmm[core]->itc_traceenable_register = (mask))
#define getITCMask(core)                (tmm[core]->itc_traceenable_register)

#define setITCTrigMask(core, mask)      (tmm[core]->itc_trigenable_register = (mask))
#define getITCTrigMask(core)            (tmm[core]->itc_trigenable_register)


#define setITC(core, bmask, tmask){                                         \
    setITCMask(core, bmask);                                                \
    setITCTrigMask(core, tmask);                                            \
}

// The ts control register
#define setTsControl(core, value)       (tmm[core]->ts_control_register = (value))
#define getTsControl(core)              (tmm[core]->ts_control_register)

#define setTsActive(core, opt)          (_setGeneric(getTsControl(core), 0x1, (opt), 0x0))
#define getTsActive(core)               (_getGeneric(getTsControl(core), 0x1, 0x0))

#define setTsCount(core, opt)           (_setGeneric(getTsControl(core), 0x2, (opt), 0x1))
#define getTsCount(core)                (_getGeneric(getTsControl(core), 0x2, 0x1))

#define setTsReset(core, opt)           (_setGeneric(getTsControl(core), 0x4, (opt), 0x2))
#define getTsReset(core)                (_getGeneric(getTsControl(core), 0x4, 0x0))

#define setTsDebug(core, opt)           (_setGeneric(getTsControl(core), 0x8, (opt), 0x3))
#define getTsDebug(core)                (_getGeneric(getTsControl(core), 0x8, 0x3))

#define setTsType(core, opt)            (_setGeneric(getTsControl(core), 0x70, (opt), 0x4))
#define getTsType(core)                 (_getGeneric(getTsControl(core), 0x70, 0x4))

#define setTsPrescale(core, opt)        (_setGeneric(getTsControl(core), 0x300, (opt), 0x8))
#define getTsPrescale(core)             (_getGeneric(getTsControl(core), 0x300, 0x8))

#define setTsEnable(core, opt)          (_setGeneric(getTsControl(core), 0x8000, (opt), 0xF))
#define getTsEnable(core)               (_getGeneric(getTsControl(core), 0x8000, 0xF))

#define setTsBranch(core, opt)          (_setGeneric(getTsControl(core), 0x30000, (opt), 0x10))
#define getTsBranch(core)               (_getGeneric(getTsControl(core), 0x30000, 0x10))

#define setTsInstrumentation(core, opt) (_setGeneric(getTsControl(core), 0x40000, (opt), 0x12))
#define getTsInstrumentation(core)      (_getGeneric(getTsControl(core), 0x40000, 0x12))

#define setTsOwnership(core, opt)       (_setGeneric(getTsControl(core), 0x80000, (opt), 0x13))
#define getTsOwnership(core)            (_getGeneric(getTsControl(core), 0x80000, 0x13))

#define setTsWidth(core, opt)           (_setGeneric(getTsControl(core), 0xFF000000, (opt), 0x18))
#define getTsWidth(core)                (_getGeneric(getTsControl(core), 0xFF000000, 0x18))

// The ts lower register
#define setTsLower(core, value)         (tmm[core]->ts_lower_register = (value))
#define getTsLower(core)                (tmm[core]->ts_lower_register)

// The ts upper register
#define setTsUpper(core, value)         (tmm[core]->ts_upper_register = (value))
#define getTsUpper(core)                (tmm[core]->ts_upper_register)

// need to cacatonate the lower and upper ts registers together
#define getTsFull(core)                 ((((uint64_t) tmm[core]->ts_upper_register) << 32) |         \
                                         ((uint64_t)tmm[core]->ts_lower_register))

// the xti register
#define getXtiReg(core)                 (tmm[core]->xti_control_register)
#define setXtiReg(core, value)          (tmm[core]->xti_control_register = (value))

#define getXtiAction(core, n)           ( (getXtiReg(core) & (0xF << ((n)*4))) >> ((n)*4) )
#define setXtiAction(core, n, opt)      ( getXtiReg(core) = (getXtiReg(core) & ~(0xF << ((n)*4))) | (opt) << ((n)*4) )

// The xto register
#define getXtoReg(core)                 (tmm[core]->xto_control_register)
#define setXtoReg(core, value)          (tmm[core]->xto_control_register = (value))

#define getXtoEvent(core, n)            ( (getXtoReg(core) & (0xF << ((n)*4))) >> ((n)*4) )
#define setXtoEvent(core, n, opt)       ( getXtoReg(core) = (getXtoReg(core) & ~(0xF << ((n)*4))) | (opt) << ((n)*4) )

// The wp control register
#define getWpReg(core)                  (tmm[core]->wp_control_register)
#define setWpReg(core, value)           (tmm[core]->wp_control_register = (value))

#define getWp(core, n)                  ( (getWpReg(core) & (0xF << ((n)*4))) >> ((n)*4) )
#define setWp(core, n, opt)             ( getWpReg(core) = (getWpReg(core) & ~(0xF << ((n)*4))) | (opt) << ((n)*4) )

// The PC Control Register
#define setPcControl(core, value)       (tmm[core]->pc_sampling_register[_PC_CONTROL] = (value))
#define getPcControl(core)              (tmm[core]->pc_sampling_register[_PC_CONTROL])

#define setPcActive(core, opt)          (_setGeneric(tmm[core]->pc_sampling_register[_PC_CONTROL], 0x1, (opt), 0x0)
#define getPcActive(core)               (_getGeneric(tmm[core]->pc_sampling_register[_PC_CONTROL], 0x1, 0x0))

// The PC Capture Register
#define getPcCapture(core)              (tmm[core]->pc_sampling_register[_PC_CAPTURE])

#define getPcCaptureValid(core)         (_getGeneric(tmm[core]->pc_sampling_register[_PC_CONTROL], 0x1, 0x0))

#define getPcCaptureAddress(core)       (_getGeneric(tmm[core]->pc_sampling_register[_PC_CONTROL], ~0x1, 0x1))

// The PC Capture High Register
#define getPcCaptureHigh(core)          (tmm[core]->pc_sampling_register[_PC_CAPTURE_HIGH])

// The PC Sample Register
#define getPcSampleReg(core)            (tmm[core]->pc_sampling_register[_PC_SAMPLE])

#define getPcSampleValid(core)          (_getGeneric(tmm[core]->pc_sampling_register[_PC_SAMPLE], 0x1, 0x0))

#define getPcSample(core)               (_getGeneric(tmm[core]->pc_sampling_register[_PC_SAMPLE], ~0x1, 0x1))

// The PC Sample High Register
#define getPcSampleHigh(core)           (tmm[core]->pc_sampling_register[_PC_SAMPL_HIGH])

// The tf control register
#define getTfControl()                  (fmm->tf_control_register)
#define setTfControl(value)             (fmm->tf_control_register = (value))

#define setTfActive(opt)                (_setGeneric(fmm->tf_control_register, 0x1, (opt), 0x0))
#define getTfActive()                   (_getGeneric(fmm->tf_control_register, 0x1, 0x0))

#define setTfEnable(opt)                (_setGeneric(fmm->tf_control_register, 0x2, (opt), 0x1))
#define getTfEnable()                   (_getGeneric(fmm->tf_control_register, 0x2, 0x1))

#define setTfEmpty(opt)                 (_setGeneric(fmm->tf_control_register, 0x8, (opt), 0x3))
#define getTfEmpty()                    (_getGeneric(fmm->tf_control_register, 0x8, 0x3))

#define setTfStopOnWrap(opt)            (_setGeneric(fmm->tf_control_register, 0x4000, (opt), 0xE))
#define getTfStopOnWrap()               (_getGeneric(fmm->tf_control_register, 0x4000, 0xE))

#define setTfSinkError(opt)             (_setGeneric(fmm->tf_control_register, 0x8000000, (opt), 0x1B))
#define getTfSinkError()                (_getGeneric(fmm->tf_control_register, 0x8000000, 0x1B))

#define setTfSink(opt)                  (_setGeneric(fmm->tf_control_register, 0xF0000000, (opt), 0x1C))
#define getTfSink()                     (_getGeneric(fmm->tf_control_register, 0xF0000000, 0x1C))

// The tf impl register
#define getTfImpl()                     (fmm->tf_control_register)

#define getTfVersion()                  (_getGeneric(getTfImpl(), 0xF, 0x0))

#define getTfHasSRAMSink()              (_getGeneric(getTfImpl(), 0x10, 0x4))

#define getTfHasATBSink()               (_getGeneric(getTfImpl(), 0x20, 0x5))

#define getTfHasPIBSink()               (_getGeneric(getTfImpl(), 0x40, 0x6))

#define getTfHasSBASink()               (_getGeneric(getTfImpl(), 0x80, 0x7))

#define getTfHasFunnelSink()            (_getGeneric(getTfImpl(), 0x100, 0x8))

#define getTfSinkBytes()                (_getGeneric(getTfImpl(), 0x30000, 0x10))

// The Tf Sink Registers
#define getTfSinkWp()                   (fmm->tf_sink_wp_register)
#define setTfSinkWp(value)              (fmm->tf_sink_wp_register = (value))

#define getTfSinkRp()                   (fmm->tf_sink_rp_register)
#define setTfSinkRp(value)              (fmm->tf_sink_rp_register = (value))

#define getTfSinkData()                 (fmm->tf_sink_data_register)
#define setTfSinkData(value)            (fmm->tf_sink_data_register = (value))

// The atb control register - Trace Encoder
#define setTeAtbControl(core, value)    (tmm[core]->atb_control_register = (value))
#define getTeAtbControl(core)           (tmm[core]->atb_control_register)

#define setTeAtbActive(core, opt)       (_setGeneric(getTeAtbControl(core), 0x1, (opt), 0x0))
#define getTeAtbctive(core)             (_getGeneric(getTeAtbControl(core), 0x1, 0x0))

#define setTeAtbEnable(core, opt)       (_setGeneric(getTeAtbControl(core), 0x2, (opt), 0x1))
#define getTeAtbEnable(core)            (_getGeneric(getTeAtbControl(core), 0x2, 0x1))

#define getTeAtbEmpty(core)             (_getGeneric(getTeAtbControl(core), 0x4, 0x2))

// The pib control register - Trace Encoder
#define setTePibControl(core, value)    (tmm[core]->pib_control_register = value)
#define getTePibControl(core)           (tmm[core]->pib_control_register)

#define setTePibActive(core, opt)       (_setGeneric(getTePibControl(core), 0x1, (opt), 0x0))
#define getTePibctive(core)             (_getGeneric(getTePibControl(core), 0x1, 0x0))

#define setTePibEnable(core, opt)       (_setGeneric(getTePibControl(core), 0x2, (opt), 0x1))
#define getTePibEnable(core)            (_getGeneric(getTePibControl(core), 0x2, 0x1))

#define getTePibEmpty(core)             (_getGeneric(getTePibControl(core), 0x4, 0x2))

// The atb control register - Funnel
#define setTfAtbControl(value)          (fmm->atb_control_register = (value))
#define getTfAtbControl()               (fmm->atb_control_register)

#define setTfAtbActive(opt)             (_setGeneric(getTfAtbControl(), 0x1, (opt), 0x0))
#define getTfAtbctive()                 (_getGeneric(getTfAtbControl(), 0x1, 0x0))

#define setTfAtbEnable(opt)             (_setGeneric(getTfAtbControl(), 0x2, (opt), 0x1))
#define getTfAtbEnable()                (_getGeneric(getTfAtbControl(), 0x2, 0x1))

#define getTfAtbEmpty()                 (_getGeneric(getTfAtbControl(), 0x4, 0x2))

// The pib control register - Funnel
#define setTfPibControl(value)          (fmm->pib_control_register = (value))
#define getTfPibControl()               (fmm->pib_control_register)

#define setTfPibActive(opt)             (_setGeneric(getTfPibControl(), 0x1, (opt), 0x0))
#define getTfPibctive()                 (_getGeneric(getTfPibControl(), 0x1, 0x0))

#define setTfPibEnable(opt)             (_setGeneric(getTfPibControl(), 0x2, (opt), 0x1))
#define getTfPibEnable()                (_getGeneric(getTfPibControl(), 0x2, 0x1))

#define getTfPibEmpty()                 (_getGeneric(getTfPibControl(), 0x4, 0x2))

// The Now we need to do the same for the tf_SinkBase register
#define setTfSinkBase(value)            (fmm->tf_sinkbase_register = (value))
#define getTfSinkBase()                 (fmm->tf_sinkbase_register)

// The tf_SinkBaseHigh register
#define setTfSinkBaseHigh(value)        (fmm->tf_sinkbasehigh_register = (value))
#define getTfSinkBaseHigh()             (fmm->tf_sinkbasehigh_register)

// The tf_SinkLimit register
#define setTfSinkLimit(value)           (fmm->tf_sinklimit_register = (value))
#define getTfSinkLimit()                (fmm->tf_sinklimit_register)

// The CA Control Register
#define setCaControl(core, value)       (cmm[core]->ca_control_register = (value))
#define getCaControl(core)              (cmm[core]->ca_control_register)

#define setCaActive(core, opt)          (_setGeneric(getCaControl(core), 0x1, (opt), 0x0))
#define getCaActive(core)               (_getGeneric(getCaControl(core), 0x1, 0x0))

#define setCaEnable(core, opt)          (_setGeneric(getCaControl(core), 0x2, (opt), 0x1))
#define getCaEnable(core)               (_getGeneric(getCaControl(core), 0x2, 0x1))

// Read Only
#define getCaTracing(core)              (_getGeneric(getCaControl(core), 0x4, 0x2))

// Read Only
#define getCaEmpty(core)                (_getGeneric(getCaControl(core), 0x8, 0x3))

// Write Only
#define setCaStopOnWrap(core, opt)      (_setGeneric(getCaControl(core), 0x4000, (opt), 0xE))

#define setCaSink(core, opt)            (_setGeneric(getCaControl(core), 0xF0000000, (opt), 0x1C))
#define getCaSInk(core)                 (_getGeneric(getCaControl(core), 0xF0000000, 0x1C))

// The CA Impl Register -- Read only
#define getCaImpl(core)                 (cmm[core]->ca_control_register)

#define getCaVersion(core)              (_getGeneric(getCaImpl(core), 0xF, 0x0))

#define getCaHasSramSink(core)          (_getGeneric(getCaImpl(core), 0x10, 0x4))

#define getCaHasAtbSink(core)           (_getGeneric(getCaImpl(core), 0x11, 0x5))

#define getCaHasPibSink(core)           (_getGeneric(getCaImpl(core), 0x12, 0x6))

#define getCaHasSbaSink(core)           (_getGeneric(getCaImpl(core), 0x13, 0x7))

#define getCaHasFunnelSink(core)        (_getGeneric(getCaImpl(core), 0x14, 0x8))

#define getCaSinkBytes(core)            (_getGeneric(getCaImpl(core), 0x30000, 0x10))

#define getCaSinkData(core)             (cmm[core]->ca_sink_data_register)

// The CA Sink WP Register
#define setCaSinkWpReg(core, value)     (cmm[core]->ca_sink_wp_register = (value))
#define getCaSinkWpReg(core)            (cmm[core]->ca_sink_wp_register)

#define setCaWrap(core, opt)            (_setGeneric(getCaSinkWpReg(core), 0x1, (opt), 0x0))
#define getCaWrap(core)                 (_getGeneric(getCaSinkWpReg(core), 0x1, 0x0))

// The CA Sink RP Register
#define setCaSinkRpReg(core, value)     (cmm[core]->ca_sink_rp_register = (value))
#define getCaSinkRpReg(core)            (cmm[core]->ca_sink_rp_register)

// reset macros for Tf, Ts, Ca, and Tf registers

#define TFReset(){                                                                      \
    setTfActive(0);                                                                     \
    setTfActive(1);                                                                     \
}

#define TEReset(core){                                                                  \
    if(core == TRACE_CORES_ALL){                                                        \
        for(int i = 0; i < CORE_COUNT; i++){                                            \
            setTeActive(i, 0);                                                          \
            setTeActive(i, 1);                                                          \
        }                                                                               \
        if(tfBaseAddress) TFReset();                                                    \
    }                                                                                   \
    else{                                                                               \
        setTeActive(core, 0);                                                           \
        setTeActive(core, 1);                                                           \
    }                                                                                   \
}

#define TSReset(core){                                                                  \
    if(core == TRACE_CORES_ALL){                                                        \
        for(int i = 0; i < CORE_COUNT; i++){                                            \
            setTsActive(i, 0);                                                          \
            setTsActive(i, 1);                                                          \
        }                                                                               \
        if(tfBaseAddress) TFReset();                                                    \
    }                                                                                   \
    else{                                                                               \
        setTsActive(core, 0);                                                           \
        setTsActive(core, 1);                                                           \
    }                                                                                   \
}

#define PCReset(core){                                                                  \
    if(core == TRACE_CORES_ALL){                                                        \
        for(int i = 0; i < CORE_COUNT; i++){                                            \
            setPcActive(i, 0);                                                          \
            setPcActive(i, 1);                                                          \
        }                                                                               \
    }                                                                                   \
    else{                                                                               \
        setPcActive(core, 0);                                                           \
        setPcActive(core, 1);                                                           \
    }                                                                                   \
}

#define CAReset(core){                                                                  \
    if(core == TRACE_CORES_ALL){                                                        \
        for(int i = 0; i < CORE_COUNT; i++){                                            \
            setCaActive(i, 0);                                                          \
            setCaActive(i, 1);                                                          \
        }                                                                               \
    }                                                                                   \
    else{                                                                               \
        setCaActive(core, 0);                                                           \
        setCaActive(core, 1);                                                           \
    }                                                                                   \
}

/*==========================================================================================
Query Macros
==========================================================================================*/

// define a macro to check if HTM is supported
#define checkHaveHTM(core)({                                                            \
    uint32_t saved = tmm[core]->te_control_register;                                    \
    uint32_t tmp = saved & 0xffffff8f;                                                  \
    tmp |= 0x00000070;                                                                  \
    tmm[core]->te_control_register = tmp;                                               \
    tmp = tmm[core]->te_control_register;                                               \
    tmm[core]->te_control_register = saved;                                             \
    ((( tmp &  0x00000070) >> 4) == 0x7) ? 1 : 0;                                       \
})

// define a macro to check if BTM is supported
#define checkHaveBTM(core)({                                                            \
    uint32_t saved = tmm[core]->te_control_register;                                    \
    uint32_t tmp = saved & 0xffffff8f;                                                  \
    tmp |= 0x00000030;                                                                  \
    tmm[core]->te_control_register = tmp;                                               \
    tmp = tmm[core]->te_control_register;                                               \
    tmm[core]->te_control_register = saved;                                             \
    ((( tmp &  0x00000030) >> 4) == 0x3) ? 0 : 1;                                       \
})

/*==========================================================================================
Manual Tracing -- Can be either a single core, or all cores using TRACE_CORES_ALL
==========================================================================================*/

// Set a specified core's TE's Enable and Active bits to high(enable) and low (disable))
#define enableTraceEncoder(core) {                                                      \
    if(core == TRACE_CORES_ALL){                                                        \
        for(int i = 0; i < CORE_COUNT; i++){                                            \
            setTeEnable(i, 0x1);                                                        \
            setTeActive(i, 0x1);                                                        \
        }                                                                               \
    }                                                                                   \
    else{                                                                               \
        setTeEnable(core, 0x1);                                                         \
        setTeActive(core, 0x1);                                                         \
    }                                                                                   \
}

#define disableTraceEncoder(core) {                                                     \
    if(core == TRACE_CORES_ALL){                                                        \
        for(int i = 0; i < CORE_COUNT; i++){                                            \
            setTeEnable(i, 0x0);                                                        \
        }                                                                               \
    }                                                                                   \
    else{                                                                               \
        setTeEnable(core, 0x0);                                                         \
    }                                                                                   \
}

// Enable or disable a core's tracing bit
#define enableTrace(core) {                                                             \
    if(core == TRACE_CORES_ALL){                                                        \
        for(int i = 0; i < CORE_COUNT; i++){                                            \
            setTeTracing(i, 0x1);                                                       \
        }                                                                               \
    }                                                                                   \
    else{                                                                               \
        setTeTracing(core, 0x1);                                                        \
    }                                                                                   \
}

#define disableTrace(core) {                                                            \
    if(core == TRACE_CORES_ALL){                                                        \
        for(int i = 0; i < CORE_COUNT; i++){                                            \
            setTeTracing(i, 0x0);                                                       \
        }                                                                               \
    }                                                                                   \
    else{                                                                               \
        setTeTracing(core, 0x0);                                                        \
    }                                                                                   \
}

// Enable or disable a specified core's trace encoder in order to
// toggle trace collection on/off
#define Trace(core, opt) {                                                              \
    if (opt == TRACE_ON){                                                               \
        enableTrace(core);                                                              \
    }                                                                                   \
    else if (opt == TRACE_OFF){                                                         \
        disableTrace(core);                                                             \
    }                                                                                   \
    else if (opt == TRACE_RESET){                                                       \
        TEReset(core);                                                                  \
    }                                                                                   \
}

// Set a specified core's trace encoder to a specific instruction
// uses the TE_INSTRUCTION_OPT flags
#define setTraceMode(core, opt){                                                        \
    if(core == TRACE_CORES_ALL){                                                        \
        for(int i = 0; i < CORE_COUNT; i++){                                            \
            setTeInstruction(i, opt);                                                   \
        }                                                                               \
    }                                                                                   \
    else{                                                                               \
        setTeInstruction(core, opt);                                                    \
    }                                                                                   \
}

// helper function for macro below
#define _traceConfig(core, inst, instru, overflow, stall, sow, srcInhib, maxBtm, maxInst, teSink){      \
    setTeInstruction(core, inst);                                                                       \
    setTeInstrumentation(core, instru);                                                                 \
    setTeStallOrOverflow(core, overflow);                                                               \
    setTeStallEnable(core, stall);                                                                      \
    setTeStopOnWrap(core, sow);                                                                         \
    setTeInhibitSrc(core, srcInhib);                                                                    \
    setTeSyncMaxBtm(core, maxBtm);                                                                      \
    setTeSyncMaxInst(core, maxInst);                                                                    \
    setTeSliceFormat(core, SLICE_FORMAT_6MDO_2MSEO);                                                    \
    setTeSink(core, teSink);                                                                            \
}

// Configure a core's trace encoder control register using
// user specified parameters
// if trace_cores_all is specified, and there is a funnel sink, all cores will be set to use the funnel
// if the funnel isnt present, and trace_cores_all is specified, all cores will be set to the specified sink
#define traceConfig(core, inst, instru, overflow, stall, sow, srcInhib, maxBtm, maxInst, teSink){       \
    if(core == TRACE_CORES_ALL){                                                                        \
        for(int i = 0; i < CORE_COUNT; i++){                                                            \
            if (tfBaseAddress){                                                                         \
                _traceConfig(i, inst, instru, overflow, stall, sow, srcInhib, maxBtm, maxInst,          \
                             TE_SINK_FUNNEL);                                                           \
            }                                                                                           \
            else{                                                                                       \
                _traceConfig(i, inst, instru, overflow, stall, sow, srcInhib, maxBtm, maxInst,          \
                             teSink);                                                                   \
            }                                                                                           \
        }                                                                                               \
    }                                                                                                   \
    else{                                                                                               \
        _traceConfig(core, inst, instru, overflow, stall, sow, srcInhib, maxBtm, maxInst, teSink);      \
    }                                                                                                   \
}

// Set the trace encoder's sink address to the user defined address
// in a specified core. If TRACE_CORES_ALL is specified, it will set all the
// cores to use the funnel sink, and will use the same sink address for all of the cores
// if trace_cores_all is specified, and there is no funnel sink, no operation will be performed
#define traceSetSinkAddr(core, addr, size){                                             \
    if((core == TRACE_CORES_ALL) && (tfBaseAddress)){                                   \
        for(int i = 0; i < CORE_COUNT; i++){                                            \
            setTeSinkLimit(i, addr+size-4);                                             \
            setTeSinkBase(i, addr);                                                     \
            setTeSinkBaseHigh(i, addr >> 30);                                           \
            setTeSinkWpReg(i, 0x0);                                                     \
            setTeSinkRpReg(i, 0x0);                                                     \
            setTeSink(i, TE_SINK_FUNNEL);                                               \
        }                                                                               \
    }                                                                                   \
    if(core == TRACE_CORES_ALL){                                                        \
        for(int i = 0; i < CORE_COUNT; i++){                                            \
            setTeSinkLimit(i, addr+size-4);                                             \
            setTeSinkBase(i, addr);                                                     \
            setTeSinkBaseHigh(i, addr >> 30);                                           \
            setTeSinkWpReg(i, 0x0);                                                     \
            setTeSinkRpReg(i, 0x0);                                                     \
        }                                                                               \
    }                                                                                   \
    else{                                                                               \
        setTeSinkLimit(core, addr+size-4);                                              \
        setTeSinkBase(core, addr);                                                      \
        setTeSinkBaseHigh(core, addr >> 30);                                            \
        setTeSinkWpReg(core, 0x0);                                                      \
        setTeSinkRpReg(core, 0x0);                                                      \
    }                                                                                   \
}

// Enable a core's trace encoder,
// if TRACE_CORES_ALL is specified, and there is a funnel
// all cores will be set to use the funnel, and the funnel
// will be activated as well
#define setTraceEnable(core, opt){                                                      \
    if(core == TRACE_CORES_ALL){                                                        \
        for(int i = 0; i < CORE_COUNT; i++){                                            \
            setTeEnable(i, opt);                                                        \
            if(tfBaseAddress){                                                          \
                setTeSink(i, TE_SINK_FUNNEL);                                           \
            }                                                                           \
        }                                                                               \
    }                                                                                   \
    else{                                                                               \
        setTeEnable(core, opt);                                                         \
    }													                                \
    if(tfBaseAddress){                                                                  \
        setTfEnable(opt);                                                               \
    }                                                                                   \
}

// helper function for macro below
#define _traceClear(core){                                                              \
    if (getTeImplHasSBASink(core)){                                                     \
        setTeSinkRpReg(core, getTeSinkLimit(core));                                     \
        setTeSinkWpReg(core, getTeSinkLimit(core));                                     \
    }                                                                                   \
    else{                                                                               \
        setTeSinkWpReg(core, 0x0);                                                      \
        setTeSinkRpReg(core, 0x0);                                                      \
    }                                                                                   \
}

// Clear the trace encoder buffer
#define traceClear(core){                                                               \
    if(core == TRACE_CORES_ALL){                                                        \
        for(int i = 0; i < CORE_COUNT; i++){                                            \
            _traceClear(i);                                                             \
        }                                                                               \
        if(tfBaseAddress){                                                              \
            setTfSinkWp(0x0);                                                           \
            setTfSinkRp(0x0);                                                           \
        }                                                                               \
    }                                                                                   \
    else{                                                                               \
        _traceClear(core);                                                              \
    }                                                                                   \
}

// helper function for macro below
#define _traceConfigDefaults(core, sink){                                               \
    TEReset(core);                                                                      \
    setTeEnable(core, 0x1);                                                             \
    traceClear(core);                                                                   \
    setXtiReg(core, 0x4);                                                               \
    setXtoReg(core, 0x21);                                                              \
    setTeTracing(core, TRACE_ENABLE);                                                   \
    checkHaveHTM(core) ? setTeInstruction(core, 0x7) : setTeInstruction(core, 0x3);     \
    setTeSink(core, sink);                                                              \
}

// Configure a core's trace encoder to default values
#define traceConfigDefaults(core){                                                      \
    if(core == TRACE_CORES_ALL){                                                        \
        for(int i = 0; i < CORE_COUNT; i++){                                            \
            _traceConfigDefaults(i, TE_SINK_FUNNEL);                                    \
        }                                                                               \
        setTfSink(TE_SINK_SRAM);                                                        \
    }                                                                                   \
    else{                                                                               \
        _traceConfigDefaults(core, TE_SINK_DEFAULT);                                    \
    }                                                                                   \
}

// Configure the time stamp control register to user specified parameters
#define tsConfig(core, debug, prescale, branch, instru, own){                           \
    setTsInstrumentation(core, instru);                                                 \
    setTsOwnership(core, own);                                                          \
    setTsDebug(core, debug);                                                            \
    setTsPrescale(core, prescale);                                                      \
    setTsBranch(core, branch);                                                          \
}

// Configure the time stamp control register to default parameters
#define tsConfigDefault(core){                                                          \
    if(core == TRACE_CORES_ALL){                                                        \
        for(int i = 0; i < CORE_COUNT; i++){                                            \
            TSReset(i);                                                                 \
            tsConfig(i, 0x0, 0x1, 0x1, 0x1, 0x1);                                       \
        }                                                                               \
    }                                                                                   \
    else{                                                                               \
        TSReset(core);                                                                  \
        tsConfig(core, 0x0, 0x1, 0x1, 0x1, 0x1);                                        \
    }                                                                                   \
}

// Set the funnel sink address to the user defined address
// and set the limiting address
#define tfSetSinkAddr(addr, size){         			 	                                \
    setTfSinkLimit(addr+size-4);           				                                \
    setTfSinkBase(addr);                   				                                \
    setTfSinkBaseHigh((uint64_t)addr >> 32);         	                                \
}

// Configure the trace funnel with default parameters
#define tfConfigDefault(){                                                              \
    TFReset();                                                                          \
    setTfStopOnWrap(0x0);                                                               \
    setTfSink(0x0);                                                                     \
}

//  Configure the trace funnel with user specified parameters
#define tfConfig(sow, sink){                                                            \
    setTfStopOnWrap(sow);                                                               \
    setTfSink(sink);                                                                    \
}

// Flush the trace buffers in all cores. If any core has a funnel sink,
// then flush the funnel sink. If the funnel sink has either an ATB or PIB sink
// flush them. Then, if any of the cores has either an ATB or PIB sink
// flush them.
// Will check to make sure that each buffer if flushed before continuing
// to the next buffer
#define traceFlush(){                                                                   \
    int _tmp_Funnel_Flag = 0;                                                           \
    int _tmp_Atb_Flag = 0;                                                              \
    int _tmp_Pib_Flag = 0;                                                              \
    for (int i = 0; i < CORE_COUNT; i++){                                               \
        setTeTracing(i, 0x0);                                                           \
        setTeEnable(i, 0x0);                                                            \
        while(getTeEmpty(i) == 0){}                                                     \
        if(getTeSink(i) == 8){                                                          \
            _tmp_Funnel_Flag = 1;                                                       \
        }                                                                               \
        else if(getTeSink(i) == 5){                                                     \
            _tmp_Atb_Flag = 1;                                                          \
        }                                                                               \
        else if(getTeSink(i) == 6){                                                     \
            _tmp_Pib_Flag = 1;                                                          \
        }                                                                               \
    }                                                                                   \
    if(_tmp_Funnel_Flag){                                                               \
        setTfEnable(0x0);                                                               \
        while(getTfEmpty() != 0){}                                                      \
        if(getTfHasATBSink()){                                                          \
            setTfAtbEnable(0x0);                                                        \
            while(getTfAtbEmpty() != 0){}                                               \
        }                                                                               \
        if(getTfHasPIBSink()){                                                          \
            setTfPibEnable(0x0);                                                        \
            while(getTfPibEmpty() != 0){}                                               \
        }                                                                               \
    }                                                                                   \
    if(_tmp_Atb_Flag){                                                                  \
        for (int i = 0; i < CORE_COUNT; i++){                                           \
            setTeAtbEnable(i, 0x0);                                                     \
            while(getTeAtbEmpty(i) != 0){}                                              \
        }                                                                               \
    }                                                                                   \
    if(_tmp_Pib_Flag){                                                                  \
        for (int i = 0; i < CORE_COUNT; i++){                                           \
            setTePibEnable(i, 0x0);                                                     \
            while(getTePibEmpty(i) != 0){}                                              \
        }                                                                               \
    }                                                                                   \
}                                                                                       \

// helper function to take the control register's parameters and stitch them into 32 bit values
// at which point the control registers can be set in one go
// thus avoiding issues that arrise when configured peacemeal
#define _caParamSticher(core,teInstruction,teInstrumentation,teStallEnable,teStopOnWrap,\
    teInhibitSrc,teSyncMaxBTM,teSyncMaxInst,teSink,caStopOnWrap){                       \
    uint32_t _tmp_te_reg = 0x0;                                                         \
    _tmp_te_reg = 1 | (1<<1) | (1<<2) | (teInstruction<<4) | (teInstrumentation<<7) |   \
    (teStallEnable<<13) | (teStopOnWrap<<14) | (teInhibitSrc<<15) | (teSyncMaxBTM<<16) |\
    (teSyncMaxInst<<20) | (1<<24) | (teSink<<28);                                       \
    uint32_t _tmp_ca_reg = 0x0;                                                         \
    _tmp_ca_reg = 1 | (1<<1) | (caStopOnWrap<<14) | (0x4<<28);                          \
    setCaControl(core, _tmp_ca_reg);                                                    \
    setTeControl(core, _tmp_te_reg);                                                    \
}

// Accurate Trace on -- Defaults, HTM -- Not for end user
#define _caTraceConfigHTMDefaults(core, sink)({                                          \
    setTeActive(core, 0);                                                               \
    setCaActive(core, 0);                                                               \
    setTeActive(core, 1);                                                               \
    setTeSinkWpReg(core, 0);                                                            \
    setTeSinkRpReg(core, 0);                                                            \
    setXtiReg(core, 0x4);                                                               \
    setXtoReg(core, 0x21);                                                              \
    setCaActive(core, 1);                                                               \
    setCaSinkWpReg(core, 0);                                                            \
    setCaSinkRpReg(core, 0);                                                            \
    _caParamSticher(core,TE_INSTRUCTION_HTMSYNC,TE_INSTRUMENTATION_NONE,TE_DISABLE,     \
    TE_ENABLE,TE_ENABLE,SYNC_MAX_BTM_256,SYNC_MAX_INST_1024,sink,1);                    \
})



// Cycle Accurate Trace on -- Defaults, BTM -- Not for end user
#define _caTraceConfigBTMDefaults(core, sink)({                                          \
    setTeActive(core, 0);                                                               \
    setCaActive(core, 0);                                                               \
    setTeActive(core, 1);                                                               \
    setTeSinkWpReg(core, 0);                                                            \
    setTeSinkRpReg(core, 0);                                                            \
    setXtiReg(core, 0x4);                                                               \
    setXtoReg(core, 0x21);                                                              \
    setCaActive(core, 1);                                                               \
    setCaSinkWpReg(core, 0);                                                            \
    setCaSinkRpReg(core, 0);                                                            \
    _caParamSticher(core,TE_INSTRUCTION_BTMSYNC,TE_INSTRUMENTATION_NONE,TE_DISABLE,     \
    TE_ENABLE,TE_ENABLE,SYNC_MAX_BTM_256,SYNC_MAX_INST_1024,sink,1);                    \
})


// helper function for macro below
#define _caTraceConfigDefaults(core, origcore){                                                   \
    if (checkHaveHTM(core)){                                                            \
        (origcore != TRACE_CORES_ALL) ? _caTraceConfigHTMDefaults(core, TE_SINK_SRAM) :     \
         _caTraceConfigHTMDefaults(core, TE_SINK_FUNNEL);                               \
    }                                                                                   \
    else if (checkHaveBTM(core)){                                                       \
        (origcore != TRACE_CORES_ALL) ? _caTraceConfigBTMDefaults(core, TE_SINK_SRAM) :     \
         _caTraceConfigBTMDefaults(core, TE_SINK_FUNNEL);                               \
    }                                                                                   \
}

// Cycle Accurate Trace on -- Defaults, will select HTM or BTM depending
// on hardware support
#define caTraceConfigDefaults(core){                                                    \
    if(core ==  TRACE_CORES_ALL){                                                       \
        for(int i = 0; i < CORE_COUNT; i++){                                            \
            _caTraceConfigDefaults(i, core);                                                  \
        }                                                                               \
    }                                                                                   \
    else{                                                                               \
        _caTraceConfigDefaults(core, core);                                                   \
    }                                                                                   \
}

// helper function for macro below
#define _caTraceConfig(core,teInstruction,teInstrumentation,teStallEnable,teStopOnWrap, \
    teInhibitSrc,teSyncMaxBTM,teSyncMaxInst,teSink,caStopOnWrap){                       \
    setTeActive(core, 0);                                                               \
    setCaActive(core, 0);                                                               \
    setTeActive(core, 1);                                                               \
    setTeSinkWpReg(core, 0);                                                            \
    setTeSinkRpReg(core, 0);                                                            \
    setXtiReg(core, 0x4);                                                               \
    setXtoReg(core, 0x21);                                                              \
    setCaActive(core, 1);                                                               \
    setCaSinkWpReg(core, 0);                                                            \
    setCaSinkRpReg(core, 0);                                                            \
    _caParamSticher(core,teInstruction,teInstrumentation,teStallEnable,teStopOnWrap,    \
    teInhibitSrc,teSyncMaxBTM,teSyncMaxInst,teSink,caStopOnWrap);                       \
}

// Cycle Accurate Trace on -- User defined parameters
#define caTraceConfig(core,teInstruction,teInstrumentation,teStallEnable,teStopOnWrap,  \
    teInhibitSrc,teSyncMaxBTM,teSyncMaxInst,teSink,caStopOnWrap){                       \
    if(core ==  TRACE_CORES_ALL){                                                       \
        for(int i = 0; i < CORE_COUNT; i++){                                            \
            _caTraceConfig(i,teInstruction,teInstrumentation,teStallEnable,teStopOnWrap \
            ,teInhibitSrc,teSyncMaxBTM,teSyncMaxInst,TE_SINK_FUNNEL,caStopOnWrap);      \
        }                                                                               \
    }                                                                                   \
    else{                                                                               \
        _caTraceConfig(core,teInstruction,teInstrumentation,teStallEnable,teStopOnWrap  \
        ,teInhibitSrc,teSyncMaxBTM,teSyncMaxInst,teSink,caStopOnWrap)                   \
    }                                                                                   \
}

// Enable a specified ITC channel
#define ItcEnableChannel(core, channel)({                                               \
    if(core ==  TRACE_CORES_ALL){                                                       \
        for(int i = 0; i < CORE_COUNT; i++){                                            \
            setITCTraceEnable(i,                                                        \
            getITCTraceEnable(i) | 1 << (channel));                                     \
        }                                                                               \
    }                                                                                   \
    else{                                                                               \
        setITCTraceEnable(core,                                                         \
        getITCTraceEnable(core) | 1<< (channel));                                       \
    }                                                                                   \
})

// Disable a specified ITC channel
#define ItcDisableChannel(core, channel)({                                              \
    if(core ==  TRACE_CORES_ALL){                                                       \
        for(int i = 0; i < CORE_COUNT; i++){                                            \
            setITCTraceEnable(i,                                                        \
            getITCTraceEnable(i) & 									                    \
			(getITCTraceEnable(i) | ~(1<<(channel))));                                  \
        }                                                                               \
    }                                                                                   \
    else{                                                                               \
    	setITCTraceEnable(core,                                         	            \
    	getITCTraceEnable(core) & 									                    \
    	(getITCTraceEnable(core) | ~(1<<(channel))));                	                \
    }                                                                                   \
})

// Write a 32 bit number to a specified ITC channel
// Wont accept TRACE CORES ALL
#define ItcWrite(core, channel, marker){                                                \
    setITCStimulus32(core, channel, marker)                                             \
}

/*==========================================================================================
Print Macros
==========================================================================================*/

// define a macro to print out the trace registers
#define teRegDump(core){                                                                                                            \
    printf("+-TE-REGISTERS------+-Address--+-Value----+\n");                                                                        \
    printf("| Control           | %08X | %08X |\n", &getTeControl(core), getTeControl(core));	                                    \
    printf("| IMPL              | %08X | %08X |\n", &getTeImpl(core), getTeImpl(core));	                                            \
    printf("| SINKBASE          | %08X | %08X |\n", &getTeSinkBase(core), getTeSinkBase(core));	                                    \
    printf("| SINKBASE HIGH     | %08X | %08X |\n", &getTeSinkBaseHigh(core), getTeSinkBaseHigh(core));	                            \
    printf("| SINKBASE LIMIT    | %08X | %08X |\n", &getTeSinkLimit(core), getTeSinkLimit(core));	                                \
    printf("| SINK WP           | %08X | %08X |\n", &getTeSinkWpReg(core), getTeSinkWpReg(core));	                                \
    printf("| SINK RP           | %08X | %08X |\n", &getTeSinkRpReg(core), getTeSinkRpReg(core));	                                \
    printf("| SINK DATA         | %08X | %08X |\n", &getTeSinkData(core), getTeSinkData(core));	                                    \
    printf("| FIFO              | %08X | %08X |\n", &getTeFifo(core), getTeFifo(core));	                                            \
    printf("| BTM COUNT         | %08X | %08X |\n", &getTeBtmCnt(core), getTeBtmCnt(core));	                                        \
    printf("| WORD COUNT        | %08X | %08X |\n", &getTeWordCnt(core), getTeWordCnt(core));	                                    \
    printf("| TS Control        | %08X | %08X |\n", &getTsControl(core), getTsControl(core));                                       \
    printf("| TS LOWER          | %08X | %08X |\n", &getTsLower(core), getTsLower(core));	                                        \
    printf("| TS UPPER          | %08X | %08X |\n", &getTsUpper(core), getTsUpper(core));	                                        \
    printf("| XTI CONTROL       | %08X | %08X |\n", &getXtiReg(core), getXtiReg(core));	                                            \
    printf("| XTO CONTROL       | %08X | %08X |\n", &getXtoReg(core), getXtoReg(core));	                                            \
    printf("| WP CONTROL        | %08X | %08X |\n", &getTeWp(core), getTeWp(core));	                                                \
    printf("| ITC TRACE ENABLE  | %08X | %08X |\n", &getITCTraceEnable(core), getITCTraceEnable(core));	                            \
    printf("| ITC TRIG ENABLE   | %08X | %08X |\n", &getITCTrigEnable(core), getITCTrigEnable(core));	                            \
    printf("+-------------------+----------+----------+\n");                                                                        \
}

// define a macro to print out the ca trace registers
#define tfRegDump(){                                                                                                                \
    printf("+-TF-REGISTERS------+-Address--+-Value----+\n");                                                                        \
    printf("| Control           | %08X | %08X |\n", &getTfControl(), getTfControl());	                                            \
    printf("| IMPL              | %08X | %08X |\n", &getTfImpl(), getTfImpl());	                                                    \
    printf("| SINKBASE          | %08X | %08X |\n", &getTfSinkBase(), getTfSinkBase());	                                            \
    printf("| SINKBASE HIGH     | %08X | %08X |\n", &getTfSinkBaseHigh(), getTfSinkBaseHigh());	                                    \
    printf("| SINK LIMIT        | %08X | %08X |\n", &getTfSinkLimit(), getTfSinkLimit());	                                        \
    printf("| SINK WP           | %08X | %08X |\n", &getTfSinkWp(), getTfSinkWp());	                                                \
    printf("| SINK RP           | %08X | %08X |\n", &getTfSinkRp(), getTfSinkRp());	                                                \
    printf("| SINK DATA         | %08X | %08X |\n", &getTfSinkData(), getTfSinkData());	                                            \
    printf("+-------------------+----------+----------+\n");                                                                        \
}

// define a macro to print out the tf trace registers
#define caRegDump(core){                                                                                                            \
    printf("+-CA-REGISTERS------+-Address--+-Value----+\n");                                                                        \
    printf("| Control           | %08X | %08X |\n", &getCaControl(core), getCaControl(core));	                                    \
    printf("| IMPL              | %08X | %08X |\n", &getCaImpl(core), getCaImpl(core));	                                            \
    printf("| SINK WP           | %08X | %08X |\n", &getCaSinkWpReg(core), getCaSinkWpReg(core));	                                \
    printf("| SINK RP           | %08X | %08X |\n", &getCaSinkRpReg(core), getCaSinkRpReg(core));	                                \
    printf("| SINK DATA         | %08X | %08X |\n", &getCaSinkData(core), getCaSinkData(core));	                                    \
    printf("+-------------------+----------+----------+\n");                                                                        \
}

#define teControlDump(core){                                                                                                        \
    printf("+TE Control Register, Address: %08X, Value: %08X+\n", &getTeControl(core), getTeControl(core));                         \
    printf("| Bits  | Field             | Value | Discription       |\n");                                                          \
    printf("|     0 | teActive          |    %02X |", getTeActive(core));                                                           \
    if (getTeActive(core)){                                                                                                         \
        printf(" Active            |\n");                                                                                           \
    }                                                                                                                               \
    else{                                                                                                                           \
        printf(" Reset             |\n");                                                                                           \
    }                                                                                                                               \
    printf("|     1 | teEnable          |    %02X |", getTeEnable(core));                                                           \
    if (getTeEnable(core)){                                                                                                         \
        printf(" Enabled           |\n");                                                                                           \
    }                                                                                                                               \
    else{                                                                                                                           \
        printf(" Disabled          |\n");                                                                                           \
    }                                                                                                                               \
    printf("|     2 | teTracing         |    %02X |", getTeTracing(core));                                                          \
    if (getTeTracing(core)){                                                                                                        \
        printf(" Tracing           |\n");                                                                                           \
    }                                                                                                                               \
    else{                                                                                                                           \
        printf(" Not Tracing       |\n");                                                                                           \
    }                                                                                                                               \
    printf("|     3 | teEmpty           |    %02X |", getTfEmpty());                                                            	\
    if (getTfEmpty()){                                                                                                          	\
        printf(" Flushing          |\n");                                                                                           \
    }                                                                                                                               \
    else{                                                                                                                           \
        printf(" Flushed           |\n");                                                                                           \
    }                                                                                                                               \
    printf("|   6-4 | teInstruction     |    %02X |", getTeInstruction(core));                                                      \
    switch (getTeInstruction(core)){                                                                                                \
        case 0:                                                                                                                     \
            printf(" No BTM or Sync    |\n");                                                                                       \
            break;                                                                                                                  \
        case 1:                                                                                                                     \
            printf(" Sync Only         |\n");                                                                                       \
            break;                                                                                                                  \
        case 3:                                                                                                                     \
            printf(" BTM and Sync      |\n");                                                                                       \
            break;                                                                                                                  \
        case 6:                                                                                                                     \
            printf(" HTM+Sync, Not Opt |\n");                                                                                       \
            break;                                                                                                                  \
        case 7:                                                                                                                     \
            printf(" HTM and Sync      |\n");                                                                                       \
            break;                                                                                                                  \
        default:                                                                                                                    \
            printf(" Reserved          |\n");                                                                                       \
            break;                                                                                                                  \
    }                                                                                                                               \
    printf("|   8-7 | teInstrumentation |    %02X |", getTeInstrumentation(core));                                                  \
    switch (getTeInstrumentation(core)){                                                                                            \
        case 0:                                                                                                                     \
            printf(" ITC Disabled      |\n");                                                                                       \
            break;                                                                                                                  \
        case 1:                                                                                                                     \
            printf(" Sync Only         |\n");                                                                                       \
            break;                                                                                                                  \
        case 3:                                                                                                                     \
            printf(" BTM and Sync      |\n");                                                                                       \
            break;                                                                                                                  \
        default:                                                                                                                    \
            printf(" Reserved          |\n");                                                                                       \
            break;                                                                                                                  \
    }                                                                                                                               \
    printf("|    13 | teStallEnable     |    %02X |", getTeStallEnable(core));                                                      \
    if (getTeStallEnable(core)){                                                                                                    \
        printf(" Stall Core        |\n");                                                                                           \
    }                                                                                                                               \
    else{                                                                                                                           \
        printf(" Reserved          |\n");                                                                                           \
    }                                                                                                                               \
    printf("|    14 | teStopOnWrap      |    %02X |", getTeStopOnWrap(core));                                                       \
    if (getTeStopOnWrap(core)){                                                                                                     \
        printf(" Will Stop         |\n");                                                                                           \
    }                                                                                                                               \
    else{                                                                                                                           \
        printf(" Will Wrap         |\n");                                                                                           \
    }                                                                                                                               \
    printf("|    15 | teInhibitSrc      |    %02X |", getTeInhibitSrc(core));                                                       \
    if (getTeInhibitSrc(core)){                                                                                                     \
        printf(" Disable SRC       |\n");                                                                                           \
    }                                                                                                                               \
    else{                                                                                                                           \
        printf(" Enable SRC        |\n");                                                                                           \
    }                                                                                                                               \
    printf("| 19-16 | teSyncMaxBTM      |    %02X |", getTeSyncMaxBtm(core));                                                       \
    if (getTeSyncMaxBtm(core) <= 11){                                                                                               \
        printf(" BTM and Sync      |\n");                                                                                           \
    }                                                                                                                               \
    else{                                                                                                                           \
        printf(" Reserved          |\n");                                                                                           \
    }                                                                                                                               \
    printf("| 23-20 | teSyncMaxInst     |    %02X |", getTeSyncMaxInst(core));                                                      \
    if (getTeSyncMaxInst(core) == 1){                                                                                               \
        printf(" 6 MDO + 2 MSEO    |\n");                                                                                           \
    }                                                                                                                               \
    else{                                                                                                                           \
        printf(" Reserved          |\n");                                                                                           \
    }                                                                                                                               \
    printf("|    27 | teSinkError       |    %02X |", getTeSinkError(core));                                                        \
    if (getTeSinkError(core)){                                                                                                      \
        printf(" Error             |\n");                                                                                           \
    }                                                                                                                               \
    else{                                                                                                                           \
        printf(" No Error          |\n");                                                                                           \
    }                                                                                                                               \
    printf("| 31-28 | teSink            |    %02X |", getTeSink(core));                                                             \
    switch (getTeSink(core)){                                                                                                       \
        case 0:                                                                                                                     \
            printf(" Connected Sink    |\n");                                                                                       \
            break;                                                                                                                  \
        case 4:                                                                                                                     \
            printf(" SRAM Sink         |\n");                                                                                       \
            break;                                                                                                                  \
        case 5:                                                                                                                     \
            printf(" ATB Sink          |\n");                                                                                       \
            break;                                                                                                                  \
        case 6:                                                                                                                     \
            printf(" PIB Sink          |\n");                                                                                       \
            break;                                                                                                                  \
        case 7:                                                                                                                     \
            printf(" SBA Sink          |\n");                                                                                       \
            break;                                                                                                                  \
        case 8:                                                                                                                     \
            printf(" Funnel Sink       |\n");                                                                                       \
            break;                                                                                                                  \
        default:                                                                                                                    \
            printf(" Reserved          |\n");                                                                                       \
            break;                                                                                                                  \
    }                                                                                                                               \
    printf("+-------+-------------------+-------+-------------------+\n");                                                          \
}

#define tsControlDump(core){                                                                                                        \
    printf("+TS Control Register, Address: %08X, Value: %08X+\n", &getTsControl(core), getTsControl(core));                         \
    printf("| Bits  | Field             | Value | Discription       |\n");                                                          \
    printf("|     0 | tsActive          |    %02X |", getTsActive(core));                                                           \
    if (getTsActive(core)){                                                                                                         \
        printf(" Active            |\n");                                                                                           \
    }                                                                                                                               \
    else{                                                                                                                           \
        printf(" Reset             |\n");                                                                                           \
    }                                                                                                                               \
    printf("|     1 | tsCount           |    %02X |", getTsCount(core));                                                            \
    if (getTsCount(core)){                                                                                                          \
        printf(" Counter Running   |\n");                                                                                           \
    }                                                                                                                               \
    else{                                                                                                                           \
        printf(" Counter Stopped   |\n");                                                                                           \
    }                                                                                                                               \
    printf("|     2 | tsReset           |    %02X |", getTsReset(core));                                                            \
    if (getTsReset(core)){                                                                                                          \
        printf(" Counter Reset     |\n");                                                                                           \
    }                                                                                                                               \
    else{                                                                                                                           \
        printf(" Counting          |\n");                                                                                           \
    }                                                                                                                               \
    printf("|     3 | teDebug           |    %02X |", getTsDebug(core));                                                            \
    if (getTsDebug(core)){                                                                                                          \
        printf(" Debug Mode        |\n");                                                                                           \
    }                                                                                                                               \
    else{                                                                                                                           \
        printf(" Normal Mode       |\n");                                                                                           \
    }                                                                                                                               \
    printf("|   6-4 | tsType            |    %02X |", getTsType(core));                                                             \
    switch (getTsType(core)){                                                                                                       \
        case 0:                                                                                                                     \
            printf(" None              |\n");                                                                                       \
            break;                                                                                                                  \
        case 1:                                                                                                                     \
            printf(" External          |\n");                                                                                       \
            break;                                                                                                                  \
        case 2:                                                                                                                     \
            printf(" Internal Bus Clk  |\n");                                                                                       \
            break;                                                                                                                  \
        case 3:                                                                                                                     \
            printf(" Internal Core Clk |\n");                                                                                       \
            break;                                                                                                                  \
        case 4:                                                                                                                     \
            printf(" Slave             |\n");                                                                                       \
            break;                                                                                                                  \
        default:                                                                                                                    \
            printf(" Reserved          |\n");                                                                                       \
            break;                                                                                                                  \
    }                                                                                                                               \
    printf("|   9-8 | tsPrescale        |    %02X |", getTsPrescale(core));                                                         \
    printf(" Prescale Value    |\n");                                                                                               \
    printf("|    15 | tsEnable          |    %02X |", getTsEnable(core));                                                           \
    if (getTsEnable(core)){                                                                                                         \
        printf(" Timestamp Enabled |\n");                                                                                           \
    }                                                                                                                               \
    else{                                                                                                                           \
        printf(" Timestamp Disabled|\n");                                                                                           \
    }                                                                                                                               \
    printf("| 17-16 | tsBranch          |    %02X |", getTsBranch(core));                                                           \
    switch (getTsBranch(core)){                                                                                                     \
        case 0:                                                                                                                     \
            printf(" No Timestamps     |\n");                                                                                       \
            break;                                                                                                                  \
        case 1:                                                                                                                     \
            printf(" Indirect Branches |\n");                                                                                       \
            break;                                                                                                                  \
        case 3:                                                                                                                     \
            printf(" All Branches      |\n");                                                                                       \
            break;                                                                                                                  \
        default:                                                                                                                    \
            printf(" Reserved          |\n");                                                                                       \
            break;                                                                                                                  \
    }                                                                                                                               \
    printf("|    18 | tsInstrumentation |    %02X |", getTsInstrumentation(core));                                                  \
    if (getTsInstrumentation(core)){                                                                                                \
        printf(" TS Msgs On Inst   |\n");                                                                                           \
    }                                                                                                                               \
    else{                                                                                                                           \
        printf(" No TS Msgs On Inst|\n");                                                                                           \
    }                                                                                                                               \
    printf("|    19 | tsOwnership       |    %02X |", getTsOwnership(core));                                                        \
    if (getTsOwnership(core)){                                                                                                      \
        printf(" TS Msgs On Ownr   |\n");                                                                                           \
    }                                                                                                                               \
    else{                                                                                                                           \
        printf(" No TS Msgs On Ownr|\n");                                                                                           \
    }                                                                                                                               \
    printf("| 31-24 | tsWidth           |    %02X |", getTsWidth(core));                                                            \
    printf(" Timestamp Width   |\n");                                                                                               \
    printf("+-------+-------------------+-------+-------------------+\n");                                                          \
}

#define implControlDump(core){                                                                                                      \
    printf("+Te Impl Register, Address: %08X, Value: %08X---+\n", &getTeImpl(core), getTeImpl(core));                               \
    printf("| Bits  | Field             | Value | Discription       |\n");                                                          \
    printf("|   3-0 | Version           |    %02X |", getTeImplVersion(core));                                                      \
    printf(" TE Version        |\n");                                                                                               \
    printf("|     4 | hasSRAMSink       |    %02X |", getTeImplHasSRAMSink(core));                                                  \
    if (getTeImplHasSRAMSink(core)){                                                                                                \
        printf(" SRAM Present      |\n");                                                                                           \
    }                                                                                                                               \
    else{                                                                                                                           \
        printf(" SRAM Absent       |\n");                                                                                           \
    }                                                                                                                               \
    printf("|     5 | hasATBSink        |    %02X |", getTeImplHasATBSink(core));                                                   \
    if (getTeImplHasATBSink(core)){                                                                                                 \
        printf(" ATB Present       |\n");                                                                                           \
    }                                                                                                                               \
    else{                                                                                                                           \
        printf(" ATB Absent        |\n");                                                                                           \
    }                                                                                                                               \
    printf("|     6 | hasPIBSink        |    %02X |", getTeImplHasPIBSink(core));                                                   \
    if (getTeImplHasPIBSink(core)){                                                                                                 \
        printf(" PIB Present       |\n");                                                                                           \
    }                                                                                                                               \
    else{                                                                                                                           \
        printf(" PIB Absent        |\n");                                                                                           \
    }                                                                                                                               \
    printf("|     7 | hasSBASink        |    %02X |", getTeImplHasSBASink(core));                                                   \
    if (getTeImplHasSBASink(core)){                                                                                                 \
        printf(" SBA Present       |\n");                                                                                           \
    }                                                                                                                               \
    else{                                                                                                                           \
        printf(" SBA Absent        |\n");                                                                                           \
    }                                                                                                                               \
    printf("|     8 | hasFunnelSink     |    %02X |", getTeImplHasFunnelSink(core));                                                \
    if (getTeImplHasFunnelSink(core)){                                                                                              \
        printf(" Funnel Present    |\n");                                                                                           \
    }                                                                                                                               \
    else{                                                                                                                           \
        printf(" Funnel Absent     |\n");                                                                                           \
    }                                                                                                                               \
    printf("| 17-16 | SinkBytes         |    %02X |", getTeImplSinkBytes(core));                                                    \
    switch (getTeImplSinkBytes(core)){                                                                                              \
        case 0:                                                                                                                     \
            printf(" 32 Bit Width      |\n");                                                                                       \
            break;                                                                                                                  \
        case 1:                                                                                                                     \
            printf(" 64 Bit Width      |\n");                                                                                       \
            break;                                                                                                                  \
        case 2:                                                                                                                     \
            printf(" 128 Bit Width     |\n");                                                                                       \
            break;                                                                                                                  \
        case 3:                                                                                                                     \
            printf(" 256 Bit Width     |\n");                                                                                       \
            break;                                                                                                                  \
        default:                                                                                                                    \
            printf(" Reserved          |\n");                                                                                       \
            break;                                                                                                                  \
    }                                                                                                                               \
    printf("| 19-18 | CrossingType      |    %02X |", getTeImplCrossingType(core));                                                 \
    switch (getTeImplCrossingType(core)){                                                                                           \
        case 0:                                                                                                                     \
            printf(" Synchronous       |\n");                                                                                       \
            break;                                                                                                                  \
        case 1:                                                                                                                     \
            printf(" Rational          |\n");                                                                                       \
            break;                                                                                                                  \
        case 3:                                                                                                                     \
            printf(" Asynchronous      |\n");                                                                                       \
            break;                                                                                                                  \
        default:                                                                                                                    \
            printf(" Reserved          |\n");                                                                                       \
            break;                                                                                                                  \
    }                                                                                                                               \
    printf("| 23-20 | srcID             |    %02X |", getTeImplSrcId(core));                                                        \
    if (getTeImplSrcId(core)){                                                                                                      \
        printf(" Nexus Msg w/ Src  |\n");                                                                                           \
    }                                                                                                                               \
    else{                                                                                                                           \
        printf(" Nexus Msg w/o Src |\n");                                                                                           \
    }                                                                                                                               \
    printf("| 26-24 | nSrcBits          |    %02X |", getTeImplNSrcBits(core));                                                     \
    printf(" Num Bits in Nexus |\n");                                                                                               \
    printf("| 31-28 | hartid            |    %02X |", getTeImplHartId(core));                                                       \
    printf(" Hart ID           |\n");                                                                                               \
    printf("+-------+-------------------+-------+-------------------+\n");                                                          \
}

#define xtiControlDump(core){                                                                                                       \
    printf("+XTI Control Register,Address: %08X, Value: %08X+\n",&getXtiReg(core), getXtiReg(core));                                \
    printf("| Bits  | Field             | Value | Discription       |\n");                                                          \
    printf("|   3-0 | xtiAction0        |    %02X |", _getGeneric(tmm[core]->xti_control_register, 0xF, 0x0));                      \
    switch (_getGeneric(tmm[core]->xti_control_register, 0xF, 0x0)){                                                                \
        case 0:                                                                                                                     \
            printf(" No Action         |\n");                                                                                       \
            break;                                                                                                                  \
        case 2:                                                                                                                     \
            printf(" Start Trace       |\n");                                                                                       \
            break;                                                                                                                  \
        case 3:                                                                                                                     \
            printf(" Stop Trace        |\n");                                                                                       \
            break;                                                                                                                  \
        case 4:                                                                                                                     \
            printf(" Record            |\n");                                                                                       \
            break;                                                                                                                  \
        default:                                                                                                                    \
            printf(" Reserved          |\n");                                                                                       \
            break;                                                                                                                  \
    }                                                                                                                               \
    printf("|  31-4 | xtiActionN        |    %02X |", _getGeneric(tmm[core]->xti_control_register, ~0xF, 0x4));                     \
    printf(" Action Selection      |\n");                                                                                           \
}

#define xtoControlDump(core){                                                                                                       \
    printf("+XTI Control Register,Address: %08X, Value: %08X+\n",&getXtoReg(core), getXtoReg(core));                                \
    printf("| Bits  | Field             | Value | Discription       |\n");                                                          \
    printf("|   3-0 | xtiEvent0         |    %02X |", _getGeneric(tmm[core]->xto_control_register, 0xF, 0x0));                      \
    switch (_getGeneric(tmm[core]->xto_control_register, 0xF, 0x0)){                                                                \
        case 0:                                                                                                                     \
            printf(" Starting Trace    |\n");                                                                                       \
            break;                                                                                                                  \
        case 2:                                                                                                                     \
            printf(" Stop Trace        |\n");                                                                                       \
            break;                                                                                                                  \
        case 3:                                                                                                                     \
            printf(" Stop Trace        |\n");                                                                                       \
            break;                                                                                                                  \
        case 4:                                                                                                                     \
            printf(" ITC Write         |\n");                                                                                       \
            break;                                                                                                                  \
        default:                                                                                                                    \
            printf(" Watchpoint occured|\n");                                                                                       \
            break;                                                                                                                  \
    }                                                                                                                               \
    printf("|  31-4 | xtoEventN         |    %02X |", _getGeneric(tmm[core]->xto_control_register, ~0xF, 0x4));                     \
    printf(" Event Selection       |\n");                                                                                           \
}

#define tfControlDump(){                                                                                                            \
    printf("+TF Control Register, Address: %08X, Value: %08X+\n", &getTfControl(), getTfControl());                                 \
    printf("| Bits  | Field             | Value | Discription       |\n");                                                          \
    printf("|     0 | tfActive          |    %02X |", getTfActive());                                                               \
    if (getTfActive()){                                                                                                             \
        printf(" Active            |\n");                                                                                           \
    }                                                                                                                               \
    else{                                                                                                                           \
        printf(" Reset             |\n");                                                                                           \
    }                                                                                                                               \
    printf("|     1 | tfEnable          |    %02X |", getTfEnable());                                                               \
    if (getTfEnable()){                                                                                                             \
        printf(" Enabled           |\n");                                                                                           \
    }                                                                                                                               \
    else{                                                                                                                           \
        printf(" Disabled          |\n");                                                                                           \
    }                                                                                                                               \
    printf("|     3 | tfEmpty           |    %02X |", getTfEmpty());                                                                \
    if (getTfEmpty()){                                                                                                              \
        printf(" Trace Emptied     |\n");                                                                                           \
    }                                                                                                                               \
    else{                                                                                                                           \
        printf(" Trace Populated   |\n");                                                                                           \
    }                                                                                                                               \
    printf("|    14 | tfStopOnWrap      |    %02X |", getTfStopOnWrap());                                                           \
    if (getTfStopOnWrap()){                                                                                                         \
        printf(" StopOnWrap Active |\n");                                                                                           \
    }                                                                                                                               \
    else{                                                                                                                           \
        printf("StopOnWrap Inactive|\n");                                                                                           \
    }                                                                                                                               \
    printf("|    27 | tfSinkError       |    %02X |", getTfSinkError());                                                            \
    if (getTfSinkError()){                                                                                                          \
        printf(" Error             |\n");                                                                                           \
    }                                                                                                                               \
    else{                                                                                                                           \
        printf(" No Error          |\n");                                                                                           \
    }                                                                                                                               \
    printf("| 31-28 | tfSink            |    %02X |", getTfSink());                                                                 \
    switch (getTfSink()){                                                                                                           \
        case 0:                                                                                                                     \
            printf(" Connected Sink    |\n");                                                                                       \
            break;                                                                                                                  \
        case 4:                                                                                                                     \
            printf(" SRAM Sink         |\n");                                                                                       \
            break;                                                                                                                  \
        case 5:                                                                                                                     \
            printf(" ATB Sink          |\n");                                                                                       \
            break;                                                                                                                  \
        case 6:                                                                                                                     \
            printf(" PIB Sink          |\n");                                                                                       \
            break;                                                                                                                  \
        case 7:                                                                                                                     \
            printf(" SBA Sink          |\n");                                                                                       \
            break;                                                                                                                  \
        case 8:                                                                                                                     \
            printf(" Funnel Sink       |\n");                                                                                       \
            break;                                                                                                                  \
        default:                                                                                                                    \
            printf(" Reserved          |\n");                                                                                       \
            break;                                                                                                                  \
    }                                                                                                                               \
    printf("+-------+-------------------+-------+-------------------+\n");                                                          \
}

#endif // SIFIVETRACEH
