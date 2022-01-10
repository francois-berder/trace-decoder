# SiFive Trace Performance Library

### Introduction

The SiFive Trace Performance Library is intended as an example of how timestamped performance data from on-processor performance counters can be collected and written into the trace buffer using the ITC trace mechanism. The SiFive trace decoder can extract the performance information and write it to textual performance data files for further processing using such tools as Freedom Studio/Trace Compass.

### Description

Some SiFive processors have both on-processor trace capabilities and performance counters. The SiFive Trace Performance Library provides a mechanism to choose which of the performance counters to write to the trace buffer and when to write it. Performance counter information is written to the trace buffer using the ITC trace mechanism as data acquisition messages. The  performance library supports either manual instrumenting the program under trace with library function calls to collect the performance data and write it to the trace buffer, or using a timer based ISR to collect the performance data and write it to the trace buffer.

For details on what trace capabilities and performance counter support the processor design being used supports, reference the documentation for the particular processor implementation being traced.

The Sifive Trace Decoder can extract performance data written with the Sifive Trace Performance Library. Extraced performance and address information is written to text files which can then be processed with Fredom Studio/Trace Compass. For information on using the trace decoder to manually extract the performance information and the format of the extracted information, reference the SiFive Trace Decoder Performance Counter document. To view the information in Freedom Studio with Trace Compass, see the Freedom Studio documentation.

### API

The SiFive perf library provides the following routines for initializing and collecting performance data:

```
int perfInit(int num_cores,int num_funnels)
int perfManualInit(int core,uint32_t counterMask,int itcChannel,int stopOnWrap,int markerCnt)
int perfTimerISRInit(int core,int interval,uint32_t counterMask,int itcChannel,int stopOnWrap,int markerCnt)
int perfWriteCntrs()
```

Below, each of the routines are described.

```
int perfInit(int num_cores,int num_funnels,unsigned long long timerFreq)
```

`PerfInit()` initializes variables used by the sifivPerf routines. PerfInit must be called before any other sifive_perf routines, including other init routines. It should only be called once.

Arguments:

`int num_cores:` The number of cores for the processor being traced.

`int num_funnels:` The number of trace funnels for the processor being traced.

`unsigned long long timerFreq:` The frequency of the timer clock used to activate the ISR. May use 0 if only using manual performance counter recording (not using the ISR). The value can be found in the dst file for the processor, with the label `timebase-frequency`.

Returns 0 on success, otherwise error.

```
int perfManualInit(int core,uint32_t counterMask,int itcChannel,int stopOnWrap,int markerCnt)
```

`PerfManualInit()` should be used when manually instrumenting the program. It will program the trace engine and do any needed setup. `PerfManualInit()` should be called after `perfInit()` and before any performance data is collected.

The trace engine will be set up with trace mode `teInstruction = 0` (no instruction trace) and `teInstrumentation = 1` (generate ITC message for all itStimulus registers). Also, timestamps will be on. note that if instruction trace and itc instrumentation is enabled, the number of itc (data acquisition) messages that will fit in the buffer will be greatly reduced because of the presence of BTM or HTM messages in the buffer.

Arguments:

`int core:` The core number (hartID) of the core being instrumented. Support for instrumenting multiple cores is not yet fully functional.

`uint32_t counterMask:` There are up to 32 HPM performance counters that can be recorded, although actual implementations may be less. Counters are identified by a number 0 - 31, and the bit position in the mask specifies the counter number (e.g. bit 3 is counter 3). Bits that are set in the mask will be recorded. The program counter is always recorded.

`int itcChannel:` Which ITC channel to write the performance data out to. The channel number can be 0 - 31. Channel 6 is the commonly used channel for performance data, and the default channel for the trace decoder (but it can be overridden). All cores should use the same channel. Each core will write to its own set of ITC stimulus registers, so each core's data can be identified in the trace (the trace decoder will supply the source core information in the decoded output).

`int stopOnWrap:` This is a boolean that specifies if the teStopOnWrap bit should be set in the teControl register. Typically, the stop-on-wrap bit should be set. If it is not, the trace decoder may not be able to successfully decode the trace because the trace may never contain an uncompressed time stamp to be able to decoder timestamps correctly.

`markerCnt:` Specifies how often to write a marker into the trace message stream. If 0, an initial marker will be written only on the first call to `perfWriteCntrs()`, and never again. If greater than 0, specifies how many times `perfWriteCntrs()` will be called before inserting a marker message (for example, 100 would write a marker message every 100 times the `perfWriteCntrs()` is invoked). An initial marker message is always written before the very first time performance data is written to the trace buffer. The marker ensures the trace decoder can synchronize the expected performance data item with what is in the buffer, and also informs the trace decoder what performance data is being written. The marker contains a header value that can be identified as a marker, the coutnerMask, and the performance counter programming for all HPM performance counters 2 and greater. Performance counters 0 and 1 are fixed function and not programmable, so no definition is needed.

Returns 0 on success, otherwise error.

```
int perfTimerISRInit(int core,int interval,uint32_t counterMask,int itcChannel,int stopOnWrap,int markerCnt)
```

The `perfTimerISRInit()` function performs the following tasks: Programs a timer based ISR that will be invoked every interval microseconds and write the current execution address and selected HPM counters to the trace buffer using ITC writes for the selected core. The trace engine is programmed for ITC instrumentation and no instruction trace.

The trace engine will be set up with trace mode `teInstruction = 0` (no instruction trace) and `teInstrumentation = 1` (generate ITC message for all itStimulus registers). Also, timestamps will be on. note that if instruction trace and itc instrumentation is enabled, the number of itc (data acquisition) messages that will fit in the buffer will be greatly reduced because of the presence of BTM or HTM messages in the buffer.

Arguments:

`core:` The core to record performance data for. Support for instrumenting multiple cores is not yet fully functional.

`interval:` The period in microseconds the timer ISR will be called at. If less than 100, 100 will be used.

`counterMask:` a 32 bit mask that specifies which HPM counters to read and record in the trace buffer using ITC writes. The address of where the timer interrupt occured is also always written.

`itcChannel:` Specifies which ITC stimulus register to write the performance data to. Can be 0 - 31. Channel 6 is the normal performance data channel.

`stopOnWrap:` If non-zero, will set the stop-on-wrap bit in the teControl register, otherwise it will be cleared. If stop-on-wrap is cleared, the trace decoder may not be able to decode the trace correctly because buffer wrap may overwrite any timestamp synchronization messages (ICT Control message).

`markerCnt:` Specifies how often to write a marker into the trace message stream. If 0, an initial marker will be written only the first time the ISR is invoked, and never again. If greater than 0, specifies how many times the ISR will write performance data to the trace buffer before inserting a marker message (for example, 100 would write a marker message every 100 times the timer ISR is invoked). An initial marker message is always written before the very first time performance data is written to the trace buffer. The marker ensures the trace decoder can synchronize the expected performance data item with what is in the buffer, and also informs the trace decoder what performance data is being written. The marker contains an initial value that can be identified as a marker, the coutnerMask, and the performance counter programming for all HPM performance counters 2 and greater. Performance counters 0 and 1 are fixed function and not programmable.

Returns 0 on success, otherwise error.

```
int perfWriteCntrs()
```

Used for manual instrumentation of the program. Calling `perfWriteCntr()` performs the same function as the timer ISR, but is explicitly called in the program when manually instrumenting for performance data collection. The `perfInit()` and `perfManualInit()` functions must be called before the first call to `perfWriteCntrs()`.

Returns 0 on success, otherwise error.

### SiFive Perf Library Usage

Whether using manual performance data collection or a timer ISR to collect performance data, some modification of the program under trace will need to be done. If using the manual method, some initialization and setup routines will need to be called, and then `perfWriteCntrs()` will need to be called each time it is desired to write the performance data to the trace buffer. Both methods are outlined below.

If using any HPM counters other than 0 and 1 to collect performance data, it is the programmers responsibility to add code to program the desired counters correctly using the metal HPM functions. Counter 0 and 1 are fixed function and cannot be configured differently.

Manual Performance Data Collection:

Prior to collecting performance data, a call to `perfInit(int numCore, int numFunnels)` must be made. The first argument specifies the number of cores in the processor, and numFunnels specifies the number of funnels. NumFunnels should be 0 if there are no funnels.

After calling `perfInif()`, `perfManualInit(int core,uint32_t counterMask,int itcChannel,int stopOnWrap,int markerCnt)` should be called. The core argument specifies which core's performance counters will be collected. Currently, the support for collecting multi-core performance data concurrently is not complete.

Add calls to your code for `perfWriteCntrs()` wherever you want performance data to be collected and written.

Timer ISR Performance Data Collection:

To enable a timer ISR to collect performance data, First call `perfInit(int numCore, int numFunnels)`, similar to manual performance data collection. Next, add a call to `perfTimerISRInit(int core,int interval,uint32_t counterMask,int itcChannel,int stopOnWrap,int markerCnt)`. After the call to `perTimerISRInit()`, the timer is running and performance data is being collected/written to the trace buffer.

### Performance Data Format

All performance data is written to the trace buffer using ITC stimulus register writes creating data acquisition messages in the trace buffer. Only the itc channel specified in the initialization functions is used for all writes.

Marker Message Format:

Marker messages provide information to the trace decoder on what HPM counters are being recorded and the programming of the HPM counters. Every marker messages begins with a full 32 bit data acquisition messages with the data value `0x70657266` (which is ('p'<<24)|('e'<<16)|('r'<<8)|('f'<<0)). The beginning value allows the trace decoder to identify a marker in the performance data stream. It is possible an actual counter value will coincide with the marker header value, but unlikely.

Following the marker identification will be a second 32 bit data acquisition message that contains the counter mask. Non-zero bits in the mask give which HPM counters will be recorded.

Following the counter mask will be a series of 0 to 30 data acquisition messages with the programming of any HPM counters from 2 and up that are being recorded. The programming for HPM counters 0 and 1 is not provided in the  marker (if they are selected by the mask) because they are fixed-function and cannot be programmed.

HPM performance counter configuration registers are 64 bits, but the current metal HPM routines only read the lower 32 bits. Because of this restriction, only the lower 32 bits are currently written into the marker messages. In the future all 64 bits will be written as two full 32 bit data acquisition messages.

Below is an example of the format for a marker written into the trace buffer. It assumes ITC channel 6 for performance data, and an HPM counter mask of 0x00000003. This marker will not contain any counter configuration data because only counters 0 and 1 are being captured.

| Message Number | ID Tag | Value | Description |
| :------------: | :----: | :---: | :---------- |
| #1 | 0x00000018 | 0x70657266 | Marker identification header |
| #2 | 0x00000018 | 0x00000003 | HPM counter mask |

Performance Data Format:

Every time performance data is recorded in the trace buffer, the first thing written is the address of where in the binary being traced execution was as when either the ISR was invoked or a manual write to record perfdata was called. On processors with 32 bit address space, a single full 32 bit data acquisition message will be written. On systems with more than 32 bits of address space, if the upper 32 bits of the address are 0, only a single 32 bit data acquisition message will be written. If the upper 32 bits are not 0, bit 0 in the lower 32 bits will be set and written using a full 32 bit data acquisition message. Next, the upper 32 bits will be written using a second 32 bit data acquisition message. Valid addresses for the processor will always have bit 0 clear, so setting bit 0 to signal a second 32 bits of address follows can be used when the address is greater than 32 bits. When the trace decoder sees bit 0 set in the first address message, it knows there is a second address following for the upper 32 bits.

Following the address, the selected HPM counters will be written from the lower numbers to the higher numbered (the order is deterministic). To reduce the size of the performance data write to the trace buffer and increase the number of performance data measurements that will fit in the trace buffer, the number of writes to the trace buffer for each performance counter is variable. HPM performance counters can be up to 64 bits each. The actual size is implementation dependent. Currently, the SiFive performance library will do either a single 32 bit write for each performance counter (the lower 32 bits), or a 48 bit write (more than 32 bits).  If the actual value of the performance counter fits in 32 bits (the upper 32 bits are 0), a single 32 bit data acquisition message will be written into the trace buffer. If the upper 32 bits are not 0, the lower 32 bits will first be written as a full ITC stimulus write and the next 16 bits (bits 32 - 47) will be written as a 16 bit ITC stimulus write. This allows the trace decoder to deterministically parse the data acquisition stream and determine if 32 or 48 bits were written for a performance counter (by the presence of 16 bit data acquisition messages). 48 bits per counter is likely adequate, but if this restriction proves problematic, the format will be modified to provide additional bits.

Below is an example of the format for performance counter data in the trace buffer. It assumes ITC channel 6 for performance data, more than 32 bits of address space, and an HPM counter mask of 0x00000003. In this example, HPM counter 0 has more than 32 bits of significant data and counter 1 has 32 bits or less.

| Message Number | ID Tag | Value | Description |
| :------------: | :----: | :---: | :---------- |
| #1 | 0x00000018 | 0x800022c6 | Lower 32 bits of address. Bit 0 is clear which indicates the address fits in 32 bits |
| #2 | 0x00000018 | 0x000029e3 | Lower 32 bits of count 0 |
| #3 | 0x0000001a | 0x1234 | Next 16 bits of counter 0. The ID tag of 0x1a instead of 0x18 indicates this is a 16 bit write and not 32 |
| #4 | 0x00000018 | 0x12345678 | Lower 32 bits of counter 1. Upper 32 bats are all 0, so no additional write for counter 1 |

Support for programming what the HPM counters 2 and up actually count can be done using the HPM Metal routines, and is not part of this SiFive performance library.

### Current Limitations

Stack size needs to be at least 800. Check the linker script to make sure. If odd behavior is seen, try increasing the stack size further. The amount needed will depend on the actual program being traced.

The current SiFive performance library has the following known limitations:

Counter definitions as part of the perf markers are only the lower 32 bits. The limitation is created by the metal library routine `metal_hpm_get_event()`, and will be fixed in the future.

Multicore support is incomplete and untested.

Funnel support is incomplete.

Only processors with support for the ICT Control Message CKSRC = 0, CKDF = 0 are known to work for collecting decodable traces. This is because the Control 0, 0 messages is used to insert an uncompressed timestamp into the trace buffer using an ICT message. Without an initial uncompressed timestamp, timestamp values cannot be correctly decoded.

The SRAM trace buffers on processors have limited size and may restrict the amount of data that can be collected. If available, the SBA sink may be used to collect larger amounts of performance data. Using an SBA sink will require modifications to the init routines in the SiFive perf library.

### Example Program

The program below shows how to use the timer ISR to collect performance data. Create a project in Freedom Studio and replace the main program with the one below. Make sure traceBaseAddress is set correctly for the processor being used. Also, add sifive_trace.h, sifive_perf.h, and sifive_perf.c to the project. The program below requires a stack size of 800, set in the linker script.

```
/* Copyright 2019 SiFive, Inc */
/* SPDX-License-Identifier: Apache-2.0 */

#include <stdio.h>
#include <metal/cpu.h>

#include "sifive_trace.h"
#include "sifive_perf.h"

#define traceBaseAddress 0x10000000

// create the trace memory map object
struct TraceRegMemMap volatile * const tmm[] = {(struct TraceRegMemMap*)traceBaseAddress};

#define caBaseAddress 0
// create the cycle accurate trace memory map object
struct CaTraceRegMemMap volatile * const cmm[] = {(struct CaTraceRegMemMap*)caBaseAddress};

#define tfBaseAddress 0
// create the trace funnel memory map object
struct TfTraceRegMemMap volatile * const fmm = (struct TfTraceRegMemMap*)tfBaseAddress;

int main (void)
{
    int rc;
    int hartID;
    unsigned long long timerFreq = 10833333;

    hartID = metal_cpu_get_current_hartid();

    rc = perfInit(sizeof tmm / sizeof tmm[0],0,timerFreq);
    if (rc != 0) {
        return rc;
    }

    rc = perfTimerISRInit(hartID,100,3,6,1,10);
    if (rc != 0) {
        return rc;
    }

    // loop until trace buffer wraps

    while ((getTeSinkWp(0) & 1) == 0) {
        printf("control: 0x%08x rp: %d wp: %d, itcTraceEnable: 0x%08x\n",getTeControl(0),getTeSinkRp(0),getTeSinkWp(0),getITCTraceEnable(0));
    }

    printf("control: 0x%08x rp: %d wp: %d, itcTraceEnable: 0x%08x\n",getTeControl(0),getTeSinkRp(0),getTeSinkWp(0),getITCTraceEnable(0));

    return 0;
}
```

