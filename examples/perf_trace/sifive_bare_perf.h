/* Copyright 2021 SiFive, Inc */

#ifndef SIFIVE_PERF_H_
#define SIFIVE_PERF_H_

#include <stdint.h>

#include <metal/cpu.h>
#include <metal/hpm.h>

#define HW_NO_EVENT			0
#define	HW_CPU_CYCLES			1
#define HW_INSTRUCTIONS			2

#define HW_TIMESTAMP			(0x80)

#define PERF_MEM_POOL_SIZE (256*1024)

#define PERF_MAX_CORES		8
#define PERF_MAX_CNTRS		32
#define PERF_MARKER_VAL		(('p' << 24) | ('e' << 16) | ('r' << 8) | ('f' << 0))

enum {
        perfEventHWGeneral = 0,
        perfEventHWCache = 1,
        perfEventHWRaw = 2,
        perfEventFW = 15,
};

typedef enum {
    perfRecord_FuncEnter = 0,
    perfRecord_FuncExit = 1,
    perfRecord_Manual = 2,
    perfRecord_ISR = 3,
} perfRecordType_t;

typedef enum {
    perfCount_Raw = 0,
    perfCount_Delta = 1,
    perfCount_DeltaXOR = 2,
} perfCountType_t;

typedef struct {
        unsigned int ctrIdx;
        int type;
        union {
                int code;
                struct {
                        int cache_id;
                        int op_id;
                        int result_id;
                };
        };
        unsigned long event_data;
	unsigned long ctrInfo;
} perfEvent;

int perfInit(int num_cores,int num_funnels);
int perfTimerISRInit(perfEvent *perfCntrList,int numCntrs,int itc_channel,perfCountType_t cntType,uint32_t SBABufferSize,int interval);
int perfManualInit(perfEvent *perfCntrList,int numCntrs,int itcChannel,perfCountType_t cntType,uint32_t SBABufferSize);
int perfFuncEntryExitInit(perfEvent *perfCntrList,int numCntrs,int itcChannel,perfCountType_t cntType,uint32_t SBABufferSize);

int perfTraceOn();
int perfTraceOff();

void __cyg_profile_func_enter(void *this_fn,void *call_site);
void __cyg_profile_func_exit(void *this_fn,void *call_site);

void dump_trace_encoder(int core);
void dump_trace_funnel();

#endif // SIFIVE_PERF_H_
