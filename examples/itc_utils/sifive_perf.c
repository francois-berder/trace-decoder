
#include "sifive_trace.h"
#include "sifive_perf.h"

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
    uint32_t perfCntrMask;
    int      itcChannel;
} perf_settings_t;

typedef enum {
	tracetype_none,
	tracetype_ISR,
	tracetype_func,
	tracetype_manual,
} perf_tracetype_t;

static int numCores;
static int numFunnels;

extern struct TraceRegMemMap volatile * const tmm[];
extern struct TfTraceRegMemMap volatile * const fmm;

// Array of pointers to stimulus registers. Maps a core id to a stimulus register. Supports multi-core

static uint32_t *perfStimulusCPUPairing[PERF_MAX_CORES];

// Map core id to perf counters being recorded for that core

static uint32_t perfCounterCPUPairing[PERF_MAX_CORES];
static struct metal_cpu *cachedCPU[PERF_MAX_CORES];
static int perfMarkerCntReload[PERF_MAX_CORES];
static int perfMarkerCnt[PERF_MAX_CORES];
static uint32_t perfMarkerVal;

static perf_tracetype_t traceType;
static int timerTracingEnabled;
static int funcTracingEnabled;
static int manualTracingEnabled;

__attribute__((no_instrument_function)) int perfTraceOn()
{
	switch (traceType) {
	case tracetype_ISR:
		timerTracingEnabled = 1;
		funcTracingEnabled = 0;
		manualTracingEnabled = 0;
		break;
	case tracetype_func:
		timerTracingEnabled = 0;
		funcTracingEnabled = 1;
		manualTracingEnabled = 0;
		break;
	case tracetype_manual:
		timerTracingEnabled = 0;
		funcTracingEnabled = 0;
		manualTracingEnabled = 1;
		break;
	case tracetype_none:
	default:
		return 1;
	}

	return 0;
}

__attribute__((no_instrument_function)) int perfTraceOff()
{
	timerTracingEnabled = 0;
	funcTracingEnabled = 0;
	manualTracingEnabled = 0;

	return 0;
}

__attribute__((no_instrument_function)) static void perfEmitMarker(int core,uint32_t perfCntrMask)
{
    struct metal_cpu *cpu;

    cpu = cachedCPU[core];
    if (cpu == NULL) {
    	return;
    }

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

    uint32_t tmpPerfCntrMask = perfCntrMask >> 3;

    if (tmpPerfCntrMask != 0) {
        int counter = 3;
        while (tmpPerfCntrMask != 0) {
        	if (tmpPerfCntrMask & 1) {
        		uint64_t mask;
        		mask = (uint64_t)metal_hpm_get_event(cpu, counter);

        	    // block until room in FIFO
        	    while (*stimulus == 0) { /* empty */ }

        	    // write the first counter mask
        	    *stimulus = (uint32_t)mask;

        	    // block until room in FIFO
        	    while (*stimulus == 0) { /* empty */ }

        	    // write the first counter mask
        	    *stimulus = (uint32_t)(mask >> 32);
        	}

        	tmpPerfCntrMask >>= 1;
        	counter += 1;
        }
    }
}

__attribute__((no_instrument_function)) static int perfSetChannel(int core,uint32_t perfCntrMask,int channel)
{
	// pair a performance counter mask to a channel

	// check to see if the channel is valid
	if ((channel < 0) || (channel > 31)) {
		return 1;
	}

	// enable the itc channel requested

    setITCTraceEnable(core, (getITCTraceEnable(core)) | 1 << (channel));                                     \

	// set the value-pair since we didn't fail at enabling

	perfStimulusCPUPairing[core] = (uint32_t*)&tmm[core]->itc_stimulus_register[channel];

	perfCounterCPUPairing[core] = perfCntrMask;

	return 0;
}

// maybe change the return values to just 0, 1? maybe return the number of writes, or words written, or bytes written??

__attribute__((no_instrument_function)) int perfWriteCntrs()
{
	if (manualTracingEnabled == 0) {
		return 0;
	}

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

    cpu = cachedCPU[hartID];
    if (cpu == NULL) {
    	return 0;
    }

    uint32_t perfCntrMask = perfCounterCPUPairing[hartID];

    if (perfMarkerCnt[hartID] > 1) {
    	perfMarkerCnt[hartID] -= 1;
    }
    else if (perfMarkerCnt[hartID] == 1) {
    	perfEmitMarker(hartID,perfCntrMask);
    	perfMarkerCnt[hartID] = perfMarkerCntReload[hartID];
    }

    volatile uint32_t *stimulus = perfStimulusCPUPairing[hartID];

    if (pcH != 0) {
        // block until room in FIFO
        while (*stimulus == 0) { /* empty */ }

        // write the first 32 bits, set bit 0 to indicate 64 bit address
        *stimulus = pc | 1;

    	// block until room in FIFO
        while (*stimulus == 0) { /* empty */ }

        // write the second 32 bits
        *stimulus = pcH;
    }
    else {
        // block until room in FIFO
        while (*stimulus == 0) { /* empty */ }

        // write 32 bit pc
        *stimulus = pc;
    }

    int perfCntrIndex = 0;

    while (perfCntrMask != 0) {
        if (perfCntrMask & 1) {
            unsigned long long perfCntrVal;

            if (perfCntrIndex == 1) {
            	perfCntrVal = metal_cpu_get_mtime(cpu);
            }
            else {
            	perfCntrVal = metal_hpm_read_counter(cpu, perfCntrIndex);
            }

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

__attribute__((no_instrument_function)) int perfResetCntrs(uint32_t cntrMask)
{
	int core;

    core = metal_cpu_get_current_hartid();

	struct metal_cpu *cpu;

    cpu = cachedCPU[core];
    if (cpu == NULL) {
        return 1;
    }

	int i = 0;

	while (cntrMask != 0) {
		if (cntrMask & 1) {
			// clear the value
			metal_hpm_clear_counter(cpu,i);
		}

		i += 1;
	}

	return 1;
}

__attribute__((no_instrument_function)) int perfInit(int num_cores,int num_funnels)
{
	timerTracingEnabled = 0;
	manualTracingEnabled = 0;
	funcTracingEnabled = 0;
	traceType = tracetype_none;

    numCores = num_cores;
    numFunnels = num_funnels;

    for (int i = 0; i < sizeof perfStimulusCPUPairing / sizeof perfStimulusCPUPairing[0]; i++) {
    	perfStimulusCPUPairing[i] = NULL;
    }

    for (int i = 0; i < sizeof perfCounterCPUPairing / sizeof perfCounterCPUPairing[0]; i++) {
    	perfCounterCPUPairing[i] = 0;
    }

    for (int i = 0; i < sizeof cachedCPU / sizeof cachedCPU[0]; i++) {
    	cachedCPU[i] = NULL;
    }

    return 0;
}

__attribute__((no_instrument_function)) static int perfCounterInit(int core,perf_settings_t *settings)
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

	rc = perfSetChannel(core,settings->perfCntrMask,settings->itcChannel);
	if (rc != 0) {
		return 1;
	}

    // reset trace encoder

    setTeActive(core,0);
    setTeActive(core,1);

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

    setTeControl(core,
                 (settings->teControl.teSink << 28)            |
                 (settings->teControl.teSyncMaxInst << 20)     |
                 (settings->teControl.teSyncMaxBTM << 16)      |
                 (settings->teControl.teInhibitSrc << 15)      |
                 (settings->teControl.teStopOnWrap << 14)      |
                 (settings->teControl.teStallEnable << 13)     |
                 (settings->teControl.teStallOrOverflow << 12) |
                 (settings->teControl.teInstrumentation << 7)  |
                 (settings->teControl.teInstruction << 4)      |
                 (0x03 << 0));

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

    // setup the itc channel enables

    setITCTraceEnable(core,settings->itcTraceEnable);

	setTeTracing(core,1);

    return 0;
}

static unsigned long long next_mcount;
static unsigned long long interval;

__attribute__((no_instrument_function)) static void perfTimerHandler(int id,void *data)
{
    int hartID;		// use hartID as CPU index
    struct metal_cpu *cpu;

    cpu = (struct metal_cpu *)data;

	if (timerTracingEnabled) {
	    unsigned long long pc;

	    pc = metal_cpu_get_exception_pc(cpu);

	    hartID = metal_cpu_get_current_hartid();

	    uint32_t perfCntrMask = perfCounterCPUPairing[hartID];

	    if (perfMarkerCnt[hartID] > 1) {
	    	perfMarkerCnt[hartID] -= 1;
	    }
	    else if (perfMarkerCnt[hartID] == 1) {
	    	perfEmitMarker(hartID,perfCntrMask);
	    	perfMarkerCnt[hartID] = perfMarkerCntReload[hartID];
	    }

	    volatile uint32_t *stimulus = perfStimulusCPUPairing[hartID];

	    if ((pc >> 32) != 0) {
	        // block until room in FIFO
	        while (*stimulus == 0) { /* empty */ }

	        // write the first 32 bits, set bit 0 to indicate 64 bit address
	        *stimulus = (uint32_t)pc | 1;

	    	// block until room in FIFO
	        while (*stimulus == 0) { /* empty */ }

	        // write the second 32 bits
	        *stimulus = (uint32_t)(pc >> 32);
	    }
	    else {
	        // block until room in FIFO
	        while (*stimulus == 0) { /* empty */ }

	        // write 32 bit pc
	        *stimulus = (uint32_t)pc;
	    }

	    int perfCntrIndex = 0;

	    while (perfCntrMask != 0) {
	        if (perfCntrMask & 1) {
	            unsigned long long perfCntrVal;

	            if (perfCntrIndex == 1) {
	            	perfCntrVal = metal_cpu_get_mtime(cpu);
	            }
	            else {
	            	perfCntrVal = metal_hpm_read_counter(cpu, perfCntrIndex);
	            }

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
	}
    // set time for next interrupt

    next_mcount += interval;

    metal_cpu_set_mtimecmp(cpu, next_mcount);
}

__attribute__((no_instrument_function)) void __cyg_profile_func_enter(void *this_fn,void *call_site)
{
	if (funcTracingEnabled == 0) {
		return;
	}

    // use hartID as CPU index

    int core;
    struct metal_cpu *cpu;

    core = metal_cpu_get_current_hartid();

    cpu = cachedCPU[core];
    if (cpu == NULL) {
    	return;
    }

    uint32_t perfCntrMask = perfCounterCPUPairing[core];

    if (perfMarkerCnt[core] > 1) {
    	perfMarkerCnt[core] -= 1;
    }
    else if (perfMarkerCnt[core] == 1) {
    	perfEmitMarker(core,perfCntrMask);
    	perfMarkerCnt[core] = perfMarkerCntReload[core];
    }

    volatile uint32_t *stimulus = perfStimulusCPUPairing[core];

    // need to write some kind of marker so we know this is a func entry
    // we write a 'C' as a one byte write so we can identify it in the trace.


    ((uint8_t*)stimulus)[3] = 'C';

    uint64_t fn;
    uint64_t cs;

    if (sizeof(void*) > 8) {
    	fn = (uint64_t)this_fn;
    	cs = (uint64_t)call_site;
    }
    else {
    	fn = *(uint32_t*)&this_fn;
    	cs = *(uint32_t*)&call_site;
    }

	if ((fn >> 32) != 0) {
        // block until room in FIFO
        while (*stimulus == 0) { /* empty */ }

        // write the first 32 bits, set bit 0 to indicate 64 bit address
        *stimulus = (uint32_t)fn | 1;

    	// block until room in FIFO
        while (*stimulus == 0) { /* empty */ }

        // write the second 32 bits
        *stimulus = (uint32_t)(fn >> 32);
    }
    else {
        // block until room in FIFO
        while (*stimulus == 0) { /* empty */ }

        // write 32 bit pc
        *stimulus = (uint32_t)fn;
    }

	if ((cs >> 32) != 0) {
        // block until room in FIFO
        while (*stimulus == 0) { /* empty */ }

        // write the first 32 bits, set bit 0 to indicate 64 bit address
        *stimulus = (uint32_t)cs | 1;

    	// block until room in FIFO
        while (*stimulus == 0) { /* empty */ }

        // write the second 32 bits
        *stimulus = (uint32_t)(cs >> 32);
    }
    else {
        // block until room in FIFO
        while (*stimulus == 0) { /* empty */ }

        // write 32 bit pc
        *stimulus = (uint32_t)cs;
    }

    int perfCntrIndex = 0;

    while (perfCntrMask != 0) {
        if (perfCntrMask & 1) {
            unsigned long long perfCntrVal;

            if (perfCntrIndex == 1) {
            	perfCntrVal = metal_cpu_get_mtime(cpu);
            }
            else {
            	perfCntrVal = metal_hpm_read_counter(cpu, perfCntrIndex);
            }

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
}

__attribute__((no_instrument_function)) void __cyg_profile_func_exit(void *this_fn,void *call_site)
{
	if (funcTracingEnabled == 0) {
		return;
	}

    // use hartID as CPU index

    int core;
    struct metal_cpu *cpu;

    core = metal_cpu_get_current_hartid();

    cpu = cachedCPU[core];

    uint32_t perfCntrMask = perfCounterCPUPairing[core];

    if (perfMarkerCnt[core] > 1) {
    	perfMarkerCnt[core] -= 1;
    }
    else if (perfMarkerCnt[core] == 1) {
    	perfEmitMarker(core,perfCntrMask);
    	perfMarkerCnt[core] = perfMarkerCntReload[core];
    }

    volatile uint32_t *stimulus = perfStimulusCPUPairing[core];

    // need to write some kind of marker so we know this is a func entry

    uint64_t fn;
    uint64_t cs;

    if (sizeof(void*) > 8) {
    	fn = (uint64_t)this_fn;
    	cs = (uint64_t)call_site;
    }
    else {
    	fn = *(uint32_t*)&this_fn;
    	cs = *(uint32_t*)&call_site;
    }

	if ((fn >> 32) != 0) {
        // block until room in FIFO
        while (*stimulus == 0) { /* empty */ }

        // write the first 32 bits, set bit 0 to indicate 64 bit address
        *stimulus = (uint32_t)fn | 1;

    	// block until room in FIFO
        while (*stimulus == 0) { /* empty */ }

        // write the second 32 bits
        *stimulus = (uint32_t)(fn >> 32);
    }
    else {
        // block until room in FIFO
        while (*stimulus == 0) { /* empty */ }

        // write 32 bit pc
        *stimulus = (uint32_t)fn;
    }

	if ((cs >> 32) != 0) {
        // block until room in FIFO
        while (*stimulus == 0) { /* empty */ }

        // write the first 32 bits, set bit 0 to indicate 64 bit address
        *stimulus = (uint32_t)cs | 1;

    	// block until room in FIFO
        while (*stimulus == 0) { /* empty */ }

        // write the second 32 bits
        *stimulus = (uint32_t)(cs >> 32);
    }
    else {
        // block until room in FIFO
        while (*stimulus == 0) { /* empty */ }

        // write 32 bit pc
        *stimulus = (uint32_t)cs;
    }

    int perfCntrIndex = 0;

    while (perfCntrMask != 0) {
        if (perfCntrMask & 1) {
            unsigned long long perfCntrVal;

            if (perfCntrIndex == 1) {
            	perfCntrVal = metal_cpu_get_mtime(cpu);
            }
            else {
            	perfCntrVal = metal_hpm_read_counter(cpu, perfCntrIndex);
            }

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

	return;
}

__attribute__((no_instrument_function)) static int perfTimerInit(int core,int _interval,int itcChannel,uint32_t perfCntrMask)
{
    if ((core < 0) || (core >= numCores)) {
        return 1;
    }

    if (_interval < 100) {
        _interval = 100;
    }

    struct metal_cpu *cpu;

    cpu = cachedCPU[core];
    if (cpu == NULL) {
        return 1;
    }

    cachedCPU[core] = cpu;

    unsigned long long mtimeval;
    unsigned long long mtimebase;

    mtimeval = metal_cpu_get_mtime(cpu);
    mtimebase = metal_cpu_get_mtime_timebase(cpu);

    if ((mtimeval == 0) || (mtimebase == 0)) {
        return 1;
    }

    interval = mtimebase * _interval / 1000000;

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

	return 0;
}

__attribute__((no_instrument_function)) static int perfTraceInit(int core,uint32_t counterMask,int itcChannel,int stopOnWrap,int markerCnt)
{
    if ((core < 0) || (core >= numCores)) {
        return 1;
    }

    if (markerCnt != 0) {
    	perfMarkerCnt[core] = 1;
    	perfMarkerCntReload[core] = markerCnt;
    }
    else {
    	perfMarkerCnt[core] = 1;
    	perfMarkerCntReload[core] = 0;
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

    settings.perfCntrMask = counterMask;
    settings.itcChannel = itcChannel;

    int rc;

    rc = perfCounterInit(core,&settings);
    if (rc!= 0) {
        return rc;
    }

    return 0;
}

__attribute__((no_instrument_function)) static int perfCacheCPU()
{
    int core;

    core = metal_cpu_get_current_hartid();

    struct metal_cpu *cpu;

    cpu = metal_cpu_get(core);

    cachedCPU[core] = cpu;

    return core;
}

__attribute__((no_instrument_function)) int perfFuncEntryExitInit(uint32_t counterMask,int itcChannel,int stopOnWrap,int markerCnt)
{
    funcTracingEnabled = 0;
    timerTracingEnabled = 0;
    manualTracingEnabled = 0;

    traceType = tracetype_func;

    int core;

    core = perfCacheCPU();

	int rc;

	rc = perfTraceInit(core,counterMask,itcChannel,stopOnWrap,markerCnt);
	if (rc != 0) {
		return rc;
	}

    struct metal_cpu *cpu;

    cpu = cachedCPU[core];
    if (cpu == NULL) {
    	return 1;
    }

	if (metal_hpm_init(cpu)) {
		return 1;
	}

    perfMarkerVal = PERF_FUNCMARKER_VAL;

    return 0;
}

__attribute__((no_instrument_function)) int perfManualInit(uint32_t counterMask,int itcChannel,int stopOnWrap,int markerCnt)
{
    funcTracingEnabled = 0;
    timerTracingEnabled = 0;
    manualTracingEnabled = 0;

    traceType = tracetype_manual;

    int core;

    core = perfCacheCPU();

	int rc;

	rc = perfTraceInit(core,counterMask,itcChannel,stopOnWrap,markerCnt);
	if (rc != 0) {
		return rc;
	}

    struct metal_cpu *cpu;

    cpu = cachedCPU[core];
    if (cpu == NULL) {
    	return 1;
    }

	if (metal_hpm_init(cpu)) {
		return 1;
	}

    perfMarkerVal = PERF_MARKER_VAL;

    return 0;
}

__attribute__((no_instrument_function)) int perfTimerISRInit(int interval,uint32_t counterMask,int itcChannel,int stopOnWrap,int markerCnt)
{
    funcTracingEnabled = 0;
    timerTracingEnabled = 0;
    manualTracingEnabled = 0;

    traceType = tracetype_ISR;

    int core;

    core = perfCacheCPU();

	int rc;

	rc = perfTraceInit(core,counterMask,itcChannel,stopOnWrap,markerCnt);
	if (rc != 0) {
		return rc;
	}

	// at this point tracing is set up, but not enabled.

    // tracing should now be configured, but still off

    // use 'perf' as the perf marker value - 32 bits

    perfMarkerVal = PERF_MARKER_VAL;

    rc = perfTimerInit(core,interval,itcChannel,counterMask);
    if (rc != 0) {
        return rc;
    }

    return 0;
}
