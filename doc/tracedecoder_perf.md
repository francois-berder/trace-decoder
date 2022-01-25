# Processing Performance Data with the Trace Decoder

### Introduction

The SiFive trace decoder has the capability to extract performance data written to a trace using the sifive_perf library. The extracted data is written to textual files, which can then be processed with Freedom Studio/Trace Compass.

This document describes how to use the trace decoder to extract the performance data, and the format of the textual files created. See the sifive_perf library documentation to see how to collect this information, the manual for the processor being traced to see what data can be collected in HPM counters, and how to configure the HPM counter registers, and the Freedom Studio documentation on how to process the extracted performance data.

### Textual Performance Data

The sifive_perf library will embed selected performance data into the trace using writes to ITC stimulus registers, which will create data acquisition messages in the trace. All information in the trace is in raw binary format. The trace decoder can identify performance data and extract it, converting it into a set of textual files. The files generated will depend on what data has been collected.

The SiFive Perf Library allows collecting address and timestamp information, as well as up to 32 additional HPM performance counter registers. Address/timestamp information is always collected. The programmer chooses which performance counters to also collect.

When extracting performance data from a trace with the trace decoder, a text file will be created in the same folder as the trace file (.rtd file) with the same base name as the elf file, and the extension \`.perf'. This file will contain all data collected. Additionally, a folder in the same directory as the trace file named \`perf' will be created, and it will be populated with files; one for each type of performance data found in the trace file. The base name for each file in the perf folder will be the same as the elf file, and each file will have an extension that identifies what type of performance data it contains. A list of all possible extensions and the performance data they contain is below.

| Extension | Description |
| :-------- | :---------- |
| address   | Address and timestamp information when using the manual or timer ISR based performance data collection |
| callret   | Function entry/exit information. Includes function address and where called from for both function entry and exit. Created if the trace was created using function entry/exit instrumentation |
| perfcoutnerN | Values of the HPM register N, where N is 0 - 31. Counters 0 - 2 are fixed function. Counters 3 - 31 can be programmed. The programming of the event registers for each counter collected is also provided |

In addition to the performance counter information, there will be information in each file specifying the path to the elf file used to generate the trace and how the performance counter was programmed. The aggregate performance data file will also contain a bit mask specifying what performance counters were collected. The bit mask is 32 bits, and each non-zero bit specifies that performance counter was collected in the trace.

### Example Performance Text File

The snippet below is from an aggregate performance data file:

```
# ELFPATH=src\debug\hello.elf
[0] 9537 [Perf Cntr Mask] [Mask=0x00000003]
[0] 9628 PC=0x80002338 [Address]
[0] 9938 PC=0x80002338 [Perf Cntr] [Index=0] [Value=7790] 
[0] 12695 PC=0x80002338 [Perf Cntr] [Index=1] [Value=1] 
[0] 12695 PC=0x800000c4 [Address] fl:C:/workspaces/ws-narwhal/u74_trace_all_f5f20fc_perftest/freedom-metal/src/scrub.S:66
[0] 12839 PC=0x800000c4 [Perf Cntr] [Index=0] [Value=10722]  fl:C:/workspaces/ws-narwhal/u74_trace_all_f5f20fc_perftest/freedom-metal/src/scrub.S:66
[0] 15190 PC=0x800000c4 [Perf Cntr] [Index=1] [Value=1]  fl:C:/workspaces/ws-narwhal/u74_trace_all_f5f20fc_perftest/freedom-metal/src/scrub.S:66
[0] 15190 PC=0x800027a4 [Address]
[0] 15327 PC=0x800027a4 [Perf Cntr] [Index=0] [Value=13210] 
[0] 18189 PC=0x800027a4 [Perf Cntr] [Index=1] [Value=1] 
[0] 18189 PC=0x800024dc [Address]
[0] 18326 PC=0x800024dc [Perf Cntr] [Index=0] [Value=16209] 
[0] 21177 PC=0x800024dc [Perf Cntr] [Index=1] [Value=1] 
[0] 21177 PC=0x800077e2 [Address] fl:C:/workspaces/ws-narwall/u74_trace_all_f5f20fc_perftest/bsp/build/debug/metal/machine.h:932
[0] 21314 PC=0x800077e2 [Perf Cntr] [Index=0] [Value=19197]  fl:C:/workspaces/ws-narwall/u74_trace_all_f5f20fc_perftest/bsp/build/debug/metal/machine.h:932
[0] 24173 PC=0x800077e2 [Perf Cntr] [Index=1] [Value=1]  fl:C:/workspaces/ws-narwall/u74_trace_all_f5f20fc_perftest/bsp/build/debug/metal/machine.h:932
```

The first line gives the path and name of the elf file used. The second line gives the mask for which registers were collected.

For all HPM counters greater than 2, how the performance counter was programmed will also be shown. HPM counters 0, 1, and 2 are fixed function and cannot be programmed, so their definitions are not embedded in the textual performance files. For information on programming the HPM performance counters, and what the definitions mean, see the manual for the processor implementation being traced.

Each line after the elf file name/path starts with the core number the data was collected for. The second field is the trace timestamp generated by the trace encoder for when the message was written to the trace buffer. If there is an instruction pointer associated with the data for that line, it will be the next field. The following field specifies what kind of data is being reported on that line. The meaning are:

| Field Name | Description |
| :--------- | :---------- |
| [Perf_Cntr_Mask] | The mask specifying which HPM performance counters were collected |
| [Perf Cntr Def] | The programming of the specified HPM performance counter|
| [Address] | The address associated with the timestamp and any additional data |
| [Func Enter at 0xaddress] | Function entry instrumentation. Has function address and address of where it was called from |
| [Func Exit] | Function exit instrumentation. Has address of function, and address of where it was called from |
| [Perf Cntr]| HPM performance counter information is being reported. The [Index=n] field specifies which counter the data is for. The [Value=nnnn] field gives the value of the performance counter in decimal. All counts are absolute; they are not relative to the last time that performance counter value was given |

All lines with an associated address that can be resolved to a source file and line by the trace decoder will end with fl:sourcefile:sourceline information. Additional information (such as the index of the performance counter) may be present for some types of lines.

The individual performance counter text files will have a subset of the information; that which only pertains to that HPM performance counters.

### Trace File Format for Performance Data

The binary format for the performance data embedded in the trace file can be found in the Sifive Perf Library documentation.

### Usage:

To enable HPM performance counter data extraction from the trace, the following lines must be added to the properties file (for information on the properties file and its usage, see the trace decoder information in README.md).

```
trace.config.int.itc.perf=[true | false | 1 | 0]
```

This flag enables or disables performance data extraction. Default value is false if not present in the properties file.

```
trace.config.int.itc.perf.channel=6
```

This flag specifies which ITC channel is being used for embedded performance data. Legal values are 0 - 31. The default value is 6. It must coincide with that channel that was specified in the program being traced when the data was collected.

If a trace decoder properties file enables performance data extraction, and ITC performance data is found on the channel specified, textual files as described above will be created.

### Example Usage

Below is an example usage of the trace decoder to extract performance data. Assuming an elf file named hello.elf, a trace file named trace.rtd with performance data for HPM counters 0 and 1, and a properties file that contains:

```
elf=src/debug/hello.elf
rtd=trace.rtd
trace.config.int.itc.perf=true
trace.config.int.itc.perf.channel=6
```

The command:

```
dqr -pf properties.pf
```

would generate the aggregate textual performance data file hello.perf, and the folder perf containing the text files hello.address, hello.perfchannel0, and hello.perfchannel1.
