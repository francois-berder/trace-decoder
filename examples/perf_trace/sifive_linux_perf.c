#include <stdio.h>
#include <sched.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/ioctl.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <sys/mman.h>

#include "sifive_trace.h"
#include "linux_sifive_perf.h"

#define PERF_IOCTL_NONE                         0
#define PERF_IOCTL_GET_HW_CNTR_MASK             100
#define PERF_IOCTL_START_CNTRS                  101     
#define PERF_IOCTL_STOP_CNTRS                   102
#define PERF_IOCTL_CFG_EVENT_CNTR               103     
#define PERF_IOCTL_ENABLE_HW_CNTR_ACCESS        104     
#define PERF_IOCTL_DISABLE_HW_CNTR_ACCESS       105     
#define PERF_IOCTL_ALLOC_SBA_DMA_BUFFER         106
#define PERF_IOCTL_FREE_SBA_DMA_BUFFER          107
#define PERF_IOCTL_GET_SBA_BUFFER_PHYS_ADDR     108
#define PERF_IOCTL_GET_SBA_BUFFER_SIZE          109f
#define PERF_IOCTL_READ_SBA_BUFFER              110
#define PERF_IOCTL_GET_EVENT_CNTR_INFO          111

// the following funciton don't seem to have a prototype int sched.h??

int sched_getcpu(void);
int getcpu(unsigned int *cpu,unsigned int *node);
int sched_getaffinity(pid_t pid,size_t cpusetsize,cpu_set_t *mask);

typedef struct {
    struct {
        uint8_t teInstruction;
        uint8_t teInstrumentation;
        uint8_t teStallOrOverflow;
        uint8_t teStallEnable;
        uint8_t teStopOnWrap;
        uint8_t teInhibitSrc;
        uint8_t teSyncMaxBTM;
        uint8_t teSyncMaxInst;
    } teControl;
    uint32_t itcTraceEnable;
    struct {
    	uint8_t tsCount;
        uint8_t tsDebug;
        uint8_t tsPrescale;
        uint8_t tsEnable;
        uint8_t tsBranch;
        uint8_t tsInstrumentation;
        uint8_t tsOwnership;
    } tsControl;
    struct {
        uint8_t tfStopOnWrap;
    } tfControl;
    struct {
        uint8_t  sink;
        uint32_t sinkBase;
        uint32_t sinkBaseH;
        uint32_t sinkSize;
    } sink;
    int      itcChannel;
} perfSettings_t;

static int coreID = -1;
static int numCores;
static int numFunnels;

extern struct TraceRegMemMap volatile * tmm[];
extern struct TfTraceRegMemMap volatile * fmm;

// Array of pointers to stimulus registers. Maps a core id to a stimulus register. Supports multi-core

static uint32_t volatile *perfStimulusCPUPairing[PERF_MAX_CORES];

// Pointer to master timestamp lower and upper registers

static uint32_t volatile *masterTsControl;
static uint32_t volatile *masterTs;

// Map core id to perf counters being recorded for that core

static uint32_t perfCounterCPUPairing[PERF_MAX_CORES];
static uint32_t perfMarkerVal;

static int perfTraceEnabled;
static perfCountType_t perfCountType;

static void (*write_trace_record_fn)(int core,perfRecordType_t recType,uint32_t perfCntrMask,volatile uint32_t *stimulus,void *this_fn,void *call_site);

static uint32_t SBABufferSize;
static unsigned long SBABufferPhysAddr;
static unsigned long SBABufferVirtAddr;

static uint32_t teSinkWP;

static uint32_t *SBABufferPtr;

static perfEvent *perfCntrLst;

static uint64_t *prevCntrVal[PERF_MAX_CORES];
static uint64_t cntrWrapVal[PERF_MAX_CNTRS];

static unsigned long mapped_teregs_base;

__attribute__((no_instrument_function)) static void perfEmitMarker(int core);
__attribute__((no_instrument_function)) static void perfGetInitialCnts();

__attribute__((no_instrument_function)) int perfTraceOn()
{
	perfTraceEnabled = 1;

    	perfEmitMarker(coreID);

	perfGetInitialCnts(coreID);

	return 0;
}

__attribute__((no_instrument_function)) int perfTraceOff()
{
	perfTraceEnabled = 0;

	return 0;
}

__attribute__((no_instrument_function)) static inline uint64_t perfReadTsCounter(int core)
{
    unsigned int tsL;
    unsigned int tsH;
    unsigned int tsH2;

    if (masterTs == NULL) {
        return 0UL;
    }

    do {
        tsH = masterTs[1];
        tsL = masterTs[0];
        tsH2 = masterTs[1];
    } while (tsH != tsH2);

    return (((uint64_t)tsH) << 32) + (uint64_t)tsL;
}

/* symbolic CSR names: */

#define CSR_CYCLE       0xc00
#define CSR_TIME        0xc01
#define CSR_INSTRET     0xc02
#define CSR_HPMCTR3     0xc03
#define CSR_HPMCTR4     0xc04
#define CSR_HPMCTR5     0xc05
#define CSR_HPMCTR6     0xc06

#define __ASM_STR(x)    #x

// need 32 and 64 bit versions of hpm_read()

#if __riscv_xlen == 32

#define hpm_read(csr) \
({ \
    unsigned int vh, vh1, vl; \
 \
    do { \
  __asm__ __volatile__("csrr %0, " __ASM_STR(csr) "+0x80" \
                       : "=r"(vh) \
                       : : "memory"); \
  __asm__ __volatile__("csrr %0, " __ASM_STR(csr) \
                       : "=r"(vl) \
                       : : "memory"); \
  __asm__ __volatile__("csrr %0, " __ASM_STR(csr) "+0x80" \
                       : "=r"(vh1) \
                       : : "memory"); \
    } while (vh != vh1); \
 \
    (((uint64_t)vh) << 32) | (uin64_t)vl; \
})

#else

#define hpm_read(csr) \
({ \
  register uint64_t __v; \
 \
  __asm__ __volatile__("csrr %0, " __ASM_STR(csr) \
                       : "=r"(__v) \
                       : : "memory"); \
 \
  __v; \
})

#endif

__attribute__((no_instrument_function)) void dump_trace_encoder(int core)
{
	printf("[%d]te_control_reg: 0x%08x\n",core,tmm[core]->te_control_register);
	printf("[%d]te_impl_reg: 0x%08x\n",core,tmm[core]->te_impl_register);
	printf("[%d]te_sinkbase_reg: 0x%08x\n",core,tmm[core]->te_sinkbase_register);
	printf("[%d]te_sinkbasehigh_reg: 0x%08x\n",core,tmm[core]->te_sinkbasehigh_register);
	printf("[%d]te_sinklimit_reg: 0x%08x\n",core,tmm[core]->te_sinklimit_register);
	printf("[%d]te_sink_wp_reg: 0x%08x\n",core,tmm[core]->te_sink_wp_register);
	printf("[%d]te_sink_rp_reg: 0x%08x\n",core,tmm[core]->te_sink_rp_register);
        printf("[%d]ts_control_reg: 0x%08x\n",core,tmm[core]->ts_control_register);
	printf("[%d]ts_lower_reg: 0x%08x\n",core,tmm[core]->ts_lower_register);
	printf("[%d]ts_upper_reg: 0x%08x\n",core,tmm[core]->ts_upper_register);
	printf("[%d]itc_traceenable_reg: 0x%08x\n",core,tmm[core]->itc_traceenable_register);
}

__attribute__((no_instrument_function)) void dump_trace_funnel()
{
	printf("tf_control_reg: 0x%08x\n",fmm->tf_control_register);
	printf("tf_impl_reg: 0x%08x\n",fmm->tf_impl_register);
	printf("tf_sinkbase_reg: 0x%08x\n",fmm->tf_sinkbase_register);
	printf("tf_sinkbasehigh_reg: 0x%08x\n",fmm->tf_sinkbasehigh_register);
	printf("tf_sinklimit_reg: 0x%08x\n",fmm->tf_sinklimit_register);
	printf("tf_sink_wp_reg: 0x%08x\n",fmm->tf_sink_wp_register);
	printf("tf_sink_rp_reg: 0x%08x\n",fmm->tf_sink_rp_register);
        printf("ts_control_reg: 0x%08x\n",fmm->ts_control_register);
	printf("ts_lower_reg: 0x%08x\n",fmm->ts_lower_register);
	printf("ts_upper_reg: 0x%08x\n",fmm->ts_upper_register);
}

__attribute__((no_instrument_function)) static inline uint64_t hpm_read_counter(int hpmCntrIndex)
{
    uint64_t val;

    switch (hpmCntrIndex) {
    case 0:
        val = hpm_read(CSR_CYCLE);
        break;
    case 1:
        val = perfReadTsCounter(0);	// always use core 0's TS counter
        break;
    case 2:
        val = hpm_read(CSR_INSTRET);
        break;
    case 3:
        val = hpm_read(CSR_HPMCTR3);
        break;
    case 4:
        val = hpm_read(CSR_HPMCTR4);
        break;
    case 5:
        val = hpm_read(CSR_HPMCTR5);
        break;
    case 6:
        val = hpm_read(CSR_HPMCTR6);
        break;
    default:
        val = 0;
    }

    return val;
}

__attribute__((no_instrument_function)) static void perfEmitMarker(int core)
{
    // do we need to emit mapping of events to counters?

    volatile uint32_t *stimulus = perfStimulusCPUPairing[core];
    uint32_t perfCntrMask = perfCounterCPUPairing[core];

    // block until room in FIFO
    while (*stimulus == 0) { /* empty */ }

    // write the marker value
    *stimulus = perfMarkerVal;

    // block until room in FIFO
    while (*stimulus == 0) { /* empty */ }

    // write the type of the counts (raw, delta, deltaXOR)
    ((uint8_t*)stimulus)[3] = (uint8_t)perfCountType;

    // block until room in FIFO
    while (*stimulus == 0) { /* empty */ }

    // write the first counter mask
    *stimulus = perfCntrMask;

    // we want to write out how the counters are programmed here. Can't simply read the
    // programming because it is only accessable in Machine mode. We are in user mode.
    // So we use the saved perfCntrList - but we only have one for all cores.

    int counter = 0;

    while (perfCntrMask != 0) {
        if (perfCntrMask & 1) {
            switch (perfCntrLst[counter].type) {
            case 0:
            case 1:
                // block until room in FIFO

                while (*stimulus == 0) { /* empty */ }

                *stimulus = perfCntrLst[counter].type;

                while (*stimulus == 0) { /* empty */ }

                *stimulus = perfCntrLst[counter].code;

                // block until room in FIFO
                while (*stimulus == 0) { /* empty */ }

                *stimulus = (uint32_t)perfCntrLst[counter].ctrInfo;
                break;
             case 2:
                // block until room in FIFO

                while (*stimulus == 0) { /* empty */ }

                *stimulus = perfCntrLst[counter].type;

                while (*stimulus == 0) { /* empty */ }

                *stimulus = (uint32_t)perfCntrLst[counter].event_data;

                while (*stimulus == 0) { /* empty */ }

                if (sizeof(long) > 4) {
                    *stimulus = (uint32_t)(perfCntrLst[counter].event_data >> 32);
                }
                else {
                    *stimulus = 0;
                }

                // block until room in FIFO
                while (*stimulus == 0) { /* empty */ }

                if (counter == 1) {
                    *stimulus = (uint32_t)(((*masterTsControl >> 24) - 1) << 12);
                }
                else {
                    *stimulus = (uint32_t)perfCntrLst[counter].ctrInfo;
                }
                break;
            default:
                printf("Error: perfEmitMarker(): Invalid counter type: %d\n",perfCntrLst[counter].type);
                break;
            }
        }

        perfCntrMask >>= 1;
        counter += 1;
    }
}

__attribute__((no_instrument_function)) static int perfSetChannel(int core,int channel)
{
    // pair a performance counter mask to a channel

    // check to see if the channel is valid
    if ((channel < 0) || (channel > 31)) {
        return 1;
    }

    // enable the itc channel requested

    setITCTraceEnable(core, (getITCTraceEnable(core)) | 1 << (channel));

    // set the value-pair since we didn't fail at enabling

    perfStimulusCPUPairing[core] = (uint32_t*)&tmm[core]->itc_stimulus_register[channel];

    return 0;
}

int getProcessCore()
{
        int cpu;
        int rc;
        int nproc;

        cpu_set_t cpuset;

        __CPU_ZERO_S(sizeof(cpu_set_t),&cpuset);

        rc = sched_getaffinity(0,sizeof(cpu_set_t),&cpuset);
        if (rc < 0) {
            printf("Error: getProcessCore(): Could not get process affinity\n");

            return -1;
        }

        nproc = sysconf(_SC_NPROCESSORS_ONLN);

        unsigned int m = 0;

        for (int i = 0; i < nproc; i++) {
                if (__CPU_ISSET_S(i,sizeof(cpu_set_t),&cpuset)) {
                        m  |=  (1 << i);
			cpu = i;
                }
        }

	if ((m == 0) || (m & (m-1)) != 0) {
		// process is not tied to a single core
                printf("Error: getProcessCore(): Process affinity is not a single cpu core\n");

		return -1;
	}

        return cpu;
}

__attribute__((no_instrument_function)) static int initTraceEncoder(int core,perfSettings_t *settings)
{
    if ((core < 0) || (core > numCores)) {
        printf("Error: initTraceEncoder(): Invalid core number: %d\n",core);

        return 1;
    }

    if (settings == NULL) {
        printf("Error: initTraceEncoder(): NULL settings argument\n");

        return 1;
    }

    // reset trace encoder

    if (core == numCores) {
        for (int i = 0; i < numCores; i++) {
            setTeActive(i,0);
            setTeActive(i,1);
        }
    }
    else {
        setTeActive(core,0);
        setTeActive(core,1);
    }

    unsigned int teSink;

    // If there is one or more funnels, teSink must be a funnel

    if (numFunnels > 0) {
        teSink = 8;
    }
    else {
        teSink = settings->sink.sink;
    }

    // set teEnable, clear teTracing. The trace encoder is enabled, but not activily tracing. Set everything else as specified in settings

    if (core == numCores) {
        // if core == numCores, do all cores

        for (int i = 0; i < numCores; i++) {
            setTeControl(i,
                         (teSink << 28)                                |
                         (settings->teControl.teSyncMaxInst << 20)     |
                         (settings->teControl.teSyncMaxBTM << 16)      |
                         (settings->teControl.teInhibitSrc << 15)      |
                         (settings->teControl.teStopOnWrap << 14)      |
                         (settings->teControl.teStallEnable << 13)     |
                         (settings->teControl.teStallOrOverflow << 12) |
                         (settings->teControl.teInstrumentation << 7)  |
                         (settings->teControl.teInstruction << 4)      |
                         (0x03 << 0));
        }
    }
    else {
        setTeControl(core,
                     (teSink << 28)                                |
                     (settings->teControl.teSyncMaxInst << 20)     |
                     (settings->teControl.teSyncMaxBTM << 16)      |
                     (settings->teControl.teInhibitSrc << 15)      |
                     (settings->teControl.teStopOnWrap << 14)      |
                     (settings->teControl.teStallEnable << 13)     |
                     (settings->teControl.teStallOrOverflow << 12) |
                     (settings->teControl.teInstrumentation << 7)  |
                     (settings->teControl.teInstruction << 4)      |
                     (0x03 << 0));
    }

    // clear trace buffer read/write pointers

    if (numFunnels <= 0) {
        // see if we have system memory sink as an option. If so, set base and limit regs

	if (core == numCores) {
		for (int i = 0; i < numCores; i++) {
		        if (getTeImplHasSBASink(i) && (teSink == 7)) {
		            // SBA sink

		            setTeSinkBase(i,settings->sink.sinkBase);
		            setTeSinkBaseHigh(i,settings->sink.sinkBaseH);

		            // SinkLimit reg holds the upper address, not the count!!

		            if (settings->sink.sinkSize == 0) {
		                setTeSinkLimit(i,settings->sink.sinkBase);
		            }
		            else {
		                uint64_t sinkLimit;

		                sinkLimit = (uint64_t)settings->sink.sinkBase + (uint64_t)(settings->sink.sinkSize-1);

		                if ((sinkLimit >> 32) != 0) {
		                    printf("Warning: initTraceEncoder(): SBA sink buffer extends across 32 bit boundry. Truncated\n");

		                    sinkLimit = 0xfffffffcUL;
		                }

		                setTeSinkLimit(i,sinkLimit);
		            }

		            teSinkWP = settings->sink.sinkBase;
		        }
		        else {
		            teSinkWP = 0;
		        }

		        setTeSinkWp(i,teSinkWP);
		        setTeSinkRp(i,0);
		}
	}
	else {
		if (getTeImplHasSBASink(core) && (teSink == 7)) {
			// SBA sink

			setTeSinkBase(core,settings->sink.sinkBase);
			setTeSinkBaseHigh(core,settings->sink.sinkBaseH);

			// SinkLimit reg holds the upper address, not the count!!

			if (settings->sink.sinkSize == 0) {
				setTeSinkLimit(core,settings->sink.sinkBase);
			}
			else {
				uint64_t sinkLimit;

				sinkLimit = (uint64_t)settings->sink.sinkBase + (uint64_t)(settings->sink.sinkSize-1);

				if ((sinkLimit >> 32) != 0) {
					printf("Warning: initTraceEncoder(): SBA sink buffer extends across 32 bit boundry. Truncated\n");

					sinkLimit = 0xfffffffcUL;
				}

				setTeSinkLimit(core,sinkLimit);
			}

			teSinkWP = settings->sink.sinkBase;
		}
		else {
			teSinkWP = 0;
		}

		setTeSinkWp(core,teSinkWP);
		setTeSinkRp(core,0);
	}

    // setup the timestamp unit. If multicore, only one core or one funnel will be the source

    if (core == numCores) {
        for (int i = 0; i < numCores; i++) {
            // reset ts

            setTsActive(i, 0);
            setTsActive(i, 1);

            uint32_t tsType;
            uint32_t tsDebug;
            uint32_t tsPrescale;
            uint32_t tsEnable;
            uint32_t tsCount;

            tsType = getTsType(i);

            switch (tsType) {
            case 1:
                // external ts

                tsCount = 0;
                tsDebug = 0;
                tsPrescale = 0;
                tsEnable = settings->tsControl.tsEnable;

		masterTsControl = &tmm[i]->ts_control_register;
                masterTs = &tmm[i]->ts_lower_register;
                break;
            case 2:
            case 3:
                // internal ts

                tsCount = settings->tsControl.tsCount;
                tsDebug = settings->tsControl.tsDebug;
                tsPrescale = settings->tsControl.tsPrescale;
                tsEnable = settings->tsControl.tsEnable;

		masterTsControl = &tmm[i]->ts_control_register;
                masterTs = &tmm[i]->ts_lower_register;
                break;
            case 4:
                // slave ts

                tsCount = 0;
                tsDebug = 0;
                tsPrescale = 0;
                tsEnable = settings->tsControl.tsEnable;
                break;
            case 0:
            default:
                // no ts, or reserved

                tsCount = 0;
                tsDebug = 0;
                tsPrescale = 0;
                tsEnable = 0;
                break;
            }

            tsConfig(i,
                     tsDebug,
                     tsPrescale,
                     settings->tsControl.tsBranch,
                     settings->tsControl.tsInstrumentation,
                     settings->tsControl.tsOwnership);

            setTsCount(i,tsCount);
            setTsEnable(i,tsEnable);

            // setup the itc channel enables

            setITCTraceEnable(i,settings->itcTraceEnable);
        }
    }
    else {
        // reset ts

        setTsActive(core, 0);
        setTsActive(core, 1);

        uint32_t tsType;
        uint32_t tsDebug;
        uint32_t tsPrescale;
        uint32_t tsEnable;
        uint32_t tsCount;

        tsType = getTsType(core);

        switch (tsType) {
        case 1:
            // external ts

            tsCount = 0;
            tsDebug = 0;
            tsPrescale = 0;
            tsEnable = settings->tsControl.tsEnable;

            masterTsControl = &tmm[core]->ts_control_register;
            masterTs = &tmm[core]->ts_lower_register;
            break;
        case 2:
        case 3:
            // internal ts

            tsCount = settings->tsControl.tsCount;
            tsDebug = settings->tsControl.tsDebug;
            tsPrescale = settings->tsControl.tsPrescale;
            tsEnable = settings->tsControl.tsEnable;

            masterTsControl = &tmm[core]->ts_control_register;
            masterTs = &tmm[core]->ts_lower_register;
            break;
        case 4:
            // slave ts

            tsCount = 0;
            tsDebug = 0;
            tsPrescale = 0;
            tsEnable = settings->tsControl.tsEnable;
            break;
        case 0:
        default:
            // no ts, or reserved

            tsCount = 0;
            tsDebug = 0;
            tsPrescale = 0;
            tsEnable = 0;
            break;
        }

        tsConfig(core,
                 tsDebug,
                 tsPrescale,
                 settings->tsControl.tsBranch,
                 settings->tsControl.tsInstrumentation,
                 settings->tsControl.tsOwnership);

        setTsCount(core,tsCount);
        setTsEnable(core,tsEnable);

        // setup the itc channel enables

        setITCTraceEnable(core,settings->itcTraceEnable);
    }

    return 0;
}

__attribute__((no_instrument_function)) static int initTraceFunnels(perfSettings_t *settings)
{
    if (numFunnels == 0) {
	return 0;
    }

    if (numFunnels > 1) {
        printf("Error: initTraceFunnels(): Currently don't support more than one funnel\n");

    	return 1;
    }

    if (settings == NULL) {
        printf("Error: initTraceFunnels(): NULL settings argument\n");

        return 1;
    }

    // currently only support one or no funnels

    // reset trace funnel

    setTfActive(0);
    setTfActive(1);

    setTfControl((settings->sink.sink << 28) | (settings->tfControl.tfStopOnWrap << 14) | (0x03 << 0));

    // init timestamp unit

    // reset ts

    fmm->ts_control_register &= 0xfffffffe; // clear tsActive
    fmm->ts_control_register |= 0x00000001; // set tsActive

    uint32_t tsType;
    uint32_t tsPrescale;
    uint32_t tsCount;

    tsType = (fmm->ts_control_register & 0x00000070) >> 4;

    switch (tsType) {
    case 1:
        // external ts

        tsCount = 0;
        tsPrescale = 0;

        masterTsControl = &fmm->ts_control_register;
        masterTs = &fmm->ts_lower_register;
        break;
    case 2:
    case 3:
        // internal ts

        tsCount = settings->tsControl.tsCount;
        tsPrescale = settings->tsControl.tsPrescale;

        masterTsControl = &fmm->ts_control_register;
        masterTs = &fmm->ts_lower_register;
        break;
    case 4:
        // slave ts

        tsCount = 0;
        tsPrescale = 0;
        break;
    case 0:
    default:
        // no ts, or reserved

        tsCount = 0;
        tsPrescale = 0;
        break;
    }

    fmm->ts_control_register = (tsPrescale << 8) | 1;

    fmm->ts_control_register |= (tsCount << 1);

    // clear trace buffer read/write pointers

    // see if we have system memory sink as an option. If so, set base and limit regs

    if (getTfHasSBASink() && (settings->sink.sink == 7)) {
        // SBA sink

        setTfSinkBase(settings->sink.sinkBase);
        setTfSinkBaseHigh(settings->sink.sinkBaseH);

        if (settings->sink.sinkSize == 0) {
            setTfSinkLimit(settings->sink.sinkBase);
        }
        else {
            uint64_t sinkLimit;

            sinkLimit = (uint64_t)settings->sink.sinkBase + (uint64_t)(settings->sink.sinkSize-1);

            if ((sinkLimit >> 32) != 0) {
                printf("Warning: initTraceEncoder(): SBA sink buffer extends across 32 bit boundry. Truncated\n");

                sinkLimit = 0xfffffffcUL;
            }

            setTfSinkLimit(sinkLimit);
        }

	teSinkWP = settings->sink.sinkBase;
    }
    else {
	teSinkWP = 0;
    }

    setTfSinkWp(teSinkWP);
    setTfSinkRp(0);

    return 0;
}

__attribute__((no_instrument_function)) static int initTraceEngineDefaults()
{
    perfSettings_t settings;

    settings.teControl.teInstruction = TE_INSTRUCTION_NONE;
    settings.teControl.teInstrumentation = TE_INSTRUMENTATION_ITC;
    settings.teControl.teStallOrOverflow = 0;
    settings.teControl.teStallEnable = 0;
    settings.teControl.teStopOnWrap = 1;
    settings.teControl.teInhibitSrc = 0;
    settings.teControl.teSyncMaxBTM = TE_SYNCMAXBTM_OFF;
    settings.teControl.teSyncMaxInst = TE_SYNCMAXINST_OFF;

    settings.tfControl.tfStopOnWrap = 1;

    settings.itcTraceEnable = 0x00000000;

    settings.tsControl.tsCount = 1;
    settings.tsControl.tsDebug = 0;
    settings.tsControl.tsPrescale = TS_PRESCL_1;
    settings.tsControl.tsEnable = 1;
    settings.tsControl.tsBranch = BRNCH_ALL;
    settings.tsControl.tsInstrumentation = 1;
    settings.tsControl.tsOwnership = 1;

    settings.sink.sink = TE_SINK_SRAM;
    settings.sink.sinkBase = 0;
    settings.sink.sinkBaseH = 0;
    settings.sink.sinkSize = 0;

    settings.itcChannel = 0;

    int rc;

    // initialize all trace encoders to defaults. Trace encoder are disabled

    rc = initTraceEncoder(core,&settings);
    if (rc!= 0) {
        return 1;
    }

    // initializae funnels. Funnels are enabled

    rc = initTraceFunnels(&settings);
    if (rc!= 0) {
        return 1;
    }

    return 0;
}

__attribute__((no_instrument_function)) static void perfGetInitialCnts(int core)
{
    uint32_t perfCntrMask;

    perfCntrMask = perfCounterCPUPairing[core];

    for (int i = 0; perfCntrMask; i++) {
        if (perfCntrMask & 1) {
            if (i == 1) {
            	prevCntrVal[core][i] = perfReadTsCounter(core);
            }
            else {
            	prevCntrVal[core][i] = hpm_read_counter(i);
            }
        }

        perfCntrMask >>= 1;
    }
}

__attribute__((no_instrument_function)) static int perfInitCntrs(int core,int devFD,perfEvent *pCntrLst,int nCntrs)
{
    if (perfCntrLst != NULL) {
        free(perfCntrLst);
        perfCntrLst = NULL;
    }

    uint32_t perfCntrMask = 0;

    // Get the HW counter mask

    if ((pCntrLst != NULL) && (nCntrs != 0)) {
        uint32_t hwPerfCntrMask;
        int rc;

        rc = ioctl(devFD,PERF_IOCTL_GET_HW_CNTR_MASK,&hwPerfCntrMask);
	if (rc < 0) {
		printf("Error: perfInitCntrs(): ioctl PERF_IOCTL_GET_HW_CNTR_MASK failed\n");
		return 1;
	}

        int nextCntr = 3;

	// allocate buffer for perf cntr list and then psuedo sort them, assigning counter numbers to each

	perfCntrLst = (perfEvent*)malloc(sizeof(perfEvent)*nCntrs);
        if (perfCntrLst == NULL) {
            printf("Error: perfInitCntrs(): malloc() failed\n");

            return 1;
        }

        // mark all perfCntrLst cntrs as unused

        for (int i = 0; i < nCntrs; i++) {
		perfCntrLst[i].ctrIdx = -1;
        }

        int nxtCntrIdx = 3;

        for (int i = 0; i < nCntrs; i++) {
            if ((pCntrLst[i].type == 0) && (pCntrLst[i].code == HW_CPU_CYCLES)) {
                if (perfCntrLst[0].ctrIdx == -1) {
                    perfCntrLst[0] = pCntrLst[i]; <= why isn't this fixing type?
                    perfCntrLst[0].ctrIdx = 0;

                    perfCntrMask |= 1 << 0;
                }
            }
            else if ((pCntrLst[i].type == 0) && (pCntrLst[i].code == HW_TIMESTAMP)) {
                if (perfCntrLst[1].ctrIdx == -1) {
                    perfCntrLst[1] = pCntrLst[i];
                    perfCntrLst[1].ctrIdx = 1;

                    perfCntrMask |= 1 << 1;
                }
            }
            else if ((pCntrLst[i].type == 0) && (pCntrLst[i].code == HW_INSTRUCTIONS)) {
                if (perfCntrLst[2].ctrIdx == -1) {
                    perfCntrLst[2] = pCntrLst[i];
                    perfCntrLst[2].ctrIdx = 2;

                    perfCntrMask |= 1 << 2;
                }
            }
            else if (hwPerfCntrMask & (1 << i)) {
                perfCntrLst[nxtCntrIdx] = pCntrLst[i];
                perfCntrLst[nxtCntrIdx].ctrIdx = nxtCntrIdx;

                perfCntrMask |= 1 << nxtCntrIdx;;
		nxtCntrIdx += 1;
            }
            else {
		printf("Error: perfInitCntrs(): number of HW counters exceeded by counter definitions\n");
		printf("Ignorming counter deffinition %d\n",i+1);
            }
        }

	uint32_t tmpMask;

        // Configure counters

	// Disable cntr access while we program the counters

	tmpMask = hwPerfCntrMask;

        rc = ioctl(devFD,PERF_IOCTL_DISABLE_HW_CNTR_ACCESS,&tmpMask);
	if (rc < 0) {
		printf("Error: perfInitCntrs(): ioctl PERF_IOCTL_DISABLE_HW_CNTR_ACCESS failed\n");
		return 1;
	}

	// Stop counters

	tmpMask = hwPerfCntrMask;

        rc = ioctl(devFD,PERF_IOCTL_STOP_CNTRS,&tmpMask);
	if (rc < 0) {
		printf("Error: perfInitCntrs(): ioctl PERF_IOCTL_STOP_CNTRS failed\n");
		return 1;
	}

	// Configure counters. This also get the size and CSR for the event

        for (int i = 0; i < nxtCntrIdx; i++) {
            if (perfCntrMask & (1 << i)) {
		// the ioctl() below fills in the cntrInfo field in the perfCntrLst[i] entry

                rc = ioctl(devFD,PERF_IOCTL_CFG_EVENT_CNTR,&perfCntrLst[i]);
		if (rc < 0) {
			printf("Error: perfInitCntrs(): ioctl PERF_IOCTL_CFG_EVENT_CNTR failed\n");
			return 1;
		}

		// fill in array of counter wrap values

		int cntrBits;

                cntrBits = ((perfCntrLst[i].ctrInfo >> 12) & 0x3f) + 1;

		if (cntrBits < 64) {
                    cntrWrapVal[i] = (uint64_t)1 << cntrBits;
                }
                else {
                    // cntr is 64 bits and we can't go any bigger

                    cntrWrapVal[i] = 0;
                }
            }
        }

	// Start counters

	tmpMask = perfCntrMask;

        rc = ioctl(devFD,PERF_IOCTL_START_CNTRS,&tmpMask);
	if (rc < 0) {
		printf("Error: perfInitCntrs(): ioctl PERF_IOCTL_START_CNTRS failed\n");
		return 1;
	}

	// Enable cntr access

	tmpMask = perfCntrMask;

        rc = ioctl(devFD,PERF_IOCTL_ENABLE_HW_CNTR_ACCESS,&tmpMask);
	if (rc < 0) {
		printf("Error: perfInitCntrs(): ioctl PERF_IOCTL_ENABLE_CNTR_ACCESS failed\n");
		return 1;
	}

	perfCounterCPUPairing[core] = perfCntrMask;

        if (prevCntrVal[core] == NULL) {
            prevCntrVal[core] = (uint64_t *)malloc(sizeof(uint64_t) * PERF_MAX_CNTRS);

            if (prevCntrVal[core] == NULL) {
                printf("Error: perfInitCntrs(): malloc() failed\n");
                return 1;
            }
        }

        for (int i = 0; i < PERF_MAX_CNTRS; i++) {
            prevCntrVal[core][i] = 0;
        }
    }
    else {
        perfCntrLst = NULL;
	perfCounterCPUPairing[core] = 0;

        if (prevCntrVal[core] != NULL) {
            free(prevCntrVal[core]);
            prevCntrVal[core] = NULL;
        }
    }

    return 0;
}

__attribute__((no_instrument_function)) static int perfDeviceDriverInit(int core,uint32_t sink_size,perfEvent *pCntrList,int nCntrs)
{
    int rc;

    // open device driver and use it to enable perf CSR reads and map the trace
    // encoder into application memory space

    int devFD;

    devFD = open("/dev/traceperf",O_RDWR | O_SYNC);
    if (devFD < 0) {
        printf("Error: perfDeviceDriverInit(): could not open device /dev/traceperf\n");

        return 1;
    }

    size_t  length = 0x19000; // size of block in memory space reserved for trace encoder

    unsigned long dev_baseUL;
    dev_baseUL = (unsigned long)tmm[0];

    // map trace engines/funnels into user space

    mapped_teregs_base = (unsigned long)mmap(0,length,PROT_READ|PROT_WRITE,MAP_SHARED,devFD,dev_baseUL);
    if ((void*)mapped_teregs_base == MAP_FAILED) {
        printf("Error: perfDeviceDriverInit(): Memmap failure\n");

         close(devFD);

         return 1;
    }

    // compute all the remapped addresses of the trace engines and funnels

    for (int i = 0; i < numCores; i++) {
        tmm[i] = (volatile struct TraceRegMemMap*)((unsigned long)tmm[i] - dev_baseUL + mapped_teregs_base);
    }

    fmm = (volatile struct TfTraceRegMemMap*)((unsigned long)fmm - dev_baseUL + mapped_teregs_base);

    // Init perf counters. This will program the counters and also set the perfCounteCPUPairing[] entry for the core to
    // the counter mask for that core

    rc = perfInitCntrs(core,devFD,pCntrList,nCntrs);
    if (rc != 0) {
        close(devFD);

        return rc;
    }

    if (sink_size > 0) {
        // allocate SBA trae buffer

        unsigned long buff_addr;
        uint32_t buff_size;

        // Before allocating an SBA buffer, see if one has already been allocated and it is big enough

        buff_size = 0;

        rc = ioctl(devFD,PERF_IOCTL_GET_SBA_BUFFER_SIZE,&buff_size);
        if (rc < 0) {
            printf("Error: perfDeviceDriverInit(): get buffer size failed\n");
            close(devFD);

            return 1;
        }

        if (buff_size > 0) {

            // have a buffer, see if it is big enough

            if (buff_size < sink_size) {
                // Not big enough. Free

                buff_size = 0;

                rc = ioctl(devFD,PERF_IOCTL_FREE_SBA_DMA_BUFFER,&buff_size);
                if (rc < 0) {
                    printf("Error: perDevicefInit(): free dma buffer failed\n");
                    close(devFD);

                    return 1;
                }

                SBABufferSize = 0;
                SBABufferPhysAddr = 0;
                SBABufferVirtAddr = 0;
            }
            else {
		uint64_t arg;

                arg = 0;

                rc = ioctl(devFD,PERF_IOCTL_GET_SBA_BUFFER_PHYS_ADDR,&arg);
                if (rc < 0) {
                    printf("Error: perfDeviceDriverInit(): ioctl PERF_IOCTL_GET_SBA_BUFFER_PHYS_ADDR failed\n");
                    close(devFD);

                    return 1;
                }

		SBABufferSize = sink_size; // buffer is at least sink_size bytes. We only want sink_size bytes;
                SBABufferPhysAddr = arg;
            }

        }

        if (SBABufferSize == 0) {
            uint64_t arg;

            // need to allocate a buffer

            arg = sink_size;

            rc = ioctl(devFD,PERF_IOCTL_ALLOC_SBA_DMA_BUFFER,&arg);
            if (rc < 0) {
                printf("Error: perfDeviceDriverInit(): ioctl PERF_IOCTL_ALLOC_SBA_DMA_BUFFER failed\n");
                close(devFD);

                return 1;
            }

            // At this point, arg already has the phys address of the buffer, but we read it again anyway

            rc = ioctl(devFD,PERF_IOCTL_GET_SBA_BUFFER_PHYS_ADDR,&arg);
            if (rc < 0) {
                printf("Error: perfDeviceDriverInit(): ioctl PERF_IOCTL_GET_SBA_BUFFER_PHYS_ADDR failed\n");
                close(devFD);

                return 1;
            }

            SBABufferPhysAddr = arg;

            rc = ioctl(devFD,PERF_IOCTL_GET_SBA_BUFFER_SIZE,&SBABufferSize);
            if (rc < 0) {
                printf("Error: perfDeviceDriverInit(): ioctl PERF_IOCTL_GET_SBA_BUFFER_SIZE failed\n");
                close(devFD);

                return 1;
            }
        }
    }
    else {
        SBABufferSize = 0;
        SBABufferPhysAddr = 0;
        SBABufferVirtAddr = 0;
    }

    // closing the device will not invalidate the memory mapping, or CSR enables

    close(devFD);

    // Init all trace engines and funnels into a known state

    rc = initTraceEngineDefaults();
    if (rc != 0) {
        return 1;
    }

    return 0;
}

__attribute__((no_instrument_function)) int perfInit(int num_cores,int num_funnels)
{
    perfTraceEnabled = 0;

    if (numCores > 0) {
        // this routine should only be called once!!

        printf("Error: perfInit(): perfInit() should only be called once\n");

        return 1;
    }

    if (num_cores <= 0) {
        printf("Error: perfInit(): Invalid num_cores argument: %d\n",num_cores);

        return 1;
    }

    if (num_funnels < 0) {
        printf("Error: perfInit(): Invalid num_funnels argument: %d\n",num_funnels);

        return 1;
    }

    numCores = num_cores;
    numFunnels = num_funnels;

    for (int i = 0; i < sizeof perfStimulusCPUPairing / sizeof perfStimulusCPUPairing[0]; i++) {
    	perfStimulusCPUPairing[i] = NULL;
    }

    for (int i = 0; i < sizeof perfCounterCPUPairing / sizeof perfCounterCPUPairing[0]; i++) {
    	perfCounterCPUPairing[i] = 0;
    }

    for (int i = 0; i < sizeof prevCntrVal / sizeof prevCntrVal[0]; i++) {
    	prevCntrVal[i] = NULL;
    }

    for (int i = 0; i < sizeof cntrWrapVal / sizeof cntrWrapVal[0]; i++) {
    	cntrWrapVal[i] = 0;
    }

    // Figure out wich core we are running on, and make sure affinity is set to only
    // this core.

    coreID = getProcessCore();

    if ((coreID < 0) || (coreID >= numCores)) {
	printf("Error: perfInit(): coreID invalid: %d\n",coreID);

	return 1;
    }

    return 0;
}

__attribute__((no_instrument_function)) static int perfTraceEngineInit(int core,perfSettings_t *settings)
{
    if ((core < 0) || (core >= numCores)) {
        return 1;
    }

    if (settings == NULL) {
        return 1;
    }

    if (numFunnels > 1) {
    	// currently don't support more than one funnel (multi-cluster)

    	return 1;
    }

    // set channel pairing

    int rc;

    rc = perfSetChannel(core,settings->itcChannel);
    if (rc != 0) {
        return 1;
    }

    // doctor up cntrInfo for the timestamp

    if ((masterTsControl != NULL) && (perfCounterCPUPairing[core] & (1 << 1))) {
        int tsBits;

        tsBits = (int)(*masterTsControl >> 24);

        if (tsBits < 64) {
            cntrWrapVal[1] = 1UL << tsBits;
        }
        else {
            cntrWrapVal[1] = 0;
        }

	perfCntrLst[1].ctrInfo = (tsBits-1) << 12;
    }

    rc = initTraceEncoder(core,settings);
    if (rc != 0) {
        return 1;
    }

    // trace encoder for core is now initted, but disabled

    // initializae funnels. Funnels are enabled

    rc = initTraceFunnels(settings);
    if (rc!= 0) {
        return 1;
    }

    // enable trace encoder

    setTeTracing(core,1);

    return 0;
}

__attribute__((no_instrument_function)) static inline void write_trace_record_raw(int core,perfRecordType_t recType,uint32_t perfCntrMask,volatile uint32_t *stimulus,void *this_fn,void *call_site)
{

    // block until room in FIFO
    while (*stimulus == 0) { /* empty */ }

    // need to write an record type ID tag so we know this is a func entry, eixit, or whatever

    ((uint8_t*)stimulus)[3] = (uint8_t)recType;

    uint32_t fnL;
    uint32_t fnH;
    uint32_t csL;
    uint32_t csH;

    if (sizeof(void*) > sizeof(uint32_t)) {
        fnL = ((uint32_t*)&this_fn)[0];
        fnH = ((uint32_t*)&this_fn)[1];

        csL = ((uint32_t*)&call_site)[0];
        csH = ((uint32_t*)&call_site)[1];
    }
    else {
        fnL = *(uint32_t*)&this_fn;
        fnH = 0;

        csL = *(uint32_t*)&call_site;
        csH = 0;
    }

    if (fnH != 0) {
        // block until room in FIFO
        while (*stimulus == 0) { /* empty */ }

        // write the first 32 bits, set bit 0 to indicate 64 bit address
        *stimulus = fnL | 1;

    	// block until room in FIFO
        while (*stimulus == 0) { /* empty */ }

        // write the second 32 bits
        *stimulus = fnH;
    }
    else {
        // block until room in FIFO
        while (*stimulus == 0) { /* empty */ }

        // write 32 bit pc
        *stimulus = fnL;
    }

    if ((recType == perfRecord_FuncEnter) || (recType == perfRecord_FuncExit)) {
        if (csH != 0) {
            // block until room in FIFO
            while (*stimulus == 0) { /* empty */ }

            // write the first 32 bits, set bit 0 to indicate 64 bit address
            *stimulus = csL | 1;
    
            // block until room in FIFO
            while (*stimulus == 0) { /* empty */ }

            // write the second 32 bits
            *stimulus = csH;
        }
        else {
            // block until room in FIFO
            while (*stimulus == 0) { /* empty */ }

            // write 32 bit pc
            *stimulus = csL;
        }
    }

    if (prevCntrVal[core] == NULL) {
        return;
    }

    int perfCntrIndex = 0;

    while (perfCntrMask != 0) {
        if (perfCntrMask & 1) {
            uint64_t newCntrVal;

            if (perfCntrIndex == 1) {
            	newCntrVal = perfReadTsCounter(core);
            }
            else {
            	newCntrVal = hpm_read_counter(perfCntrIndex);
            }

            // block until room in FIFO
            while (*stimulus == 0) { /* empty */ }

            // write the first 32 bits
            *stimulus = (uint32_t)newCntrVal;

            // write an extra 16 bits if needed; if it has non-zero data

            newCntrVal >>= 32;

            if (newCntrVal != 0) {
                // block until room in FIFO
                while (*stimulus == 0) { /* empty */ }

                // write extra  bits

                ((uint16_t*)stimulus)[1] = (uint16_t)newCntrVal;
            }
        }

        perfCntrIndex += 1;
        perfCntrMask >>= 1;
    }
}

__attribute__((no_instrument_function)) static inline void write_trace_record_delta(int core,perfRecordType_t recType,uint32_t perfCntrMask,volatile uint32_t *stimulus,void *this_fn,void *call_site)
{

    // block until room in FIFO
    while (*stimulus == 0) { /* empty */ }

    // need to write an record type ID tag so we know this is a func entry, eixit, or whatever

    ((uint8_t*)stimulus)[3] = (uint8_t)recType;

    uint32_t fnL;
    uint32_t fnH;
    uint32_t csL;
    uint32_t csH;

    if (sizeof(void*) > sizeof(uint32_t)) {
        fnL = ((uint32_t*)&this_fn)[0];
        fnH = ((uint32_t*)&this_fn)[1];

        csL = ((uint32_t*)&call_site)[0];
        csH = ((uint32_t*)&call_site)[1];
    }
    else {
        fnL = *(uint32_t*)&this_fn;
        fnH = 0;

        csL = *(uint32_t*)&call_site;
        csH = 0;
    }

    if (fnH != 0) {
        // block until room in FIFO
        while (*stimulus == 0) { /* empty */ }

        // write the first 32 bits, set bit 0 to indicate 64 bit address
        *stimulus = fnL | 1;

    	// block until room in FIFO
        while (*stimulus == 0) { /* empty */ }

        // write the second 32 bits
        *stimulus = fnH;
    }
    else {
        // block until room in FIFO
        while (*stimulus == 0) { /* empty */ }

        // write 32 bit pc
        *stimulus = fnL;
    }

    if ((recType == perfRecord_FuncEnter) || (recType == perfRecord_FuncExit)) {
        if (csH != 0) {
            // block until room in FIFO
            while (*stimulus == 0) { /* empty */ }

            // write the first 32 bits, set bit 0 to indicate 64 bit address
            *stimulus = csL | 1;
    
            // block until room in FIFO
            while (*stimulus == 0) { /* empty */ }

            // write the second 32 bits
            *stimulus = csH;
        }
        else {
            // block until room in FIFO
            while (*stimulus == 0) { /* empty */ }

            // write 32 bit pc
            *stimulus = csL;
        }
    }

    if (prevCntrVal[core] == NULL) {
        return;
    }

    int perfCntrIndex = 0;

    while (perfCntrMask != 0) {
        if (perfCntrMask & 1) {
            uint64_t newCntrVal;
            uint64_t oldCntrVal;
            uint64_t delta;

            if (perfCntrIndex == 1) {
            	newCntrVal = perfReadTsCounter(core);
            }
            else {
            	newCntrVal = hpm_read_counter(perfCntrIndex);
            }

            oldCntrVal = prevCntrVal[core][perfCntrIndex];

            if (newCntrVal > oldCntrVal) {
                delta = newCntrVal - oldCntrVal;
            }
            else {
                delta = newCntrVal + cntrWrapVal[perfCntrIndex] - oldCntrVal;
            }

            prevCntrVal[core][perfCntrIndex] = newCntrVal;

            // block until room in FIFO
            while (*stimulus == 0) { /* empty */ }

            // write the first 32 bits
            *stimulus = (uint32_t)delta;

            // write an extra 16 bits if needed; if it has non-zero data

            delta >>= 32;

            if (delta != 0) {
                // block until room in FIFO
                while (*stimulus == 0) { /* empty */ }

                // write extra  bits

                ((uint16_t*)stimulus)[1] = (uint16_t)delta;
            }
        }

        perfCntrIndex += 1;
        perfCntrMask >>= 1;
    }
}

__attribute__((no_instrument_function)) static inline void write_trace_record_deltaXOR(int core,perfRecordType_t recType,uint32_t perfCntrMask,volatile uint32_t *stimulus,void *this_fn,void *call_site)
{

    // block until room in FIFO
    while (*stimulus == 0) { /* empty */ }

    // need to write an record type ID tag so we know this is a func entry, eixit, or whatever

    ((uint8_t*)stimulus)[3] = (uint8_t)recType;

    uint32_t fnL;
    uint32_t fnH;
    uint32_t csL;
    uint32_t csH;

    if (sizeof(void*) > sizeof(uint32_t)) {
        fnL = ((uint32_t*)&this_fn)[0];
        fnH = ((uint32_t*)&this_fn)[1];

        csL = ((uint32_t*)&call_site)[0];
        csH = ((uint32_t*)&call_site)[1];
    }
    else {
        fnL = *(uint32_t*)&this_fn;
        fnH = 0;

        csL = *(uint32_t*)&call_site;
        csH = 0;
    }

    if (fnH != 0) {
        // block until room in FIFO
        while (*stimulus == 0) { /* empty */ }

        // write the first 32 bits, set bit 0 to indicate 64 bit address
        *stimulus = fnL | 1;

    	// block until room in FIFO
        while (*stimulus == 0) { /* empty */ }

        // write the second 32 bits
        *stimulus = fnH;
    }
    else {
        // block until room in FIFO
        while (*stimulus == 0) { /* empty */ }

        // write 32 bit pc
        *stimulus = fnL;
    }

    if ((recType == perfRecord_FuncEnter) || (recType == perfRecord_FuncExit)) {
        if (csH != 0) {
            // block until room in FIFO
            while (*stimulus == 0) { /* empty */ }

            // write the first 32 bits, set bit 0 to indicate 64 bit address
            *stimulus = csL | 1;
    
            // block until room in FIFO
            while (*stimulus == 0) { /* empty */ }

            // write the second 32 bits
            *stimulus = csH;
        }
        else {
            // block until room in FIFO
            while (*stimulus == 0) { /* empty */ }

            // write 32 bit pc
            *stimulus = csL;
        }
    }

    if (prevCntrVal[core] == NULL) {
        return;
    }

    int perfCntrIndex = 0;

    while (perfCntrMask != 0) {
        if (perfCntrMask & 1) {
            uint64_t newCntrVal;
            uint64_t oldCntrVal;
            uint64_t delta;

            if (perfCntrIndex == 1) {
            	newCntrVal = perfReadTsCounter(core);
            }
            else {
            	newCntrVal = hpm_read_counter(perfCntrIndex);
            }

            oldCntrVal = prevCntrVal[core][perfCntrIndex];

            if (newCntrVal > oldCntrVal) {
                delta = newCntrVal ^ oldCntrVal;
            }
            else {
                delta = (newCntrVal + cntrWrapVal[perfCntrIndex]) ^ oldCntrVal;
            }

            prevCntrVal[core][perfCntrIndex] = newCntrVal;

            // block until room in FIFO
            while (*stimulus == 0) { /* empty */ }

            // write the first 32 bits
            *stimulus = (uint32_t)delta;

            // write an extra 16 bits if needed; if it has non-zero data

            delta >>= 32;

            if (delta != 0) {
                // block until room in FIFO
                while (*stimulus == 0) { /* empty */ }

                // write extra  bits

                ((uint16_t*)stimulus)[1] = (uint16_t)delta;
            }
        }

        perfCntrIndex += 1;
        perfCntrMask >>= 1;
    }
}

__attribute__((no_instrument_function)) void __cyg_profile_func_enter(void *this_fn,void *call_site)
{
   if (perfTraceEnabled == 0) {
       return;
    }

    // use hartID as CPU index

    int core;

    // only support tracing on a single core right now, so we use a global value for coreID

    core = coreID;

    uint32_t perfCntrMask = perfCounterCPUPairing[core];

    volatile uint32_t *stimulus = perfStimulusCPUPairing[core];

    write_trace_record_fn(core,perfRecord_FuncEnter,perfCntrMask,stimulus,this_fn,call_site);
}

__attribute__((no_instrument_function)) void __cyg_profile_func_exit(void *this_fn,void *call_site)
{
    if (perfTraceEnabled == 0) {
        return;
    }

    // use hartID as CPU index

    int core;

    // only support tracing on a single core right now, so we use a global value for coreID

    core = coreID;

    uint32_t perfCntrMask = perfCounterCPUPairing[core];

    volatile uint32_t *stimulus = perfStimulusCPUPairing[core];

    write_trace_record_fn(core,perfRecord_FuncExit,perfCntrMask,stimulus,this_fn,call_site);
}

__attribute__((no_instrument_function)) void perfWriteCntrs()
{
    if (perfTraceEnabled == 0) {
        return;
    }

    uint64_t pc;

#if __riscv_xlen == 32

    uint32_t pcL;

    // get lower 32 bits of return address into pc, upper 32 bits (if they exist) into pcH

    asm volatile ("sw ra, %0": "=m"(pcL));

    pc = (uint64_t)pcL;

#else

    asm volatile ("sd ra, %0": "=m"(pc));

#endif

    int core;

    // only support tracing on a single core right now, so we use a global value for coreID

    core = coreID;

    uint32_t perfCntrMask = perfCounterCPUPairing[core];

    volatile uint32_t *stimulus = perfStimulusCPUPairing[core];

    write_trace_record_fn(core,perfRecord_Manual,perfCntrMask,stimulus,(void*)pc,NULL);
}

__attribute__((no_instrument_function)) static int perfTraceInit(int core,perfEvent *pCntrLst,int nCntrs,int itcChannel,uint32_t sink_size)
{
    if ((core < 0) || (core >= numCores)) {
        return 1;
    }

    int rc;

    rc = perfDeviceDriverInit(core,sink_size,pCntrLst,nCntrs);
    if (rc != 0) {
        return 1;
    }

    perfSettings_t settings;

    settings.teControl.teInstruction = TE_INSTRUCTION_NONE;
    settings.teControl.teInstrumentation = TE_INSTRUMENTATION_ITC;
    settings.teControl.teStallOrOverflow = 0;
    settings.teControl.teStallEnable = 0;
    settings.teControl.teStopOnWrap = 1;

    settings.teControl.teInhibitSrc = 0;
//    settings.teControl.teInhibitSrc = 1;

    settings.teControl.teSyncMaxBTM = TE_SYNCMAXBTM_OFF;
//    settings.teControl.teSyncMaxBTM = 0;
    settings.teControl.teSyncMaxInst = TE_SYNCMAXINST_OFF;
//    settings.teControl.teSyncMaxInst = 0;

    settings.tfControl.tfStopOnWrap = 1;

    settings.itcTraceEnable = 1 << itcChannel;

    settings.tsControl.tsCount = 1;
    settings.tsControl.tsDebug = 0;
    settings.tsControl.tsPrescale = TS_PRESCL_1;

    settings.tsControl.tsEnable = 1;
//    settings.tsControl.tsEnable = 0;

    settings.tsControl.tsBranch = BRNCH_ALL;
    settings.tsControl.tsInstrumentation = 1;
    settings.tsControl.tsOwnership = 1;

    if (SBABufferSize != 0) {
        settings.sink.sink = TE_SINK_SBA;
        settings.sink.sinkBase = (unsigned int)SBABufferPhysAddr;
        settings.sink.sinkBaseH = (unsigned int)(SBABufferPhysAddr >> 32);
        settings.sink.sinkSize = SBABufferSize -1;
    }
    else {
        settings.sink.sink = TE_SINK_SRAM;
        settings.sink.sinkBase = 0;
        settings.sink.sinkBaseH = 0;
        settings.sink.sinkSize = 0;
    }

    settings.itcChannel = itcChannel;

    rc = perfTraceEngineInit(core,&settings);
    if (rc!= 0) {
        return rc;
    }

    return 0;
}

__attribute__((no_instrument_function)) int perfFuncEntryExitInit(perfEvent *perfCntrList,int numCntrs,int itcChannel,perfCountType_t cntType,uint32_t sink_size)
{
    perfTraceEnabled = 0;

    if (coreID == -1) {
	printf("Error: perfFuncEntryExitInit(): Invalid coreID\n");

	return 1;
    }

    int rc;

    rc = perfTraceInit(coreID,perfCntrList,numCntrs,itcChannel,sink_size);
    if (rc != 0) {
        return 1;
    }

    perfMarkerVal = PERF_MARKER_VAL;
    perfCountType = cntType;

    switch (cntType) {
    case perfCount_Raw:
        write_trace_record_fn = write_trace_record_raw;
        break;
    case perfCount_Delta:
        write_trace_record_fn = write_trace_record_delta;
        break;
    case perfCount_DeltaXOR:
        write_trace_record_fn = write_trace_record_deltaXOR;
        break;
    default:
        printf("Error: perfFuncEntryExitInit(): invalid perfCountType\n");
        return 1;
    }

    return 0;
}

__attribute__((no_instrument_function)) int perfManualInit(perfEvent *perfCntrList,int numCntrs,int itcChannel,perfCountType_t cntType,uint32_t sink_size)
{
    perfTraceEnabled = 0;

    if (coreID == -1) {
	printf("Error: perfManualInit(): Invalid coreID\n");

	return 1;
    }

    int rc;

    rc = perfTraceInit(coreID,perfCntrList,numCntrs,itcChannel,sink_size);
    if (rc != 0) {
        return rc;
    }

    perfMarkerVal = PERF_MARKER_VAL;
    perfCountType = cntType;

    switch (cntType) {
    case perfCount_Raw:
        write_trace_record_fn = write_trace_record_raw;
        break;
    case perfCount_Delta:
        write_trace_record_fn = write_trace_record_delta;
        break;
    case perfCount_DeltaXOR:
        write_trace_record_fn = write_trace_record_deltaXOR;
        break;
    default:
        printf("Error: perfFuncEntryExitInit(): invalid perfCountType\n");
        return 1;
    }

    return 0;
}

__attribute__((no_instrument_function)) static int perfWriteSRAMBuffer(int fd,struct TraceRegMemMap volatile *te)
{
    unsigned int sinkWP;
    unsigned int wrapped;
    int rc;
    unsigned int numWritten;

    sinkWP = te->te_sink_wp_register;
    wrapped = sinkWP & 1;
    sinkWP &= ~1;

    numWritten = 0;

    if (wrapped && (sinkWP == te->te_sink_rp_register)) {
        // write entire buffer, starting at WP and ending at WP-1

        te->te_sink_rp_register = sinkWP;

        do {
            unsigned int sinkData;

            sinkData = te->te_sink_data_register;

            if (numWritten < 16) {
                printf("%2d: 0x%08x\n",numWritten,sinkData);
            }

            rc = (unsigned int)write(fd,&sinkData,sizeof sinkData);
            if (rc != (int)sizeof sinkData) {
                printf("Error: perfWriteSRAMBuffer(): write failed\n");

                return 1;
            }

            numWritten += sizeof sinkData;
        } while (te->te_sink_rp_register != sinkWP);
    }
    else {
        // write from sinkRP to sinkWP

        while (sinkWP != te->te_sink_rp_register) {
            unsigned int sinkData;

            sinkData = te->te_sink_data_register;

            rc = (unsigned int)write(fd,&sinkData,sizeof sinkData);
            if (rc != (int)sizeof sinkData) {
                printf("Error: perfWriteSRAMBuffer(): write failed\n");

                return 1;
            }

            numWritten += sizeof sinkData;
        }
    }

    printf("Wrote %d bytes\n",numWritten);

    return 0;
}

__attribute__((no_instrument_function)) static int perfWriteSBABuffer(int fd,struct TraceRegMemMap volatile *te)
{
    if (SBABufferSize == 0) {
	return 0;
    }

    // open device driver and use it to copy the SBA trace buffer into user space

    int devFD;

    devFD = open("/dev/traceperf",O_RDWR | O_SYNC);
    if (devFD < 0) {
        printf("Error: perfWriteSBABuffer(): could not open /dev/traceperf\n");

        return 1;
    }

    int rc;

    // Read the SBA buffer into user space

    // first, better allocate a buffer!

    struct {
        unsigned long addr;
	uint32_t size;
    } bp;

    bp.addr = (unsigned long)malloc(SBABufferSize);
    if (bp.addr == 0) {
        printf("Error: perfWriteSBABuffer(): malloc() failed\n");

        close(devFD);

        return 1;
    }

    bp.size = SBABufferSize;

    rc = ioctl(devFD,PERF_IOCTL_READ_SBA_BUFFER,&bp);
    if (rc < 0) {
        printf("Error: perfWriteSBABuffer(): ioctl() to read buffer failed\n");

        free((void*)bp.addr);
        close(devFD);

        return 1;
    }

    if (bp.size != SBABufferSize) {
	printf("Warning: Read requested %u bytes, but read %u bytes\n",SBABufferSize,bp.size);
    }

    unsigned int wp;
    size_t bsize;

    wp = te->te_sink_wp_register;

    if (wp & 1) {
      bsize = (size_t)SBABufferSize;
    }
    else {
      bsize = (size_t)wp;
    }
    
    rc = (unsigned int)write(fd,(void*)bp.addr,bsize);

    close(fd);
    free((void*)bp.addr);

    if (rc != (int)bsize) {
        printf("Error: perfWriteSRAMBuffer(): write failed. Wrote %d bytes, tried %d bytes\n",rc,(int)bsize);

        return 1;
    }

    printf("Wrote %d bytes\n",(int)bsize);

    return 0;
}

__attribute__((no_instrument_function)) static int perfResetSinkBufferPtrs(struct TraceRegMemMap volatile *te)
{
    unsigned int tec;
    tec = te->te_control_register;

    te->te_control_register = tec & ~(1 << 2);

    while ((te->te_control_register & (1 << 3)) == 0) {
        // do nothing but wait for bit 3 to get set indicating trace is flushed
    }

    te->te_sink_wp_register = teSinkWP;
    te->te_sink_rp_register = 0;

    te->te_control_register = tec;

    return 0;
}

__attribute__((no_instrument_function)) int perfWriteTrace(char *file)
{
    // flush all TEs and funnels

    for (int core = 0; core < numCores; core++) {
	if (!getTeEmpty(core)) {
            setTeEnable(core,0);

            while (!getTeEmpty(core)) { /* empty */ }

            setTeEnable(core,1);
        }
    }

    if (numFunnels > 0) {
	if (!getTfEmpty()) {
            setTfEnable(0);

            while (!getTfEmpty()) { /* empty */ }

            setTfEnable(1);
        }
    }

    if (file == NULL) {
        file = "trace.rtd";
    }

    int f;

    f = open(file,O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (f < 0) {
        printf("Error: perfWriteTrace(): Could not open trace file %s\n",file);

        return 1;
    }

    // If funnel, write funnel buffer. Else, write TE buffer(s)

    int rc;

    if (numFunnels > 0) {
        int sink;
        sink = getTfSink();

        if (sink == TE_SINK_SRAM) {
            rc = perfWriteSRAMBuffer(f,(struct TraceRegMemMap volatile *)fmm);
            if (rc != 0) {
                close(f);
                return rc;
            }
        }
        else if (sink == TE_SINK_SBA) {
            rc = perfWriteSBABuffer(f,(struct TraceRegMemMap volatile *)fmm);
            if (rc != 0) {
                close(f);
                return rc;
            }
        }
        else {
            printf("Error: perfWriteTrace(): Invalid sink type: %d\n",sink);

            close(f);
            return 1;
        }

        rc = perfResetSinkBufferPtrs((struct TraceRegMemMap volatile *)fmm);
        if (rc != 0) {
            close(f);
            return rc;
        }
    }
    else {
        for (int core = 0; core < numCores; core++) {
            int sink;
            sink = getTeSink(core);

            if (sink == TE_SINK_SRAM) {
                rc = perfWriteSRAMBuffer(f,tmm[core]);
                if (rc != 0) {
                    close(f);
                    return rc;
                }
            }
            else if (sink == TE_SINK_SBA) {
                rc = perfWriteSBABuffer(f,tmm[core]);
                if (rc != 0) {
                    close(f);
                    return rc;
                }
            }
            else if (sink != 8) {
                printf("Error: perfWriteTrace(): Invalid sink type: %d\n",sink);

                close(f);
                return 1;
            }

            rc = perfResetSinkBufferPtrs((struct TraceRegMemMap volatile *)tmm[core]);
            if (rc != 0) {
                close(f);
                return rc;
            }
        }
    }

    close(f);

    return 0;
}
