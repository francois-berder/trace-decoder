# SiFive Trace Performance Library

### Introduction

The SiFive Trace Performance Library is intended as an example of how timestamped performance data from on-processor performance counters can be collected and written into the trace buffer using the ITC trace mechanism. The SiFive trace decoder can extract the performance information and write it to textual performance data files for further processing using such tools as Freedom Studio/Trace Compass.

### Description

Some SiFive processors have both on-processor trace capabilities and performance counters. The SiFive Trace Performance Library provides a mechanism to choose which of the performance counters to write to the trace buffer and when to collect and write them during program execution for later processing. Performance counter information is written to the trace buffer using the ITC trace mechanism as data acquisition messages. The  performance library supports either manual instrumenting the program under trace with library function calls to collect the performance data and write it to the trace buffer, or using a timer based ISR to collect the performance data and write it to the trace buffer, or recording performance data at the entry and exit of functions using the gcc or clang `-finstrument-functions` compiler option.

For details on what trace capabilities and performance counter support the processor design being used supports, reference the documentation for the particular processor implementation being traced.

The Sifive trace decoder can extract performance data written with the Sifive Trace Performance Library. Extracted performance and address information is written to text files which can then be processed with Freedom Studio/Trace Compass, or with a custom tool. For information on using the trace decoder to manually extract the performance information and the format of the extracted information, reference the SiFive Trace Decoder Performance Counter document. To view the information in Freedom Studio with Trace Compass, see the Freedom Studio documentation.

The performance library supports collecting performance data for both single-core processors and multiple cores processors. Currently it does not support multi-cluster (more than one funnel), but that support would be easy to add. Even though performance data can be collected on multi-core processors, currently performance data can only be collected on one of the cores (multi-core support is incomplete).

### API

The SiFive perf library consists of a Linux version and a bare-metal version. For Linux, use the files:

```
sifive_linux_perf.h
sifive_linux_perf.c
```

For bare metal, use the Freedom Metal versions:

```
sifive_bare_perf.h
sifive_bare_perf.c
```

The SiFive perf library provides the following routines and data types for initializing and collecting performance data:

```
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

int perfInit(int num_cores,int num_funnels)
int perfManualInit(perfEvent *perfCntrList,int numCntrs,int itcChannel,perfCountType_t cntType,uint32_t SBABufferSize)
int perfTimerISRInit(perfEvent *perfCntrList,int numCntrs,int itcChannel,perfCountType_t cntType,uint32_t SBABufferSize,int interval)
int perfFuncEntryExitInit(perfEvent *perfCntrList,int numCntrs,int itcChannel,perfCountType_t cntType,uint32_t SBABufferSize)
int perfWriteCntrs()
int perfTraceOn()
int perfTraceOff()
int perfWriteTrace(char *file)
```

Below, each of the routines are described.

```
int perfInit(int num_cores,int num_funnels)
```

`PerfInit()` initializes variables used by the SiFive performance routines. PerfInit must be called before any other SiFive performance routines, including other init routines. It should only be called once. If tracing multiple cores, only one core should call perfInit().

Arguments:

`int num_cores:` The number of cores for the processor being traced.

`int num_funnels:` The number of trace funnels for the processor being traced. In the current implementation, more than one funnel is not currently supported (multi-cluster). If there are no funnels, use 0.

Returns 0 on success, otherwise error.

```
int perfManualInit(perfEvent *perfEventList,int numEvents,int itcChannel,perfCountType_t cntType,uint32_t SBABufferSize)
```

`PerfManualInit()` should be used when manually instrumenting the program (adding explicit calls to perfWriteCntrs() at the places you would like to record the performance counters). It will program the trace engine and do any needed setup. `PerfManualInit()` should be called after `perfInit()` and before any performance data is collected.

The trace engine will be set up with trace mode `teInstruction = 0` (no instruction trace) and `teInstrumentation = 1` (generate ITC message for all itStimulus registers). Also, timestamps will be on. The stop-on-wrap bit will be set so that if the buffer fills, tracing will stop (instead of wrapping to the beginning of the buffer and continuing). Note that if instruction trace and itc instrumentation is enabled (through your own or customized init), the number of itc (data acquisition) messages that will fit in the buffer will be greatly reduced because of the presence of BTM or HTM messages in the buffer.

If tracing multiple cores, each core to trace needs to call `perfManualInit()`. If only tracing a single core, only that core needs to call `perfManualInit()`. (Note: Currently only tracing a single core is supported, but the system may be multi-core.)

Arguments:

`perfEventList:` There are up to 32 HPM performance counters that can be recorded, although actual implementations may be less. The perfEventList argument should be a pointer to an array of perfEvent structures that define how to program the counters and what events to count. The elements of the structure loosely follow the OpenSBI event programming/selection format. The fields that must be initialized prior to calling perfManualInit are:

type: The type field follows the definition in the RISC-V OpenSBI Specification. The types supported in the performance library are 0 (hardware general events), 1 (Hardware cache events), and 2 (Hardware raw events).

code: The definition of the code field is dependent on the type field. For type 0, the code field specifies which general event to count. See the OpenSBI specification for a complete list. For type 1, the code field is not used. Instead, the cache_id, op_id, and result_id fields should be initialized with the values from the OpenSBI specification. Type 2 events do not use the code field.

cache_id, op_id, result_id: Used only for type 1 events. Use the values in the OpenSBI specification

event_data: Only used for type 2 events (Raw). The event data field should be programmed with the value desired to program into the HPM event registers. See the manual for the processor being traced to see what value to program.

The order of events in the perfEventList is not important. The first three event counters are fixed function, and if selected will be programmed correctly independent of where they are in the perf event list.

`numEvents:` The number of perf counter events in the perfEventList array.

`itcChannel:` Which ITC channel to write the performance data out to. The channel number can be 0 - 31. Channel 6 is the commonly used channel for performance data, and the default channel for the trace decoder (but it can be overridden). All cores being traced should use the same channel. Each core will write to its own set of ITC stimulus registers, so each core's data can be identified in the trace (the trace decoder will supply the source core information in the decoded output).

`cntType:` Selects one of `perfCount_Raw`, `perfCount_Delta`, or `perfCount_DeltaXOR` for how the performance counts are recorded in the trace buffer. `perfCount_Raw` records the full raw value of the performance counter and addresses. `perfCount_Delta` records the additive delta of the performance counter since the last write, and full addresses. `perfCounte_DeltaXOR` records the XOR delta of the performance counters and addresses since the last write, and most closely mimics how data is recorded by the trace engine (addresses and timestamps). Performance counter wrap is automatically handled for PerfCount_Delta and perfCount_DeltaXOR if the counter does not wrap more than once. PerfCount_Raw always records the actual raw value of the counter read out of the register.

`SBABufferSize:` The SBABufferSize argument specifies the size of the trace buffer to allocate. If SBABufferSize is 0, the SRAM buffer in the trace engine will be selected as the trace buffer. If the SBABufferSize argument is greater than 0, a system memory buffer will be allocated with the specified size, which is useful when a larger buffer is desired, but the extra SBA traffic of the writes to the buffer may skew performance and counts.

Returns 0 on success, otherwise error.

```
int perfTimerISRInit(perfEvent *perfCntrList,int numCntrs,int itcChannel,perfCountType_t cntType,uint32_t SBABufferSize,int interval)
```

The `perfTimerISRInit()` function performs the following tasks: Programs a timer based ISR that will be invoked every interval microseconds and write the current execution address and selected HPM counters to the trace buffer using ITC writes for the selected core (bare-metal version only. Not available on Linux). The trace engine is programmed the same as perfManualInit(). In addition to the requested performance counters, the address of where execution was at the time of the interrupt is recorded. TimerISR tracing is currently not supported for Linux performance tracing.

When using timer based performance tracing on bare metal, a stack size of at least 800 bytes is needed. Check the linker script for your project, and if the stack size is less than 800 bytes, adjust it accordingly. The default stack size for a project is typically 400 bytes. If odd behavior is observed, such as program crashes, try increasing the stack size further.

If tracing multiple cores, each core to be traced must call `perfTimerISRInit()`.

After calling `perfTimerISRInit()`, the counters are running and the ISR is being called, but nothing will be recorded in the trace buffer until `perfTraceOn()` is called.

Arguments:

`perfEventList:` There are up to 32 HPM performance counters that can be recorded, although actual implementations may be less. The perfEventList argument should be a pointer to an array of perfEvent structures that define how to program the counters and what events to count. The elements of the structure loosely follow the OpenSBI event programming/selection format. The fields that must be initialized prior to calling perfManualInit are:

`type:` The type field follows the definition in the RISC-V OpenSBI Specification. The types supported in the perf library are 0 (hardware general events), 1 (Hardware cache events), and 2 (Hardware raw events).

`code:` The definition of the code field is dependent on the type field. For type 0, the code field specifies which general event to count. See the OpenSBI specification for a complete list. For type 1, the code field is not used. Instead, the cache_id, op_id, and result_id fields should be initialized with the values from the OpenSBI specification. Type 2 events do not use the code field.

`cache_id, op_id, result_id:` Used only for type 1 events. Use the values in the OpenSBI specification

`event_data:` Only used for type 2 events (Raw). The event data field should be programmed with the value desired to program into the HPM event registers. See the manual for the processor being traced to see what value to program.

The order of events in the perfEventList is not important. The first three event counters are fixed function, and if selected will be programmed correctly independent of where they are in the perf event list.

`numEvents:` The number of perf counter events in the perfEventList array.

`itcChannel:` Which ITC channel to write the performance data out to. The channel number can be 0 - 31. Channel 6 is the commonly used channel for performance data, and the default channel for the trace decoder (but it can be overridden). All cores being traced should use the same channel. Each core will write to its own set of ITC stimulus registers, so each core's data can be identified in the trace (the trace decoder will supply the source core information in the decoded output).

`cntType:` Selects one of `perfCount_Raw`, `perfCount_Delta`, or `perfCount_DeltaXOR` for how the performance counts are recorded in the trace buffer. `perfCount_Raw` records the full raw value of the performance counter and addresses. `perfCount_Delta` records the additive delta of the performance counter since the last write, and full addresses. `perfCounte_DeltaXOR` records the XOR delta of the performance counters and addresses since the last write, and most closely mimics how data is recorded by the trace engine (addresses and timestamps). Performance counter wrap is automatically handled for PerfCount_Delta and perfCount_DeltaXOR if the counter does not wrap more than once. PerfCount_Raw always records the actual raw value of the counter read out of the register.

`SBABufferSize:` The SBABufferSize argument specifies the size of the trace buffer to allocate. If SBABufferSize is 0, the SRAM buffer in the trace engine will be selected as the trace buffer. If the SBABufferSize argument is greater than 0, a system memory buffer will be allocated with the specified size, which is useful when a larger buffer is desired, but the extra SBA traffic of the writes to the buffer may skew performance and counts..

`interval:` The period in microseconds the timer ISR will be called at. If less than 100, 100 will be used.

Note: Not currently supported for the Linux performance library.

Returns 0 on success, otherwise error.

```
int perfFuncEntryExitInit(perfEvent *perfCntrList,int numCntrs,int itcChannel,perfCountType_t cntType,uint32_t SBABufferSize)
```

`PerfFuncEntryExitInit()` should be used when the program being traced has been compiled with function entry/exit instrumentation (by using the -finstrument-functions compiler switch). The -finstrument-functions compiler switch automatically adds calls at all function entries and exits to routines provided in the performance library that will record information in the trace buffer. When running on bare metal, all cores should call `perfFunctionEntryExitInit()`, and performance trace information will be recorded for all cores. The exception would be if any core is executing code that is not instrumented with the -finstrument-functions option, they do not need to call `perfFunctionEntryExitInit()`. Typically, all cores run the same program, so they will all be executing instrumented functions.

The performance trace library does not currently support collecting data on multiple cores. If collecting a performance trace on a multicore platform, the application being traced should be limited to a single core using the linux `taskset` command.

In addition to the requested performance counters, each time a function is called, the address of the function being called and the address of the function it being called from will be recorded. Each time a function exits, the address of the function exiting and the address of the function returning to will be recorded. Note that these addresses are the address of the function start, and not the address of the call or return.

The `perfFuncEntryExitInit()` function will program the trace engine and do any needed setup. `perfFunctEntryExitInit()` should be called after `perfInit()` and before any performance data is collected.

The trace engine will be set up with trace mode `teInstruction = 0` (no instruction trace) and `teInstrumentation = 1` (generate ITC message for all itStimulus registers). Also, timestamps will be on. The stop on wrap bit will be set so that if the buffer fills, tracing will stop (instead of wrapping to the beginning of the buffer and continuing). Note that if instruction trace and itc instrumentation is enabled (through your own or customized init), the number of itc (data acquisition) messages that will fit in the buffer will be greatly reduced because of the presence of BTM or HTM messages in the buffer.

Arguments:

`perfEventList:` There are up to 32 HPM performance counters that can be recorded, although actual implementations may be less. The perfEventList argument should be a pointer to an array of perfEvent structures that define how to program the counters and what events to count. The elements of the structure loosely follow the OpenSBI event programming/selection format. The fields that must be initialized prior to calling perfManualInit are:

`type:` The type field follows the definition in the RISC-V OpenSBI Specification. The types supported in the perf library are 0 (hardware general events), 1 (Hardware cache events), and 2 (Hardware raw events).

`code:` The definition of the code field is dependent on the type field. For type 0, the code field specifies which general event to count. See the OpenSBI specification for a complete list. For type 1, the code field is not used. Instead, the cache_id, op_id, and result_id fields should be initialized with the values from the OpenSBI specification. Type 2 events do not use the code field.

`cache_id, op_id, result_id:` Used only for type 1 events. Use the values in the OpenSBI specification

`event_data:` Only used for type 2 events (Raw). The event data field should be programmed with the value desired to program into the HPM event registers. See the manual for the processor being traced to see what value to program.

The order of events in the perfEventList is not important. The first three event counters are fixed function, and if selected will be programmed correctly independent of where they are in the perf event list.

`numEvents:` The number of perf counter events in the perfEventList array.

`itcChannel:` Which ITC channel to write the performance data out to. The channel number can be 0 - 31. Channel 6 is the commonly used channel for performance data, and the default channel for the trace decoder (but it can be overridden). All cores being traced should use the same channel. Each core will write to its own set of ITC stimulus registers, so each core's data can be identified in the trace (the trace decoder will supply the source core information in the decoded output).

`cntType:` Selects one of `perfCount_Raw`, `perfCount_Delta`, or `perfCount_DeltaXOR` for how the performance counts are recorded in the trace buffer. `perfCount_Raw` records the full raw value of the performance counter and addresses. `perfCount_Delta` records the additive delta of the performance counter since the last write, and full addresses. `perfCounte_DeltaXOR` records the XOR delta of the performance counters and addresses since the last write, and most closely mimics how data is recorded by the trace engine (addresses and timestamps). Performance counter wrap is automatically handled for PerfCount_Delta and perfCount_DeltaXOR if the counter does not wrap more than once. PerfCount_Raw always records the actual raw value of the counter read out of the register.

`SBABufferSize:` The SBABufferSize argument specifies the size of the trace buffer to allocate. If SBABufferSize is 0, the SRAM buffer in the trace engine will be selected as the trace buffer. If the SBABufferSize argument is greater than 0, a system memory buffer will be allocated with the specified size, which is useful when a larger buffer is desired, but the extra SBA traffic of the writes to the buffer may skew performance and counts..

Returns 0 on success, otherwise error.

```
int perfWriteCntrs()
```

Used for manual instrumentation of the program. Calling `perfWriteCntrs()` writes the performance counters  selected with the perfManualInit() function for the core calling `perfWriteCntrs()`, and also records the address of where `perfWriteCntrs()` was called.

Returns 0 on success, otherwise error.

```
int perfTraceOn()
```

After calling the desired init functions, `perfTraceOn()` must be called before any performance data will be recorded. Prior to calling perfTraceOn(), calls to the performance collection routines (either manual, timer based, or compiler instrumentation) perform no-operations. Calling `perfTraceOn()` should not be called until after the initialization routines have been called, and calling `perfTraceOn()` before finishing initialization will return a non-zero value.

Calling `perfTraceOn()` also writes a trace configuration header to the trace buffer. This information includes the type of the counts recorded (raw, delta, or delta XOR), what counters will be recorded and how they are configured.

Returns 0 on success, otherwise error.

```
int perfTraceOff()
```

Calling `perfTraceOff()` disables collection of performance data until the next call to `perfTraceOn()`. After calling `perfTraceOff()`, calls to either manual or automatic performance data collection do not record data in the trace buffer.

Returns 0 on success, otherwise error.

```
int perfWriteTrace(char *file)
```

The `perfWriteTrace()` function writes the trace data in the trace buffer out to disk. Only as much valid data as has been recorded is written. It is not necessary to disable tracing using the `perfTraceOff()` function prior to calling `perfWriteTrace()`. Calling `perfWriteTrace()` does not change the state of tracing being on or off. The `perfWriteTrace()` function is only supported on Linux targets. If collecting performance data on a bare-metal target, the debugger will capture the performance data and write it to a file.

Note: Linux only; not available for the bar-metal version.

Arguments:

`file:` The path/name of the file to write the performance data to. If the `file` argument is NULL, the name `trace.rtd` will be used, and created in the current working directory.

Returns 0 on success, otherwise error.

### Memory Allocation:

When performing performance tracing on bare-metal systems, there is an issue with `malloc()` not functioning properly. The performance trace library needs to do some memory allocation for internal data structures and the SBA trace buffer if used. The workaround is the library statically allocates a memory buffer and allocates memory from that.

In the file sifive_bare_perf.h, there is a define:

```
#define PERF_MEM_POOL_SIZE (256*1024)
```

The PERF_MEM_POOL_SIZE define specifies how many bytes to statically allocate for use by the performance trace library. It needs to be at least the size of the SBA buffer (if used) plus a few kbytes for internal usage.

### SiFive Perf Library Usage

Whether using manual performance data collection, a timer ISR to collect performance data, or function entry/exit performance data collection, some modification of the program under trace will need to be done. All methods are outlined below. As a general rule, only one type of performance data collection may be supported at a time. For example, you cannot do both function entry/exit and timer ISR based data collection. This is also true for manual performance data collection.

Independent of the type of performance data collection (manual, timer ISR, or function entry/exit), the following includes will need to be added to any files with explicit calls to the data collection routines (such as the initialization routine, manual collection routine, the on/off routines, or the write data to a file routine):

The names of the necessary include files and library C files are slightly different for the bare-metal and Linux versions.

```
#include "sifive_trace.h"
```

For bare-metal, use the include:

```
#include "sifive_bare_perf.h"
```

If using a Linux target:

```
#include "sifive_linux_perf.h"
```

Also, an array named `tmm` will need to be defined that lists the base addresses of the trace funnels for the system, such as:

```
#define traceBaseAddress 0x10000000

// create the trace memory map object
struct TraceRegMemMap volatile * tmm[] = {(struct TraceRegMemMap*)traceBaseAddress,(struct TraceRegMemMap*)(traceBaseAddress+0x1000)};
```

The `tmm` array lists the base addresses for a dual core system.

Next, the address of the trace funnel (if present) will need to be defined, as in:

```
#define tfBaseAddress	0x10018000

// create the trace funnel memory map object
struct TfTraceRegMemMap volatile * fmm = (struct TfTraceRegMemMap*)tfBaseAddress;
```

If the system does not have a trace funnel, set `fmm` to `NULL`.

Manual Performance Data Collection:

Prior to collecting performance data, a call to `perfInit(int numCore, int numFunnels)` must be made. The first argument specifies the number of cores in the processor, and numFunnels specifies the number of funnels. NumFunnels should be 0 if there are no funnels. If multi-core, only one core should call `perfInit()`.

After calling `perfInif()`, `perfManualInit()` should be called. Each core that will be collecting performance data should call `perfManualInit()` before any performance data is collected. After calling `perfManualInit()`, `perfTraceOn()` will need to be called to enable writing performance data to the trace buffer.

Add calls to your code for `perfWriteCntrs()` wherever you want performance data to be collected and written to the trace buffer.

If collecting performance data for a Linux target, When done collecting performance data, a call to `perfWriteTrace()` must be made. If on a bare-metal target, the debugger will collect the performance data and write it to a file when done.

Timer ISR Performance Data Collection:

To enable a timer ISR to collect performance data, First call `perfInit()`, similar to manual performance data collection. If multi-core, only one core should `call perfInit()`. Next, add a call to `perfTimerISRInit()`. After the call to `perfTimerISRInit()`, the timer is running but data will not be written to the trace buffer until `perfTraceOn()` is called. Each core wishing to collect performance data for should call `perfTimerISRInit()`. The `perfTraceOn()` function should not be called until all cores that are going to be traced have called `perfTimerISRInit()`. If running on a Linux target, a call to `perfWriteCntrs()` should be made to write the performance data to a file.

Function Entry/Exit Performance Data Collection:

To enable collecting performance data at the entry/exit of functions, first add code so a single core calls `perfInit()`. Next, every core should call `perfFuncEntryExitInit()`. When doing function entry/exit performance data collection, all cores will be calling code that has been instrumented by the compiler to call special functions at the entry and exit of functions. After calling `perfFuncEntryExitInit()`, performance data will not be collected until a call to `perfTraceOn()`. If running on a Linux target, after the performance data has been collected, a call to `perfWriteCntrs()` should be made to write the performance data to a file.

Function entry/exit level tracing requires compiling the desired code to trace with the `-finstrument-functions` switch. This will add special code to the entry and exit of functions to call the functions `__cyg_profile_func_exit()` and `__cyg_profile_funct_enter()` in the SiFive performance library. Makefile modifications will be required. It is advised to not compile the metal functions with the `-finstrument-functions` switch because the __cyg_profile_func entry and exit functions make use of some of the metal functions, which would cause infinite recursion. To explicitly force a function to not be compiled with the function entry/exit instrumentation, you can annotate a function declaration with the no_instruemnt_function attribute (see the sifive_bare_perf.c or sifive_linux_perf.c file for examples).

A complete example is at the end of this document in the Example Program section.

### Performance Data Format

All performance data is written to the trace buffer using ITC stimulus register writes creating data acquisition messages in the trace buffer. Only the itc channel specified in the initialization functions is used for all writes. All data is in binary.

The captured performance trace is a combination of header information and performance trace data. The header contains information on the format of performance count information (raw, delta, or delta XOR). The header also contains information on the HPM counters being measured (which ones, and how they are configured). The header is written to the trace buffer when `perfTraceOn()` is called.

The performance data records following the header contain one or two addresses, and the count information for all the counters being recorded.

Header Format:

The header provides information to the trace decoder on what HPM counters are being recorded and the programming of the HPM counters, as well as the type of format of the performance data. Every header begins with a full 32 bit data acquisition messages with the data value `0x70657266` (which is ('p'<<24)|('e'<<16)|('r'<<8)|('f'<<0)). The beginning value allows the trace decoder to identify a header in the performance data stream. It is possible an actual counter value will coincide with the marker header value, which would cause decoder confusion, but unlikely. This possible confusion could be removed if it was guaranteed there would only be one marker message at the beginning of the trace. Currently, it is possible to have multiple markers throughout the trace (by starting and stopping the trace multiple times).

Following the header identification word will be a 8 bit data acquisition message that contains the type of the count data (perfCount_Raw = 0, perfCount_Delta = 1, perfCount_DeltaXOR = 2).

Next, a 32 bit performance counter mask is written. 1 bits in the mask indicate that counter in that bit position will be collected. For example, if bit 5 is set, HPM counter 5 will be present.

After the performance counter mask, the definitions for how the counters are programmed, and their size and CSR address is written. For each bit in the mask that is set, the following will be written:

counter type: 32 bit write with the values 0 = OpenSBI type 0 counter (hardware general event), 1 = OpenSBI type 1 counter (hardware cache event), or 2 = OpenSBIG type 2 counter (hardware raw event).

If the counter type is 0 or 1:

code: 32 bit write. This is the code value for OpenSBI type 0 and 1 events.

If the counter type is 2:

event_data: Two 32 bit writes. The first write is the lower 32 bits, and the second write is the upper 32 bits of the event selector register. On some systems the event selector register is 32 bits; for those systems, the event_data will still be two 32 bit writes, but the second write will be 0.

Next, for all counters:

counter_info: 32 bit write. This is the lower 32 bits of the OpenSBI counter_info field. The lower 32 bits contain the  most significant bit of the counter in bits 12 - 17 (used to determine the size of the counter), and the CSR number of the counter (in bits 0 - 11). The exception is the HW_TIMESTAMP counter, which is not CSR mapped (the trace encoder timestamp is used instead). The CSR number field for the TIMESTAMP counter will be 0, but the most significant bit field will be correct. If the most significant bit field is n, then the width of the counter is n+1.

Counter definitions are written to the trace buffer from the least significant selected bit in the mask to the most significant.

The ID tag value is used to determine the size of the data (8, 16, or 32 bits), as shown below:

| ID Tag | Size of data |
| :----: | :----------: |
| 0x00000018 | 32 bits |
| 0x0000001a | 16 bits |
| 0x0000001b | 8 bits |

Tabular Representation of Header:

```
+----------------+--------+------------+--------------------------------------------------------------------+
| Message Number | Bits   | Value      | Description                                                        |
+----------------+--------+------------+--------------------------------------------------------------------+
| 1              | 32     | 0x70657266 | Magic number identifying header                                    |
+----------------+--------+------------+--------------------------------------------------------------------+
| 2              | 8      | 0 - 2      | Type of count data; 0, 1, or 2                                     |
+----------------+--------+------------+--------------------------------------------------------------------+
| 3              | 32     | Mask       | 32 bit counter mask; indicates wich HPM counters will be collected |
+----------------+--------+------------+--------------------------------------------------------------------+

```

For each counter selected in the mask, from least significant to most significant bit in the mask:

```
+----------------+--------+------------+--------------------------------------------------------------------+
| Message Number | Bits   | Value      | Description                                                        |
+----------------+--------+------------+--------------------------------------------------------------------+
| n              | 32     | 0 - 2      | OpenSBI type field for the counter                                 | 
+----------------+--------+------------+--------------------------------------------------------------------+

```

If counter type = 0 or 1 (event_data field not present):

```
+----------------+--------+--------------+------------------------------------------------------------------+
| Message Number | Bits   | Value        | Description                                                      |
+----------------+--------+--------------+------------------------------------------------------------------+
| n+1            | 32     | code         | OpenSBI code value of OpenSBI type 0 and 1 events                |
+----------------+--------+--------------+------------------------------------------------------------------+
| n+2            | 32     | counter_info | Lower 32 bits of OpenSBI counter_info.                           |
+----------------+--------+------------+--------------------------------------------------------------------+

```
Else, if counter type = 2, archsize == 32 bits (32 bit event_data, code field not present):

```
+----------------+--------+--------------+------------------------------------------------------------------+
| Message Number | Bits   | Value        | Description                                                      |
+----------------+--------+--------------+------------------------------------------------------------------+
| n+1            | 32     | event_data   | Lower 32 bits of the OpenSBI event_data field                    |
+----------------+--------+--------------+------------------------------------------------------------------+
| n+2            | 32     | counter_info | Lower 32 bits of OpenSBI counter_info.                           |
+----------------+--------+--------------+------------------------------------------------------------------+

```

Else, if counter type = 2, archsize == 64 bits (64 bit event_data, code field not present):

```
+----------------+--------+--------------+-------------------------------------------------------------------+
| Message Number | Bits   | Value        | Description                                                       |
+----------------+--------+--------------+-------------------------------------------------------------------+
| n+1            | 32     | event_dataL  | Lower 32 bits of the OpenSBI event_data field                     |
+----------------+--------+--------------+-------------------------------------------------------------------+
| n+2            | 32     | event_dataH  | Upper 32 bits of the OpenSBI event_data field                     |
+----------------+--------+--------------+-------------------------------------------------------------------+
| n+3            | 32     | counter_info | Lower 32 bits of OpenSBI counter_info.                            |
+----------------+--------+--------------+-------------------------------------------------------------------+

```

Endif

EndFor

Trace Record Format:

A trace record is written into the trace buffer each time addresses and performance counter data are recorded (timer ISR, function entry or exit, manual instrumentation - whichever has been selected). Each trace record contains a trace record type, either one or two addresses (depending on the trace record type), and 0 to 31 performance counters, depending on the number available and the number selected. To save space in the trace buffer, addresses and counters are written using the following formats:

Trace Record Type: Each trace record starts with a trace record type written as an 8 bit data acquisition message. Valid trace record types are: perfRecord_FuncEnter (0), perfRecord_FuncExit (1), perfRecord_Manual (2), and perfRecord_ISR (3). 

Addresses: One or two addresses will be written after the trace record type for each trace record. If the trace format is either perfCount_Raw (0) or perfCount_Delta (1), the entire address will be written in either 1 or two 32 bit writes. All program counter addresses will be at even addresses, which means bit 0 will always be clear. Because of this, the lower bit of the first write (the lower 32 bits of the address) is left as a 0 if the entire address fits in 32 bits (a single write), otherwise, bit 0 is set to 1 before writing it to the buffer, and then a second write of the upper 32 bits is performed. When decoding the trace and reading an address, when the first (lower) 32 bits of an address are read from the buffer, if bit 0 is set, another read of the upper 32 bits must be done.

For perfCount_DeltaXOR (2) trace format, first an xor of the last address and the new address is performed, and the result is written to the trace buffer. If the XOR result fits in 32 bits, a single write is performed. If there are bits set in the upper 32 bits, bit 0 is set before the first write, and then the upper 32 bits of the XOR result are written.

For manual and timer ISR trace records, there will be a single address in each trace record. For function entry/exit trace records, there will be two addresses in each trace record (the second immediately following the first). For manual and timer ISR trace records, the single address is the address of where execution was before the trace record was written. For entry/exit records, the first address is the address of the start of the current function; the second address is the start of the function that control is transferring to (either through a call or return).

Each address that fits in 32 bits has the format:

```
+----------------+------+-----------+--------------------------------------------------------------------+
| Message Number | Bits | Value     | Description                                                        |
+----------------+------+-----------+--------------------------------------------------------------------+
| n              | 32   | Address   | Lower 32 bits of the address (could be actual address or DeltaXOR) |
|                |      |           | Least significant bit on Address will be clear to indicate no      |
|                |      |           | upper portion of address follows                                   |
+----------------+------+-----------+--------------------------------------------------------------------+
```

If the address does not fit in 32 bits:

```
+----------------+------+-----------+--------------------------------------------------------------------+
| Message Number | Bits | Value     | Description                                                        |
+----------------+------+-----------+--------------------------------------------------------------------+
| n              | 32   | AddressL  | Lower 32 bits of the address (could be actual address or DeltaXOR) |
|                |      |           | Least significant bit in AddressL will be set to indicate upper    |
|                |      |           | portion of address follows                                         |
+----------------+------+-----------+--------------------------------------------------------------------+
| n+1            | 32   | AddressH  | Upper 32 bits of the address (could be actual address or DeltaXOR) |
+----------------+------+-----------+--------------------------------------------------------------------+
```

Note that on a 64 bit system, some addresses will fit in 32 bits, and some will need more. This means that some address portions of the trace record may have a single 32 bit write for the address, and others may be two 32 bits writes; depending if that particular address being recorded fits in 32 bits or not. If the least significant bit is set in the lower address, there will be a second write for the upper address following. Tools processing the trace should clear the lower bit in the lower address if it is set (after reading the upper portion of the address that follows the lower portion). Each address may be one or two writes.

Performance counter counts: After the address(s), the values of the performance counters being recorded (as indicated by the previous mask) will be written. Performance counters may have the least significant bit (bit 0) either set or clear, so the same algorithm as is used for addresses cannot be used. Instead if the value to be written (either the count value read (RAW), the difference between the new count value read and the previous count value read (Delta), or the XOR difference between the new count value and the previous count value (DeltaXOR)) fits in 32 bits, a single 32 bit write is performed. If instead, the value is greater than 32 bits, a second 16 bit write for bits 32 - 47 is done. This allows recording up to 48 bits for each performance counter. It is unlikely that 48 bits will not be enough, especially if doing delta recording. Performance counter information will be repeated for all counters selected in the mask, from the counter indicated by the least significant set bit in the mask, to the most significant set bit.

The table below shows the formats for counter data:

If the counter value (Raw, Delta, or DeltaXOR - whichever is being recorded) fits in 32 bits:

```
+----------------+------+-----------------+-----------------------------------------------------------------------+
| Message Number | Bits | Value           | Description                                                           |
+----------------+------+-----------------+-----------------------------------------------------------------------+
| n              | 32   | Counter Value L | Lower 32 bits of the counter value (could be Raw, Delta, or DeltaXOR) |
+----------------+------+-----------------+-----------------------------------------------------------------------+
```

If the counter value (Raw, Delta, or DeltaXOR - whichever is being recorded) is more than 32 bits:

```
+----------------+------+-----------------+-----------------------------------------------------------------------+
| Message Number | Bits | Value           | Description                                                           |
+----------------+------+-----------------+-----------------------------------------------------------------------+
| n              | 32   | Counter Value L | Lower 32 bits of the counter value (could be Raw, Delta, or DeltaXOR) |
+----------------+------+-----------------+-----------------------------------------------------------------------+
| n+1            | 16   | Counter Value H | Next 16 bits of the counter value (could be Raw, Delta, or DeltaXOR)  |
+----------------+------+-----------------+-----------------------------------------------------------------------+
```

When decoding the trace, each time the lower 32 bits of the performance counter is read, the next message in the trace is checked to see if it is a 32 bit write or a 16 bit write. If it is a 16 bit write, that message is consumed and the value is added to bits 32 - 47 of the lower 32 bits of the counter previously read. If the data is 32 bits, it is part of the next counter. If it is 8 bits, it is the start of a new trace record.

Trace records are repeated until the end of the trace data collected.

### Alternate Implementations

A dissadvantage of the current implementation is it requires modification of the application to gather performance data for; init calls must be added, calls to traceOn() and traceOff() added, and a call to writeTrace() added to finally write the collected information to a disk file.

One alternative to this would be to write a stand-alone utility that could be run before launching the appliction (or it could lanunch the appliction) that would allocate a trace buffer, program the trace engine as desired, enable reading of performance counter CSR registers in user mode, and program the event registers as desired. When the appliction being traced finishes, the stand-alone utility could be invoked to write the trace buffer to a file.

Even with such a utility, there would still be obsticals. The program being traced still needs to read the desired HPM counters where desired, and to write them and address information to the trace buffer. If doing function entry/exit instrumentation, much of that could be accomplished by the automatically called function entry/exit instrumentation routines. The function entry/exit routines would need a mechanism for determining what HPM counters to collect, as well as they would still need to map the trace engine into user space for writing to the ITC stimulus registers. The function entry/exit routines could keep an initialized flag or use a function pointer so that on the first call certain necessary initialization could be performed (such as opening the device driver and performing operations to do the mapping and get the HPM Counter list which could be written to the device driver by the stand-alone utility). Such an approach would not work for timer based data colleciton; it would still need to have a call to initialization code added to the program under trace.

Another possible approach would be to provide a custom crt0.o replacement. Such a replacement could scan the command line argument list for special arguements specifying what HPM counters to collect and how to program the Event counters. It would perforam all needed initiallization for either function entry/exit data collection or timer based data collection. Providing a customized application exit handler could then write the collected trace out to a trace file.

### Current Limitations

If using Freedom Studio while collecting a trace: Freedom Studio (and perhaps other debuggers) alter the trace encoder and funnel registers on break/resume. It is recommended you do not set any breakpoints between the calls to the performance library initialization routines until after you have executed the code you wish to collect performance data for.

When doing bare-metal tracing and using timer ISR data collection, the stack size needs to be at least 800 bytes. Check the linker script to make sure. If odd behavior is seen, try increasing the stack size further. The amount needed will depend on the actual program being traced.

When collecting a performance trace on a Linux or bare-metal system, tracing a multi-core application is not supported. If on a multi-core Linux system, the application must have its affinity set to a single core (using the Linux taskset command).

On Linux systems, multicore support is functional (but limited to a single core), but mutli-cluster is currently not supported (more than one funnel).

On Linux systems, the timer ISR method is not currently supported. Users can add one if needed.

On bare-metal, the `perfWriteTrace()` function is not supported; the debugging/trace tools being used should read the trace (either SRAM or SBA).

The bare-metal version does not support OpenSBI type 0 (Hardware general events), except for a few (see the sifive_bare_perf.h file) and type 1 (Hardware cache events). Use raw events instead (type 2).

Only processors with trace encoders that have support for the ICT Control Message CKSRC = 0, CKDF = 0 are known to work for collecting decodable traces. This is because the Control 0, 0 message is used to insert an uncompressed timestamp into the trace buffer using an ICT message. Without an initial uncompressed timestamp, timestamp values cannot be correctly decoded. Processors with support for event trace should have the needed ICT Control Message.

The SRAM trace buffers on processors have limited size and may restrict the amount of data that can be collected. If available, the SBA sink may be used to collect larger amounts of performance data. Using an SBA sink will require modifications to the init routines in the SiFive perf library for programming the trace engine.

### Example Program

Below is a complete example of a simple program that recursively computes a Fibonacci number. The program has the necessary calls described above to collect function entry/exit performance data on a Linux target. The bare-metal version is similar. This Linux example assumes the necessary trace-perf device driver has been previously loaded. See the trace-perf documentation for instructions on how to build and load the device driver (TBD).

```
#include <stdio.h>
#include <stdlib.h>

#include "sifive_linux_trace.h"
#include "sifive_linux_perf.h"

#define traceBaseAddress 0x10000000
#define tfBaseAddress	0x10018000

// create the trace memory map object
struct TraceRegMemMap volatile * tmm[] = {(struct TraceRegMemMap*)traceBaseAddress,(struct TraceRegMemMap*)(traceBaseAddress+0x1000)};

// create the trace funnel memory map object
struct TfTraceRegMemMap volatile * fmm = (struct TfTraceRegMemMap*)tfBaseAddress;

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
        {
                .type = perfEventHWGeneral,
                .code = HW_CACHE_MISSES,
                .event_data = 0
        },
        {
                .type = perfEventHWGeneral,
                .code = HW_BRANCH_INSTRUCTIONS,
                .event_data = 0
        },
        {
                .type = perfEventHWGeneral,
                .code = HW_BRANCH_MISSES,
                .event_data = 0
        },
        {
                .type = perfEventHWRaw,
                .code = 0,
                .event_data = (2 << 16) | (0 << 0)
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

  rc = perfInit(sizeof tmm / sizeof tmm[0],1);
  if (rc != 0) {
    printf("perfInit() failed. Exiting\n");

    return rc;
  }

  rc = perfFuncEntryExitInit(perfCntrList,sizeof perfCntrList/sizeof perfCntrList[0],6,perfCount_Delta,32*1024);

  if (rc != 0) {
    printf("perfFuncEntryExitInit(): failed\n");

    return rc;
  }

  perfTraceOn();

  unsigned long i = 20;

  f = fib(i);

  printf("fib(%lu) = %lu\n",i,f);
  
  perfTraceOff();

  perfWriteTrace(NULL);

  return 0;
}
```

The program should be compiled with the `-finstrument-functions` compiler switch, and linked with `sifve_linux_perf.o`. The `sifive_linux_perf.c` program does not need to be compiled with the `-finstrument functions`.  For example, on a Linux system:

```
cc -c -finstrument-functions fib.c
cc -c sifive_linux_perf.c
cc -static -o fib.elf fib.o sifive_lionux_perf.o
```

On Linux, the `-static` sitch is necessary for the SiFive trace decoder to correctly resoved function addresses, otherwise it is not needed.

To execute the program on a multi-core Linux system, use the `taskset` command to restrict tracing to a single core, as in:

```
taskset 2 ./fib.elf
```

The taskset command above will confine program execution to core 1 (starting from core 0).

The program will execute and collect performance data in a 32K byte SBA trace buffer. After collection, the data will be written to the file trace.rtd in the current working directory.
