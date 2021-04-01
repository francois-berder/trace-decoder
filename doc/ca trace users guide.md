# CA Scalar/CA Vector Trace User's Guide

This document describes both scalar and vector cycle accurate tracing; the steps to collect and process a cycle accurate trace and how to interpret the cycle information. Both collecting the trace with Freedom Studio and collecting the trace using more manual methods are described. See the Freedom Studio documentation for complete information on collecting and analyzing using only Freedom Studio. The method described here will be a combination of using Freedom Studio to run your programs and collect the traces, and using Freedom Studio to run your program but use putty to collect the traces. Also covered is using the stand-alone dqr command line program to process the traces.

CA Trace provides cycle accurate trace of code. For scalar cycle accurate tracing, each instruction object provides the cycle time from the beginning of the CA trace, and the cycle count delta from the previously retired instruction to when the instruction was finished by the scalar pipeline (either pipeline 0 or 1). When doing vector cycle accurate tracing, each instruction object also includes the relative cycle from the previous instruction scalar pipeline retirement to when the vector instruction was started by the vector, and when it was finished by the vector unit.

Collecting CA traces requires some extra steps over normal tracing. To collect a cycle accurate trace, you must first have an implementation of the processor that includes cycle accurate trace support (either CA Scalar, and CA Vector). Two separate trace files will be collected for all CA traces - trace.rtd and trace.rcad. The trace.rtd file contains normal instruction trace data. The trace.rcad file is the cycle accurate information, and is used to augment the instruction trace data to provide the cycle information. When decoding a cycle accurate trace, the decoder must be able to synchronize the two files, so the cycle information is in sync with the instruction information. Synchronizing the two trace files can be complex. To make it easier for the trace decoder to synchronize the two files, the program to be traced must be modified to perform the enabling of the trace data collection instead of relying on Freedom Studio to modify the trace control registers. The problem with letting Freedom Studio enable the trace control is that extra trace messages are inserted into the instruction trace, making it harder for the trace decoder to synchronize the instruction and cycle accurate trace information.

### Instrumenting the Program Under Trace

Before collecting trace data, the program to trace must be modified to enable tracing. Instrumenting the program is necessary because the normal debug startup process creates messages in the instruction trace that makes it difficult for the trace decoder to synchronize the two traces (as it exits and enters debug mode multiple times when resuming from breakpoints).

A header file is provided (sifive_trace.h) that contains macros for starting and stopping the trace/CA trace data generation. They are *_caTraceOnDefaults(), _caTraceOnHTMDefaults(), _caTraceOnBTMDefaults(), _caTraceOn(),* and *_catTraceOff()*; each is described below:

**_caTraceOnDefaults():** Enable CA tracing. Automatically selects either HTM mode or BTM mode, depending on what is supported by the hardware. Uses default values for teInstrumentation, teStallEnable, teStopOnWrap, teInhibitSrc, teSyncMaxBTM, teSyncMaxInst, teSink, and caStopOnWrap.

**_caTraceOnHTMDefaults():** Enable CA tracing using HTM mode. Uses default values for teInstrumentation, teStallEnable, teStopOnWrap, teInhibitSrc, teSyncMaxBTM, teSyncMaxInst, teSink, and caStopOnWrap.

**_caTraceOnBTMDefaults():** Enable CA tracing using BTM mode. Uses default values for teInstrumentation, teStallEnable, teStopOnWrap, teInhibitSrc, teSyncMaxBTM, teSyncMaxInst, teSink, and caStopOnWrap.

**_caTraceOn(teInstruction,teInstrumentation,teStallEnable,teStopOnWrap,teInhibitSrc,teSyncMaxBTM,teSyncMaxInst,teSink,caStopOnWrap):** Enable CA tracing, specifying all configuration parameters. User must specify values for teInstrumentation,  teStallEnable, teStopOnWrap, teInhibitSrc, teSyncMaxBTM, teSyncMaxInst, teSink, caStopOnwrap. Macros are defined in sifive_trace.h to use for the arguments.

**_caTraceOff():** Disable CA tracing and wait for trace information to flush to buffers.

It does not matter if the processor supports CA Scalar or CA Vector tracing; these functions will work for either. All macros only work for core 0. If the target is multicore, only core 0 will be instrumented unless the macros are modified.

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

Make sure traceBaseAddress and caBaseAddress are the correct addresses for your processor implementation. Consult the dts file for your processor to make sure. The traceBaseAddress and caBaseAddress defines are used by all the On/Off macros.

In the example above, When _caTraceOnDefaults() is executed, this will start all trace collection at the _caTraceOnDefaults() point and halt it at the _caTraceOff() point. The call to _caTraceOff() ensures the trace data is flushed to the buffers, otherwise the last part of the trace may not be written to the buffer.

The _caTraceOnDefaults() macro selects the SRAM buffers for both trace buffers. If supported by your processes and you would like to write to a SBA memory buffer, the _caTraceOnDefaults() macro can be modified. The write to the ca_control_offset and te_control_offset should be modified to select the appropriate sink buffers.

### CA Trace Collection Using Freedom Studio

Freedom Studio is capable of collecting the trace data and writing it to files. To use it, make sure your program has been instrumented to enable tracing.

Make sure the button in the trace viewer to collect the trace data/files has been selected (it should be blue). Whenever the processor hits a breakpoint, the trace data will be collected and written to files. When doing CA Instruction or CA Vector traces, two files will be written in the projects home directory; trace.rtd and trace.rcad. The trace.rtd file is the normal instruction trace file, and trace.rcad contains the CA trace data. Both files are needed to perform a CA trace decode. Previous trace data is overwritten each time the data is collected.

Now the trace data can be decoded using the dqr command line utility.

### CA Trace Collection Using Manual Methods

Sometimes it is desirable to manually collect the trace data. If so, instrument the program as described above, and run the program under Freedom Studio. After a set of traces has been collected and are sitting in the trace buffers, they will need to be written to files to be processed by the trace decoder. To do that, you will need to telnet into the OpenOCD server before terminating the debug session the trace was collected for. A good way to do that is to set a breakpoint after the _caTraceOff() macro. When execution reaches that breakpoint, telnet to localhost, port 4444; this is the OpenOCD server. PuTTY is a good utility on Windows to connect to the OpenOCD server. After connecting and you have a OpenOCD command window, you can write the instruction trace and CA trace buffers using the wtb and wcab commands. By default, the wtb command will write to file trace.rtd and the wcab command will write to file trace.cat. These files will be in the project folder of the application under trace.

Now the trace data can be decoded using the dqr command line utility.

### Using the Trace Decoder Utility

Besides the normal switches that are used when performing an instruction trace decode, there are two additional switches that must be used when decoding a CA trace; the -ca and -catype switches. The -ca switch is used to specify a CA trace file. The -catype is used to specify either a CA instruction trace, or a CA Vector trace. When doing a CA trace decoder, the instruction trace file (-t switch) and the elf file (-e switch) must also be specified. Below is an example command line use of the dqr to decode a CA Vector trace:

dqr -t trace.rtd -ca trace.cat -catype instruction -e program.elf -trace -src -file -func -branches -callreturn

If the files are not in the current working directory, you should specify the path as part of their name. All output is sent to stdout.

### Interpreting the Output of the Trace Decoder Utility

When decoding a cycle accurate trace, additional cycle information is provided in the output for each instruction. The new information includes the CA Trace cycle clock each instruction was retired on, and how long it has been since the last instruction that was retired.  For example, consider the output:

	Trace: Msg # 5, NxtAddr: 800024bc, TCode: INCIRCUITTRACE (34) ICT Reason: External Trigger (8) U-ADDR: 0x00000356 <rasterize>
	t:25 [25]       8000045a:         1141               addi	sp,sp,-16
	t:67 [42]       8000045c:         c606               sw	ra,12(sp)
	t:68 [1]        8000045e:         c422               sw	s0,8(sp)
	t:69 [1]        80000460:         c24a               sw	s2,4(sp)
	t:70 [1]        80000462:         c04e               sw	s3,0(sp)
	t:70 [0]        80000464:         0800               addi	s0,sp,16

The number following the "t:" is the CA Trace cycle clock. This trace starts on clock 25. The number in the following "[]" is the number of instructions since the last instruction was retired. At T=25, it is 25 because it is the first instruction retired in the CA trace. The next instruction has a count of 42, which may seem long, but this program was executing out of flash and cache misses are expensive. The next three instructions have a count of 1, which is expected. The last instruction at T=70 has a cycle count of 0. This is because this is a dual pipeline core, and two instructions were retired at time T=70. When two instructions are retired on the same cycle, the second instruction will always have a count of 0, and will always be from the second pipeline.

The trace example above is for an instruction CA trace. When decoding a vector CA trace, additional information will be displayed for vector instructions. It will look something like:

	File: C:/workspaces/wscavect/viu75_ca_vector_trace_gemm/src/sgemm.S:1777
	Source:     vfmacc.vf v16, fs9, v8
	t:2400 [1:0-58-62]     0000000080002f7c:  b28cd857           vfmacc.vf	v16,fs9,v8

	File: C:/workspaces/wscavect/viu75_ca_vector_trace_gemm/src/sgemm.S:1778
	Source:     blt s10, s6, cblas_sgemm_rrr_0__loop_kk_0
	t:2401 [0:1]      0000000080002f80:  f96d40e3           blt	s10,s6,80002f00 <cblas_sgemm_rrr_0__loop_kk_0> [t]

	Trace: Msg # 90, NxtAddr: 80002f00, TCode: DIRECT BRANCH (3) I-CNT: 66

	File: C:/workspaces/wscavect/viu75_ca_vector_trace_gemm/src/sgemm.S:1738
	Source:     vfmacc.vf v28, fa6, v12
	<cblas_sgemm_rrr_0__loop_kk_0>
	t:2403 [0:2-61-65]     0000000080002f00:  b2c85e57           vfmacc.vf	v28,fa6,v12

In the example above, there are two vector instructions and one scalar instruction. Scalar instruction times are displayed the same as a normal CA scalar trace. The vector instructions have additional information, showing the number of cycles in the scalar pipeline, the cycles queued after the scalar pipeline while waiting to begin in the vector unit, and the cycles to complete the vector unit. For example, the line:

	t:2403 [0:2-61-65]     0000000080002f00:  b2c85e57           vfmacc.vf	v28,fa6,v12

The vfmacc.vf instruction was processed by the scalar 0 pipeline and finished the scalar pipeline 2 cycles after the previous instruction completed the scalar pipeline. 61 cycles after the previous instruction finished the scalar pipeline, the vfmacc.vf instruction began execution in the vector unit. 65 cycles after the previous instruction finished the scalar pipeline, the vfmacc.vf instruction finished the vector unit.

### Tips for Collecting CA Traces

There are two trace buffers when collecting CA traces; the normal instruction trace buffer and the CA trace buffer. These buffers fill at different rates, and because of the differences in volumes of data written to each, the CA trace buffer will likely fill first. The _caTraceOnDefaults() macro enables stop-on-wrap for both buffers. If stop-on-wrap is off, and either of the buffers wraps, the trace decoder will not be able to synchronize the two trace buffers.


