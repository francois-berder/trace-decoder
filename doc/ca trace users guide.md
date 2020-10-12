# CA Trace Users Guide

This document describes cycle accurate tracing; the steps to collect and process a cycle accurate trace and how to interpret the cycle information. Not covered is how to use Freedom Studio to collect and process cycle accurate traces. See the Freedom Studio documentation for complete information on collecting and analyzing using only Freedom Studio. The method described here will be a combination of using Freedom studio to run your programs, putty to collect the traces, and the stand along dqr.exe program to process the traces.

CA Trace provide cycle accurate trace of code. Each instructions object provides the cycle time from the begining of the ca trace, and the cycle count delta from the previously retired instruction. Collecting CA traces requries some extra steps over normal tracing.

### CA Trace Collection

To collect a cycle accurate trace, you must first have an implementation of the processor that includes cycle accurate trace support.

After that, you must instrument your program under trace to enable cycle accurate trace. This step is necessary because when collecting a cycle accruate trace, there will be two trace buffers. The first is the normal instruction trace buffer. The instrcution trace information is the same as a normal non-ca trace. The second trace buffer contains cycle accurate trace information. When processing the traces, both the ca trace and normal instruction trace are used, and must be syncronized by the trace decoder to get accurate cycle information.

Instructmenting the program is necessary because the normal debug startup process creates messages in the instruction trace that makes it difficult for the trace decoder to syncronize the two traces (as it exits and enters debug mode multiple times when resuming from breakpoints).

A header file is provided (sifive_trace.h) that contains macros for starting and stopping the trace/CA trace data generation. They are *_caTraceOnDefaults(), _caTraceOnHTMDefaults(), _caTraceOnBTMDefaults(), _caTraceOn(),* and *_catTraceOff()*; each is described below:

**_caTraceOnDefaults():** Enable CA tracing. Auitomatically selects either HTM mode for BTM mode, depending on what is supported by the hardware. Uses default values for teInstrumentation, teStallEnable, teStopOnWrap, teInhibitSrc, teSyncMaxBTM, teSyncMaxInst, teSink, and caStopOnWrap.

**_caTraceOnHTMDefaults():** Enable CA tracing using HTM mode. Uses default values for teInstrumentation, teStallEnable, teStopOnWrap, teInhibitSrc, teSyncMaxBTM, teSyncMaxInst, teSink, and caStopOnWrap.

**_caTraceOnBTMDefaults():** Enable CA tracing using BTM mode. Uses default values for teInstrumentation, teStallEnable, teStopOnWrap, teInhibitSrc, teSyncMaxBTM, teSyncMaxInst, teSink, and caStopOnWrap.

**_caTraceOn(teInstruction,teInstrumentation,teStallEnable,teStopOnWrap,teInhibitSrc,teSyncMaxBTM,teSyncMaxInst,teSink,caStopOnWrap):** Enable CA tracing, specifying all configuration parameters. User must specify values for teInstrumentation,  teStallEnable, teStopOnWrap, teInhibitSrc, teSyncMaxBTM, teSyncMaxInst, teSink, caStopOnwrap. Macros are defined in sifive_trace.h to use for the arguments.

**_caTraceOff():** Disable CA tracing and wait for trace information to flush to buffers.

To use, add a line to your program you wish to trace that includes the sifive_trace.h file, another that calls one of the _caTraceOn macros, and one that calls _caTraceOff() to turn tracing off, such as:

	#incldue <stdio.h>
	#include "sifive_trace.h"

	#define traceBaseAddress 0x10000000
	#define caBaseAddress 0x1000f000

	main()
	{
		_caTraceOnDefaults();
		printf("hello world!\n");
		_caTraceOff();
	}

Make sure traceBaseAddress and caBaseAddress are the correct addresses for your processor implementation. Consult the dts file for your processor to make sure. The traceBaseAddress and caBaseAddress defines are used by the all _caTraceOn and _caTraceOff() macros.

When executed, this will start all trace collection at the _caTraceOnDefaults() point and halt it at the _caTraceOff() point. The call to _caTraceOff() insures the trace data is flushed to the buffers, otherwise the last part of the trace may not be.

The _caTraceOnDefaults() macro selects the SRAM buffers for both trace buffers. If supported by your processes and you would like to write to different butters, the _caTraceOnDefaults() macro can be modified. The write to the ca_control_offset and te_control_offset should be modified to select the appropriate synk buffers.

After collecting the CA and Instruction traces, they will need to be written to files. The next section describes how to do that.

### Capturing the Trace Buffers to Files

After a set of traces has been collecting and are sitting in the trace buffers, they will need to be written to files to be processed by the trace decoder. To do that, you will need to telnet into the OpenOCD server before terminating the debug session the buffers were collectded for. A good way to do that is to set a breakpoint after the _caTraceOff() macro. When execution reaches that breakpoint, telnet to localhost, port 4444; this is the OpenOCD server. PuTTY is a good utility on Windows to connect to the OpenOCD server with. After connected and you have a OpenOCD command window, you can write the instruction trace and ca trace buffers using the wtb and wcab commands. By default, the wtb command will write to file trace.rtd and the wcab command will write to file trace.cat. These files will be in the project folder of the application under trace.

### Using the Trace Decoder Utility

A new command line switch was added to the trace decoder to specify the cycle accurate trace file; -ca filename. When decoding a cycle accurate trace, you must also specify an instruction trace file and an elf file, as in:

dqr -t trace.rtd -ca trace.cat -e program.elf -trace -src -file -func -branches -callreturn

If the files are not in the current working directory, you should specify the path as part of their name.

### Interpreting the Output of the Trace Decoder Utility

When decoding a cycle accurate trace, additional cycle information is provided in the output for each instruction. The new information includes the CA Trace cycle clock each instruction was retired on, and how long it has been since the last instruction that was retired.  For example, consider the output:

	Trace: Msg # 5, NxtAddr: 800024bc, TCode: INCIRCUITTRACE (34) ICT Reason: External Trigger (8) U-ADDR: 0x00000356 <rasterize>
	t:25 [25]       8000045a:         1141               addi	sp,sp,-16
	t:67 [42]       8000045c:         c606               sw	ra,12(sp)
	t:68 [1]        8000045e:         c422               sw	s0,8(sp)
	t:69 [1]        80000460:         c24a               sw	s2,4(sp)
	t:70 [1]        80000462:         c04e               sw	s3,0(sp)
	t:70 [0]        80000464:         0800               addi	s0,sp,16

The number follwing the "t:" is the CA Trace cycle clock. This trace starts on clock 25. The number in the following "[]" is the number of instructions since the last instruction was retired. At T=25, it is 25 because it is the first instruciton retired in the CA trace. The next instruction has a count of 42, wich may seem long, but this program was executing out of flash and cach misses are expensive. The next three instruction have a count of 1, which is expected. The last instruciton at T=70 has a cycle count of 0. This is because this is a dual pipeline core, and two instructions were retired at time T=70. When two instructions are retired on the same cycle, the second instruction will always have a count of 0, and will always be from the second pipeline.

### Tips for Collecting CA Traces

There are two trace buffers when collecting ca traces; the normal instruction trace buffer and the ca trace buffer. These buffers fill at different rates, and because of the differences in volumes of data written to each, the ca trace buffer will likely fill first. The _caTraceOnDefaults() macro enables stop-on-wrap for both buffers. If stop-on-wrap is off, and either of the buffers wraps, the trace decoder will not be able to synctronize the two trace buffers.


