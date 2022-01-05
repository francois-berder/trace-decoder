
#include "sifive_trace.h"
#include "sifive_perf.h"
#include "itc_utils.h"

#include <metal/cpu.h>
#include <metal/hpm.h>

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
        uint8_t teSink;
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
    uint32_t teSinkBase;
    uint32_t teSinkBaseH;
    uint32_t teSinkLimit;
} perf_settings_t;

static int numCores;
static int numFunnels;

//extern volatile int intr_count; // remove

extern struct TraceRegMemMap volatile * const tmm[];
//extern struct CaTraceRegMemMap volatile * const cmm[];
extern struct TfTraceRegMemMap volatile * const fmm;

// array of pointers to stimulus registers. Maps a core id to a stimulus register. Supports multi-core

static uint32_t *perfStimulusCPUPairing[32];

// Map core id to perf counters being recorded for that core

static uint32_t perfCounterCPUPairing[32];

static int perfMarkerCntReload;
static int perfMarkerCnt;
static uint32_t perfMarkerVal;

static void perfEmitMarker(int core,uint32_t perfCntrMask)
{
	// do we need to emit mapping of events to counters?

    volatile uint32_t *stimulus = perfStimulusCPUPairing[core];

    // block until room in FIFO
    while (*stimulus == 0) { /* empty */ }

    // write the marker value
    *stimulus = perfMarkerVal;

    // block until room in FIFO
    while (*stimulus == 0) { /* empty */ }

    // write the first counter mask
    *stimulus = perfCntrMask;

    // skip the lower two counters because they are fixed function

    uint32_t tmpPerfCntrMask = perfCntrMask >> 2;

    if (tmpPerfCntrMask != 0) {
        int counter = 2;
        struct metal_cpu *cpu;

        cpu = metal_cpu_get(core);

        while (tmpPerfCntrMask != 0) {
        	if (tmpPerfCntrMask & 1) {
        		uint32_t mask;
        		mask = metal_hpm_get_event(cpu, counter);

        	    // block until room in FIFO
        	    while (*stimulus == 0) { /* empty */ }

        	    // write the first counter mask
        	    *stimulus = mask;
        	}

        	tmpPerfCntrMask >>= 1;
        	counter += 1;
        }
    }
}

static int perfSetChannel(uint32_t perfCntrMask,int channel)
{
	// pair a performance counter mask to a channel

	// check to see if the channel is valid
	if ((channel < 0) || (channel > 31)) {
		return 1;
	}

	int hartID;

    hartID = metal_cpu_get_current_hartid();

	// enable the itc channel requested

    setITCTraceEnable(hartID, (getITCTraceEnable(hartID)) | 1 << (channel));                                     \

	// set the value-pair since we didnt fail at enabling

	perfStimulusCPUPairing[hartID] = (uint32_t*)&tmm[hartID]->itc_stimulus_register[channel];

	perfCounterCPUPairing[hartID] = perfCntrMask;

	return 0;
}

// maybe change the return values to just 0, 1? maybe return the number of writes, or words written, or bytes written??

int perfWriteCntrs()
{
    uint32_t pc;
    uint32_t pcH;

    // get lower 32 bits of return address into pc, upper 32 bits (if they exist) into pcH

    asm volatile ("sw ra, %0\n\t"
    	 "srli ra,a5,16\n\t"
       	 "srli a5,a5,16\n\t"
    	 "sw a5,%1": "=m"(pc), "=m"(pcH));

    int hartID;

    // use hartID as CPU index
    struct metal_cpu *cpu;

    hartID = metal_cpu_get_current_hartid();

    cpu = metal_cpu_get(hartID);
    if (cpu == NULL) {
        return 1;
    }

    uint32_t perfCntrMask = perfCounterCPUPairing[hartID];

    if (perfMarkerCnt > 1) {
    	perfMarkerCnt -= 1;
    }
    else if (perfMarkerCnt == 1) {
    	perfEmitMarker(hartID,perfCntrMask);
    	perfMarkerCnt = perfMarkerCntReload;
    }

    volatile uint32_t *stimulus = perfStimulusCPUPairing[hartID];

    // block until room in FIFO
    while (*stimulus == 0) { /* empty */ }

    // write the first 32 bits
    *stimulus = pc;

    if ((sizeof(void*) * 8) > 32) {
    	// block until room in FIFO
        while (*stimulus == 0) { /* empty */ }

        // write the second 32 bits - add support for > 32 bit PCs later
        *stimulus = pcH;
    }

    int perfCntrIndex = 0;

    while (perfCntrMask != 0) {
        if (perfCntrMask & 1) {
            unsigned long long perfCntrVal;

            perfCntrVal = metal_hpm_read_counter(cpu, perfCntrIndex);

            // block until room in FIFO
            while (*stimulus == 0) { /* empty */ }

            // write the first 32 bits
            *stimulus = (uint32_t)perfCntrVal;

            // only write one extra byte if needed if it has non-zero data

			uint32_t perfCntrValH = (uint32_t)(perfCntrVal >> 32);

            if (perfCntrValH != 0) {
                // block until room in FIFO
                while (*stimulus == 0) { /* empty */ }

                // write extra 8 bits
            	uint8_t *stim8 = (uint8_t *)stimulus;

                stim8[3] = (uint8_t)perfCntrValH;
            }
        }

        perfCntrIndex += 1;
        perfCntrMask >>= 1;
    }

	return 0;
}

int perfResetCntr(int hpm_counter, struct metal_cpu *cpu)
{
	// check to see if the hpm counter value is between 0 and 31 (the range of valid counters)
	if ((hpm_counter < 0 ) || (hpm_counter > 31)) {
		return 0;
	}

	// check to see that CPU isnt NULL
	if (cpu == NULL) {
		return 0;
	}

	// clear the value
	metal_hpm_clear_counter(cpu, hpm_counter);

	return 1;
}

int perfInit(int num_cores,int num_funnels)
{
    numCores = num_cores;
    numFunnels = num_funnels;

    return 0;
}

static int perfCounterInit(int core,perf_settings_t *settings)
{
    int haveFunnel = 0;

    // error checking

    if (((core < 0) || (core >= numCores)) && (core != PERF_CORES_ALL)) {
        return 1;
    }

    if (settings == NULL) {
        return 1;
    }

    // reset trace encoder

    if (core == PERF_CORES_ALL) {
        for (int i = 0; i < numCores; i++) {
            setTeActive(i,0);
            setTeActive(i,1);
        }
    }
    else {
        setTeActive(core,0);
        setTeActive(core,1);
    }

    int tfSink = 0;
    int teSink = settings->teControl.teSink;

    // currently only support one or no funnels

    if (numFunnels > 0) {
        setTfActive(0);
        setTfActive(1);

        if (teSink == 8) {
            tfSink = 4;
        }
        else {
            tfSink = teSink;
            teSink = 8;
        }
    }

    // set teEnable, clear teTracing. The trace encoder is enabled, but not activily tracing. Set everything else as specified in settings

    if (core == PERF_CORES_ALL) {
        for (int i = 0; i < numCores; i++) {
            setTeControl(i,
                         (settings->teControl.teSink << 28)            |
                         (settings->teControl.teSyncMaxInst << 20)    |
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
                     (settings->teControl.teSink << 28)            |
                     (settings->teControl.teSyncMaxInst << 20)    |
                     (settings->teControl.teSyncMaxBTM << 16)      |
                     (settings->teControl.teInhibitSrc << 15)      |
                     (settings->teControl.teStopOnWrap << 14)      |
                     (settings->teControl.teStallEnable << 13)     |
                     (settings->teControl.teStallOrOverflow << 12) |
                     (settings->teControl.teInstrumentation << 7)  |
                     (settings->teControl.teInstruction << 4)      |
                     (0x03 << 0));
    }

    if (numFunnels > 0) {
        setTfControl((tfSink << 28) | (settings->teControl.teStopOnWrap << 14) | (0x03 << 0));
    }

    // clear trace buffer read/write pointers

    if (numFunnels > 0) {
        // see if we have system memory sink as an option. If so, set base and limit regs

        if (getTfHasSBASink()) {
            setTfSinkBase(settings->teSinkBase);
            setTfSinkBaseHigh(settings->teSinkBaseH);
            setTfSinkLimit(settings->teSinkLimit);
        }

        if (getTfHasSRAMSink()) {
            setTfSinkWp(0);
            setTfSinkRp(0);
        }
    }
    else if (core == PERF_CORES_ALL) {
        for (int i = 0; i < numCores; i++) {
            if (getTeImplHasSBASink(i)) {
                setTeSinkBase(i,settings->teSinkBase);
                setTeSinkBaseHigh(i,settings->teSinkBaseH);
                setTeSinkLimit(i,settings->teSinkLimit);
            }

            if (getTeImplHasSRAMSink(i)) {
                setTeSinkWp(i,0);
                setTeSinkRp(i,0);
            }
        }
    }
    else {
        if (getTeImplHasSBASink(core)) {
            setTeSinkBase(core,settings->teSinkBase);
            setTeSinkBaseHigh(core,settings->teSinkBaseH);
            setTeSinkLimit(core,settings->teSinkLimit);
        }

        if (getTeImplHasSRAMSink(core)) {
            setTeSinkWp(core,0);
            setTeSinkRp(core,0);
        }
    }

    // setup the timestamp unit. Te's have timestamps; the funnel does not

    if (core == PERF_CORES_ALL) {
        for (int i = 0; i < numCores; i++) {
            //TSReset(i);
            setTsActive(core, 0);                                                           \
            setTsActive(core, 1);                                                           \

            tsConfig(i,
                     settings->tsControl.tsDebug,
                     settings->tsControl.tsPrescale,
                     settings->tsControl.tsBranch,
                     settings->tsControl.tsInstrumentation,
                     settings->tsControl.tsOwnership);

            setTsCount(i,settings->tsControl.tsCount);

            if (settings->tsControl.tsEnable) {
                setTsEnable(i,1);
            }
        }
    }
    else {
        //TSReset(core);
        setTsActive(core, 0);                                                           \
        setTsActive(core, 1);                                                           \

        tsConfig(core,
                 settings->tsControl.tsDebug,
                 settings->tsControl.tsPrescale,
                 settings->tsControl.tsBranch,
                 settings->tsControl.tsInstrumentation,
                 settings->tsControl.tsOwnership);

        setTsCount(core,settings->tsControl.tsCount);

        if (settings->tsControl.tsEnable) {
            setTsEnable(core,1);
        }
    }

    // setup the itc channel enables

    if (core == PERF_CORES_ALL) {
        for (int i = 0; i < numCores; i++) {
            setITCTraceEnable(i,settings->itcTraceEnable);
        }
    }
    else {
            setITCTraceEnable(core,settings->itcTraceEnable);
    }

    if (core == PERF_CORES_ALL) {
        for (int i = 0; i < numCores; i++) {
            setTeTracing(i,1);
        }
    }
    else {
    	setTeTracing(core,1);
    }

    return 0;
}

static unsigned long long next_mcount;
static unsigned long long interval;

static void perfTimerHandler(int id,void *data)
{
    int hartID;		// use hartID as CPU index
    struct metal_cpu *cpu;

//    intr_count += 1;

    cpu = (struct metal_cpu *)data;

    unsigned long long pc;

    pc = metal_cpu_get_exception_pc(cpu);

    hartID = metal_cpu_get_current_hartid();

    uint32_t perfCntrMask = perfCounterCPUPairing[hartID];

    if (perfMarkerCnt > 1) {
    	perfMarkerCnt -= 1;
    }
    else if (perfMarkerCnt == 1) {
    	perfEmitMarker(hartID,perfCntrMask);
    	perfMarkerCnt = perfMarkerCntReload;
    }

    volatile uint32_t *stimulus = perfStimulusCPUPairing[hartID];

    // block until room in FIFO
    while (*stimulus == 0) { /* empty */ }

    // write the first 32 bits
    *stimulus = (uint32_t)pc;

    if ((sizeof(void*) * 8) > 32) {
    	// block until room in FIFO
        while (*stimulus == 0) { /* empty */ }

        // write the second 32 bits - add support for > 32 bit PCs later
        *stimulus = (uint32_t)(pc >> 32);
    }

    int perfCntrIndex = 0;

    while (perfCntrMask != 0) {
        if (perfCntrMask & 1) {
            unsigned long long perfCntrVal;

            perfCntrVal = metal_hpm_read_counter(cpu, perfCntrIndex);

            // block until room in FIFO
            while (*stimulus == 0) { /* empty */ }

            // write the first 32 bits
            *stimulus = (uint32_t)perfCntrVal;

            // only write one extra byte if needed if it has non-zero data

			uint32_t perfCntrValH = (uint32_t)(perfCntrVal >> 32);

            if (perfCntrValH != 0) {
                // block until room in FIFO
                while (*stimulus == 0) { /* empty */ }

                // write extra 8 bits
            	uint8_t *stim8 = (uint8_t *)stimulus;

                stim8[3] = (uint8_t)perfCntrValH;
            }
        }

        perfCntrIndex += 1;
        perfCntrMask >>= 1;
    }

    // set time for next interrupt

    next_mcount += interval;

    metal_cpu_set_mtimecmp(cpu, next_mcount);
}

static int perfTimerInit(int core,int _interval,int itcChannel,uint32_t perfCntrMask,int markerCnt)
{
    if ((core < 0) || (core >= numCores)) {
        return 1;
    }

    if (markerCnt != 0) {
    	perfMarkerCnt = 1;
    	perfMarkerCntReload = markerCnt;
    }
    else {
    	perfMarkerCnt = 1;
    	perfMarkerCntReload = 0;
    }

    if (_interval < 100) {
        _interval = 100;
    }

    int hartID;

    // hartID = metal_cpu_get_current_hartid();

    hartID = getTeImplHartId(core);

    struct metal_cpu *cpu;

    cpu = metal_cpu_get(hartID);
    if (cpu == NULL) {
        return 1;
    }

    unsigned long long timeval;
    unsigned long long timebase;

    timeval = metal_cpu_get_timer(cpu);
    timebase = metal_cpu_get_timebase(cpu);

    if ((timeval == 0) || (timebase == 0)) {
        return 1;
    }

    interval = timebase * _interval / 1000000000;

    struct metal_interrupt *cpu_intr;

    cpu_intr = metal_cpu_interrupt_controller(cpu);
    if (cpu_intr == NULL) {
        return 1;
    }

	if (metal_hpm_init(cpu)) {
		return 1;
	}

	metal_interrupt_init(cpu_intr);

    struct metal_interrupt *tmr_intr;

    tmr_intr = metal_cpu_timer_interrupt_controller(cpu);
    if (tmr_intr == NULL) {
        return 1;
    }

    metal_interrupt_init(tmr_intr);

    int tmr_id;

    tmr_id = metal_cpu_timer_get_interrupt_id(cpu);

    int rc;

    rc = metal_interrupt_register_handler(tmr_intr,tmr_id,perfTimerHandler,cpu);
    if (rc < 0) {
        return 1;
    }

    next_mcount = metal_cpu_get_mtime(cpu) + interval;

    metal_cpu_set_mtimecmp(cpu, next_mcount);
    if (metal_interrupt_enable(tmr_intr, tmr_id) == -1) {
        return 1;
    }

    if (metal_interrupt_enable(cpu_intr, 0) == -1) {
        return 1;
    }

	rc = perfSetChannel(perfCntrMask,itcChannel);
	if (rc != 0) {
		return 1;
	}

	return 0;
}

int perfManualInit(int core,uint32_t counterMask,int itcChannel,int stopOnWrap,int markerCnt)
{
    if ((core < 0) || (core >= numCores)) {
        return 1;
    }

    if (markerCnt != 0) {
    	perfMarkerCnt = 1;
    	perfMarkerCntReload = markerCnt;
    }
    else {
    	perfMarkerCnt = 1;
    	perfMarkerCntReload = 0;
    }

    perf_settings_t settings;

    settings.teControl.teInstruction = TE_INSTRUCTION_NONE;
    settings.teControl.teInstrumentation = TE_INSTRUMENTATION_ITC;
    settings.teControl.teStallOrOverflow = 0;
    settings.teControl.teStallEnable = 0;
    settings.teControl.teStopOnWrap = (stopOnWrap != 0);
    settings.teControl.teInhibitSrc = 0;
    settings.teControl.teSyncMaxBTM = TE_SYNCMAXBTM_OFF;
//    settings.teControl.teSyncMaxBTM = 0;
    settings.teControl.teSyncMaxInst = TE_SYNCMAXINST_OFF;
//    settings.teControl.teSyncMaxInst = 0;
    settings.teControl.teSink = TE_SINK_SRAM;

    settings.itcTraceEnable = 1 << itcChannel;

    settings.tsControl.tsCount = 1;
    settings.tsControl.tsDebug = 0;
    settings.tsControl.tsPrescale = TS_PRESCL_1;
    settings.tsControl.tsEnable = 1;
    settings.tsControl.tsBranch = BRNCH_ALL;
    settings.tsControl.tsInstrumentation = 1;
    settings.tsControl.tsOwnership = 1;

    settings.teSinkBase = 0;
    settings.teSinkBaseH = 0;
    settings.teSinkLimit = 0;

    int rc;

    rc = perfCounterInit(core,&settings);
    if (rc!= 0) {
        return rc;
    }

    // at this point tracing is set up, but not enabled.

    // tracing should now be configured, but still off

    // use 'perf' as the perf marker value - 32 bits

    perfMarkerVal = (uint32_t)(('p' << 24) | ('e' << 16) | ('r' << 8) | ('f' << 0));

    return 0;
}

int perfTimerISRInit(int core,int interval,uint32_t counterMask,int itcChannel,int stopOnWrap,int markerCnt)
{
    perf_settings_t settings;

    settings.teControl.teInstruction = TE_INSTRUCTION_NONE;
    settings.teControl.teInstrumentation = TE_INSTRUMENTATION_ITC;
    settings.teControl.teStallOrOverflow = 0;
    settings.teControl.teStallEnable = 0;
    settings.teControl.teStopOnWrap = (stopOnWrap != 0);
    settings.teControl.teInhibitSrc = 0;
    settings.teControl.teSyncMaxBTM = TE_SYNCMAXBTM_OFF;
//    settings.teControl.teSyncMaxBTM = 0;
    settings.teControl.teSyncMaxInst = TE_SYNCMAXINST_OFF;
//    settings.teControl.teSyncMaxInst = 0;
    settings.teControl.teSink = TE_SINK_SRAM;

    settings.itcTraceEnable = 1 << itcChannel;

    settings.tsControl.tsCount = 1;
    settings.tsControl.tsDebug = 0;
    settings.tsControl.tsPrescale = TS_PRESCL_1;
    settings.tsControl.tsEnable = 1;
    settings.tsControl.tsBranch = BRNCH_ALL;
    settings.tsControl.tsInstrumentation = 1;
    settings.tsControl.tsOwnership = 1;

    settings.teSinkBase = 0;
    settings.teSinkBaseH = 0;
    settings.teSinkLimit = 0;

    int rc;

    rc = perfCounterInit(core,&settings);
    if (rc!= 0) {
        return rc;
    }

    // at this point tracing is set up, but not enabled.

    // tracing should now be configured, but still off

    // use 'perf' as the perf marker value - 32 bits

    perfMarkerVal = (uint32_t)(('p' << 24) | ('e' << 16) | ('r' << 8) | ('f' << 0));

    rc = perfTimerInit(core,interval,itcChannel,counterMask,markerCnt);
    if (rc != 0) {
        return rc;
    }

    return 0;
}
