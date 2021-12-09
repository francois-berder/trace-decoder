
#include "sifive_trace.h"
#include "sifive_perf.h"
#include "itc_utils.h"

#include <metal/cpu.h>
#include <metal/hpm.h>

static int numCores;
static int numFunnels;

int perfInit(int num_cores,int num_funnels)
{
    numCores = num_cores;
    hasFunnel = has_funnel;
}

static int perfTraceConfig(int core,perf_settings_t *settings)
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
        setTFActive(0);
        setTFActive(1);

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
                         (settings->teControl.teSyincMaxInst << 20)    |
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
                     (settings->teControl.teSyincMaxInst << 20)    |
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
            setTfSinkBaseH(settings->teSinkBaseH);
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
                setTeSinkBaseH(i,settings->teSinkBaseH);
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
            setTeSinkBaseH(core,settings->teSinkBaseH);
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
            tsReset(i);
            tsConfig(i,
                     settings->tsControl.tsDebug,
                     settings->tsControl.tsPrescale,
                     settings->tsControl.tsBranch,
                     settings->tsControl.tsInstrumentation,
                     settings->tsControl.tsOwnership);

            if (settings->tsControl.tsEnable) {
                setTsEnable(i,1);
            }
        }
    }
    else {
        tsReset(core);
        tsConfig(core,
                 settings->tsControl.tsDebug,
                 settings->tsControl.tsPrescale,
                 settings->tsControl.tsBranch,
                 settings->tsControl.tsInstrumentation,
                 settings->tsControl.tsOwnership);

        if (settings->tsControl.tsEnable) {
            setTsEnable(core,1);
        }
    }

    // setup the itc channel enables

    if (core == PERF_CORES_ALL) {
        for (int i = 0; i < numCores; i++) {
            setITCTraceAnable(i,settings->itcTraceEnable);
        }
    }
    else {
            setITCTraceAnable(core,settings->itcTraceEnable);
    }
}

static unsigned long long next_mcount;
static unsigned long long interval;

static void timer_handler(int id,void *data)
{
	int hartID;		// use hartID as CPU index
	struct metal_cpu *cpu;

	cpu = (struct metal_cpu *)data;

	hartID = metal_cpu_get_current_hartid();

	unsigned long pc;

	pc = metal_cpu_get_exception_pc(cpu);

	volatile uint32_t *stimulus = stimulusRegCPUPairing[hartID];

	// block until room in FIFO
	while (*stimulus == 0) { /* empty */ }

	// write the first 32 bits
	*stimulus = (uint32_t)pc;

	// block until room in FIFO
	while (*stimulus == 0) { /* empty */ }

//	// write the second 32 bits - add support for > 32 bit PCs later
//	*stimulus = (uint32_t)(pc >> 32);
//
//	// block until room in FIFO
//	while (*stimulus == 0) { /* empty */ }

	// If perfCounteIndex[hardID] < 0, only do pc-only writes to the itc stimulus regs

	int perfCntrIndex = perfCounterCPUPairing[hartID]; maybe this should be a bit mask of counters??

	if (perfCntrIndex >= 0) {
		unsigned long long perfCntrVal;

		perfCntrVal = metal_hpm_read_counter(cpu, perfCntrIndex);

		// write the first 32 bits
		*stimulus = (uint32_t)perfCntrVal;

		// block until room in FIFO
		while (*stimulus == 0) { /* empty */ }

		// write the second 32 bits
		*stimulus = (uint32_t)(perfCntrVal >> 32);
	}

    intr_count++;

    // set time for next interrupt

    next_mcount += interval;
    metal_cpu_set_mtimecmp(cpu, next_mcount);
}


need to know if pc is 32 or 64 bits!!

static int timerConfig(int core,int interval,int itcChannel,perfCounter)
{
    if ((core < 0) || (core >= numCores)) {
        return 1;
    }

    if (int_interval < 100) {
        int_interval = 100;
    }

    printf("int_interval = %u\n",int_interval);

    int hartID;

    // hartID = metal_cpu_get_current_hartid();

    hartId = getTeImplHartId(core);

    struct metal_cpu *cpu;

    // initialize cpu to stimulus and perf counter pairings to nothing

    cpu = init_ITCPerf();
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

//    interval = int_interval * (timebase / 10000) / 100;

    interval = int_interval * timebase / 1000000;

    printf("setting interval to %u, timebase: %u\n",(uint32_t)interval,(uint32_t)timebase);

    struct metal_interrupt *cpu_intr;

    cpu_intr = metal_cpu_interrupt_controller(cpu);
    if (cpu_intr == NULL) {
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

    rc = metal_interrupt_register_handler(tmr_intr, tmr_id, timerPerfHandler, cpu);
    if (rc < 0) {
        return 1;
    }

//    probably want to either get mtime for t, or set mtime to 0?

    next_mcount = metal_cpu_get_mtime(cpu) + interval;

    metal_cpu_set_mtimecmp(cpu, next_mcount);
    if (metal_interrupt_enable(tmr_intr, tmr_id) == -1) {
        return 1;
    }

    if (metal_interrupt_enable(cpu_intr, 0) == -1) {
        return 1;
    }

	rc = set_perf_channel(perfCntr,ITCchannel);
	if (rc != 0) {
		return 1;
	}

//	emit marker!!

	return 0;


}

int perfTimerISRInit(int core,int interval,uint32_t counterMask,int itcChannel,int stopOnWrap)
{
    perf_settings_t *settings;

    settings.teControl.teInstruction = TS_INSTRUCTION_NONE;
    settings.teControl.teInstrumentation = TE_INSTRUMENTATION_ITC;
    settings.teControl.teStallOrOverflow = 0;
    settings.teControl.teStallEnable = 0;
    settings.teControl.teStopOnWrap = (stopOnWrap != 0);
    settings.teControl.teInhibitSrc = 0;
    settings.teControl.teSyncMaxBTM = TE_SYNCMAXBTM_32;
    settings.teControl.teSyncMaxInst = TE_SYNCMAXINST_32;
    settings.teControl.teSink = TE_SINK_SRAM;

    settings.itcTraceEnable = 1 << itcChannel;

    settings.tsControl.tsDEbug = 0;
    settings.tsControl.tsPrescale = TS_PRESCAL_1;
    settings.tsControl.tsEnable = TS_ENABLE;
    settings.tsBranch = BRNCH_ALL;
    settings.tsInstrumentation = 1;
    settings.tsOwnership = 1;

    settings.teSinkBase = 0;
    settings.teSinkBaseH = 0;
    settings.teSinkLimit = 0;

    int rc;

    rc = traceConfig(core,&settings);
    if (rc!= 0) {
        return rc;
    }

    // tracing should now be configured, but still off

    rc = timerConfig(core,stuff);
    if (rc != 0) {
        return rc;
    }

need to know if pc is 32 or 64 bits?? Could pass a size in to this init routine? That would be easier than detecting?

now enable tracing? Not yet - do that separatly!
should timer interrupt be enabled, or separate enable??

    return 0;
}

int perfTimerEnable();
int perfTimerDisable();

int timerPerfSampleInit(int usInterval,uint32_t counterMask,int itcChannel,int stopOnWrap)
{
    struct metal_cpu *cpu;
    int core

    core = metal_cpu_get_current_hardid(); // use hartId as core

    cpu = metal_cpu_get(core);
    if (cpu == NULL) {
        return 1;
    }

    TEReset(core);

    traceClear(core);

    traceConfig(core,
turn tracing off

    // Enable ITC proccessing on core 0

    traceConfig(core, inst, instru, overflow, stall, sow, srcInhib, maxBtm, maxInst, teSinc) too much stuff!!
enable channel
set mode
set or clean stop on wrap
clear read/write regs

    setTeInstrumentation(0, TE_INSTRUMENTATION_ITC);

	should use channel 6 by default?
	need to enable timestamps somewhere??
			
    // use ITC_Utils to enable a specific channel
    itc_enable(itcChannel);

//    int ic;

//    int rc;

//    rc = perfSampleInit(100,0,6);
//    if (rc != 0) {
//    	return rc;
//    }

	if (perfCntr < -1 || perfCntr > 31) {
		return 1;
	}

	if (ITCchannel > 31) {
		return 1;
	}

	if (int_interval < 100) {
		int_interval = 100;
	}

	printf("int_interval = %u\n",int_interval);

	int hartID;

	hartID = metal_cpu_get_current_hartid();

	struct metal_cpu *cpu;

	// initialize cpu to stimulus and perf counter pairings to nothing

	cpu = init_ITCPerf();
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

//    interval = int_interval * (timebase / 10000) / 100;

    interval = int_interval * timebase / 1000000;

    printf("setting interval to %u, timebase: %u\n",(uint32_t)interval,(uint32_t)timebase);

    struct metal_interrupt *cpu_intr;

    cpu_intr = metal_cpu_interrupt_controller(cpu);
    if (cpu_intr == NULL) {
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

    rc = metal_interrupt_register_handler(tmr_intr, tmr_id, timer_handler, cpu);
    if (rc < 0) {
        return 1;
    }

//    probably want to either get mtime for t, or set mtime to 0?

    next_mcount = metal_cpu_get_mtime(cpu) + interval;

    metal_cpu_set_mtimecmp(cpu, next_mcount);
    if (metal_interrupt_enable(tmr_intr, tmr_id) == -1) {
        return 1;
    }

    if (metal_interrupt_enable(cpu_intr, 0) == -1) {
        return 1;
    }

	rc = set_perf_channel(perfCntr,ITCchannel);
	if (rc != 0) {
		return 1;
	}

//	emit marker!!

	return 0;

}

timerSampleOn(core)
timerSampleOff(core)
