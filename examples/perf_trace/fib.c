#include <stdio.h>
#include <stdlib.h>

#include "sifive_trace.h"
#include "sifive_bare_perf.h"

#define traceBaseAddress 0x10000000
#define tfBaseAddress	0x10018000

// create the trace memory map object
//struct TraceRegMemMap volatile * tmm[] = {(struct TraceRegMemMap*)traceBaseAddress,(struct TraceRegMemMap*)(traceBaseAddress+0x1000)};
struct TraceRegMemMap volatile * tmm[] = {(struct TraceRegMemMap*)traceBaseAddress};

// create the trace funnel memory map object
//struct TfTraceRegMemMap volatile * fmm = (struct TfTraceRegMemMap*)tfBaseAddress;
struct TfTraceRegMemMap volatile * fmm = (struct TfTraceRegMemMap*)0;

// Define the performance events to collect:

perfEvent perfCntrList[] = {
        {
                .type = perfEventHWGeneral,
                .code = HW_CPU_CYCLES,
                .event_data = 0
        },
        {
                .type = perfEventHWGeneral,
                .code = HW_TIMESTAMP,
                .event_data = 0
        },
        {
                .type = perfEventHWGeneral,
                .code = HW_INSTRUCTIONS,
                .event_data = 0
        },
        {	// int load/stores retired
                .type = perfEventHWRaw,
                .code = 0,
                .event_data = (0xffffffffffUL << 8) | (0 << 0)
        },
        {	// int load/stores retired
                .type = perfEventHWRaw,
                .code = 0,
                .event_data = (1 << 9) | (1 << 10) | (0 << 0)
        },
        {	// branch jump mispredictions
                .type = perfEventHWRaw,
                .code = 0,
                .event_data = (1 << 13) | (1 << 14) | (1 << 0)
        }
};

unsigned long fib(unsigned long f)
{
  if (f == 0) {
    return 0;
  }

  if (f == 1) {
    return 1;
  }

  return fib(f-2) + fib(f-1);
}

int main()
{
  unsigned long f;
  int rc;

  char *p;

//  rc = perfInit(sizeof tmm / sizeof tmm[0],1);
  rc = perfInit(sizeof tmm / sizeof tmm[0],0);
  if (rc != 0) {
    printf("perfInit() failed. Exiting\n");

    return rc;
  }

  rc = perfFuncEntryExitInit(perfCntrList,sizeof perfCntrList/sizeof perfCntrList[0],6,perfCount_Raw,32*1024);
//  rc = perfFuncEntryExitInit(perfCntrList,sizeof perfCntrList/sizeof perfCntrList[0],6,perfCount_DeltaXOR,32*1024);
//  rc = perfFuncEntryExitInit(perfCntrList,sizeof perfCntrList/sizeof perfCntrList[0],6,perfCount_Delta,0);

  if (rc != 0) {
    printf("perfFuncEntryExitInit(): failed\n");

    return rc;
  }

  perfTraceOn();

  unsigned long i = 10;

  f = fib(i);

  printf("fib(%lu) = %lu\n",i,f);

  perfTraceOff();

// perfWriteTrace(NULL);

  return 0;
}
