
## ITC Print

ITC Print is the ability to send printed output to the ITC 0 stimulus register, capturing all output in the trace message stream where it can be decoded by the trace-decoder and displayed as printed output when the trace is decoded either in Freedom Studio, or on the command line.

There are two mechanisms available to capture printed text in the trace message stream; redirected stdio and custom itcprintf() and itcputs() functions. Both cause writes to the ITC 0 stimulus register which generates either Auxiliary Access Write trace message or Data Acquisition trace messages messages to be generated (depending on the version of the Risk-V processor). Additional detail for both is given below.

#### Using the trace-decoder to Display ITC Print Data

By default the trace-decoder will not recognize Aux Access Write messages or Data Acquisition messages as ITC print messages. To enable the recognition of ITC print data, the -itcprint flag must be given on the command line, as in:

`> dqr -t sort.rtd -e sort.elf -trace -itcprint`

This will cause both Aux Access Write messages and Data Acquisition messages to be recognized as ITC print data and displayed as such. Each itc print data line output by the trace-decoder will be prefixed with "ITC PRINT: "

#### Custom itcprintf()/itcputs() Function

One method of sending printed output to the ITC 0 stimulus register is to use custom itcprintf() and itcputs() functions. These functions take the normal printf() and puts() argument list but sends all output to the ITC 0 stimulus register. Example itcprintf() and itcputs() are in the examples directory. Add them to your project either by cutting and pasting the source to your source, or copying the files into your project and adding them to the project.

For example, if your program had a call to itcprintf() as in:

`itcprintf("Hello world!\n");`

and you captured the trace and decoded it with the command line:

`> dqr -t helloworld.rtd -e helloworld.elf -trace -itcprint`

Among all the other trace output from dqr would be the line:

`> ITC Print: Hello world!`

#### Redirected stdio

The other mechanism for getting print data in the trace message stream is to redirect all stdio output to the ITC 0 stimulus registers. To do that, you must have a bsp for your processor that was built with redirected stdout.

To see if your bsp has stdout redirected, you can check the design.dst file in the bsp directory for the project processor. There should be a chosen record in the design.dts file, something like:

```
L18: chosen {
metal,entry = <&L7 0x400000>:
stdout-path="/soc/trace-encoder-0@20007000:115200";
};
```

If the stdout-path is set to anything else, or the chosen record is not present, stdout is not redirected to the ITC 0 stimulus registers. It is not sufficient to just change the stdout-path entry to the trace-encoder; the entire BSP must be rebuilt.

### Building a BSP with redirected stdout

You can build a custom BSP for your target with stdout redirected to the ITC 0 stimulus register. This will cause all output sent to stdout to be sent to the ITC 0 stimulus register and captured in the trace messages. No stdout output will be sent to the UART.

#### Prerequisites

To build a custom BSP you will need the following:

```
Risc-V cross-compiler tools
Compiler tools for host machine
git
freedom-e-sdk
freedom-devicetree-tools
freedom-metal
design.dts file for mcs or bit file being used
```

The procedure outlined below has been tested on Linux, but should work on Windows and Mac OS X as well.

The easiest way to get the cross-compiler tools is to install Freedom Studio. It can be downloaded from https://www.sifive.com/boards. Windows, Linux, and Mac OS X are supported. Download and install according to the instructions. This will get you the Freedom Studio IDE which includes the Risc-V cross-compiler tools needed to build a custom BSP.

To get the compiler tools for the host machine, download the gnu C compiler and gnu binutils. There is much information about installing the tools on the internet. If installing on a Windows environment, use the 64 bit minGW and msys tools. Again, search the internet for instructions.

To get the freedom-e-sdk, freedom-devicetree-tools, and freedom-metal packages, you will need to install git on your system.  There are multiple ways to do that. On a Windows system, you could install msys and minGW (64 bit version). On Linux or Mac OS X, you could just install git. There is information available on the web for installing for the various platforms.

After installing git, the easiest way to get the freedom-e-sdk, freedom-devicetree-tools, and freedom-metal packages is to use the command:

```
> git clone --recursive https://github.com/sifive/freedom-e-sdk/tree/master
```

Instead of downloading the ```master,``` you could specify a branch, such as ```v201908-branch``` or a newer branch. The ```v201908-branch``` is the first branch with support for redirecting stdio to the ITC 0 stimulus register. Specifying the ```--recursive``` switch will also download the freedom-devicetree-tools and freedom-metal packages and locate them in the freedom-e-sdk where building will be easier.

All paths below will be relative to the freedom-e-sdk directory. So first do:

```
> cd <path-to-freedom-e-sdk-folder>/freedom-e-sdk
```

Where ```<path-to-freedom-e-sdk-folder>``` is wherever the freedom-e-sdk folder was created by the git command above.

Make sure the native host system compilers and cross-compiler tools are in your path. On Linux you can add them to your path with the following command:

```
> export PATH=<path-to-host-tools>:$PATH
> export PATH=<path-to-cross-tools>:$PATH
```

#### Building the custom BSP

The first step to building the custom BSP is to build the device tree tools. This step builds the executables that run on the host system and are used to build the custom BSP.

From the command line, execute:

```
> cd freedom-devicetree-tools
> ./configure
```

You should see output from configure indicating how things are being configured. You should not see any errors. If you do see an error, it must be resolved before proceeding.

On Linux, you may need to run the ```./configure`` as root. To do that, instead of ```./configure,``` execute:

```
> sudo ./configure
```

When prompted for you password, enter it.

After running configure as root, you will need to change the ownership of the created files back to you. To do that:

```
> chown -R -v <your-user-name> *
```

After configuring with no errors, from the freedom-devicetree-tools directory execute:

```
> make
```

This will build the device-tree executables for your system. After the device-tree executables are built, add them to your path using (Linux example):

```
> export PATH=<path-to-freedom-e-sdk>/freedom-e-sdk/freedom-devicetree-tools/:$PATH
```

#### Building the Freedom-E-SDK

This step will build the custom BSP with redirected stdout for your target. To do this, execute from inside the freedom-e-sdk directory:

```
> cd bsp
> mkdir <new-target-bsp>
```

Where ```<new-target-bsp>``` is whatever name you want to give to the bsp you are going to create.

Next, 

```
> cp <path-to-dts-file>/design.dts <path-to-freedom-e-sdk>/freedom-e-sdk/bsp/<new-target-bsp>
> cd <new-target-bsp>
```

Now edit the design.dts file to enable the redirection. Towards the top of the design.dts file there should be a chosen record, something like

```
chosen {
stdout-path = "/soc/serial@10013000:115200";
metal,entry = <&L7 0x400000>;
};
```

The stdout-path line will need to be changed to:

```
stdout-path="/soc/trace-encoder-0@20007000:115200";
```

The address after the ```@``` is the base address for the device, and should match that for the trace-encoder found elsewhere in the file. The number after the ```:``` is the baud rate. It is not needed for the trace-encoder device, but is needed for the design.dts file to be parsed correctly.

If your dts file does not have a chosen record, go ahead and build the bsp using the update-targets.sh script as directed below. This will, among other things, create a chosen record that you can edit. After building the bsp, go back and edit the chosen record and rebuild the bsp. Your dts file will not have a chosen record if it has never been used to build a bsp, such as when they are distributed with a mcs or bit file.

If the dts file does not have a trace-encoder record, it does not support redirection of stdout to the ITC, and redirection cannot be done.

After editing the file, execute the commands:

```
> cd <path-to-freedom-e-sdk>/bsp
> ./update-targets.sh --target-name <new-target_bsp> --sdk-path=./../ --target-dts=./<new-target-bsp>/design.dts --target-type arty
```

At this point you should have a new custom BSP. To use it with Freedom Studio, you will need to configure Freedom Studio.

#### Configuring Freedom Studio to use the new BSP

Fire up Freedom Studio, and perform the following steps:

```
Select the SiFiveTools button at the top of the screen

Select Use another Freedom E SDK

Set you Freedom E SDK Path to <path-to-freedom-e-sdk>/freedom-e-sdk

Select Apply and Close
```

Now you can create a project that uses the new BSP. In Freedom Studio:

```
Select the SiFiveTools button at the top of the screen

Select New Freedom E SDK Project

Under Select Target, select your new BSP

Select Example Program, such as hello

Select Finish
```

For the newly created project, all output sent to stdout should now be redirected to the ITC 0 stimulus register and collected in the trace output.

#### Limitations

The example code for itcprintf() inherits any limitations imposed on the normal printf() function because it relies on vsnprintf() to do the conversion from the argument list to text. The vsnprintf()/printf() functions do not support floating point or 64 bit integer display.

#### Multi-Core and ITC Printing

Currently both mechanisms to capture print data in the trace message stream print only to core 0's ITC 0. This means if doing multi-core tracing, all ITC print messages will show up at core 0 ITC Prints, as in:

`[0] ITC Print: Hello World!`

Also, if multiple cores are printing to the ITC registers, their output could be intermixed and displayed as jumbled. To prevent that, either only print from a single core, or use locks around the prints.

#### ITC Print Flushing

Because the ITC stimulus register is 32 bits, data is grouped in 4-byte packets before writing the register (using either the custom itcprintf()/itcputs() routines, or the redirected stdout). If fewer than 4 bytes are sent, or at the tail of a printf() where fewer than 4 bytes are left to send, the partially full 32 bit data word can sit there waiting for more bytes to fill out the 32 bits. There is a mechanism to cause a flush - if you terminate your print with either a '\n' or a '\r', the partial word will be flushed. The itcprintf() and itcputs() routines know when they are at the end of the print, and they will always flush the last word - partial or not.

The trace-decoder has a similar issue when reconstructing the print string. It buffers the messages until either a '\n' or a '\r' is seen, and then it will display the data. The trace-decoder will also flush the data when its buffer fills up.

#### Enabling trace and ITC in the trace collection

When using Freedom Studio to view trace data and ITC prints, Freedom Studio provides options to enable tracing and viewing ITC print data. If instead you are collecting trace data using OpenOCD, you will need to telnet into OpenOCD and then load the trace.tcl script file (in the scripts directory). Then you will need to enable the ITC by using the command:

`> itc all`

followed by:

`> trace on`

When done tracing:

`> trace off`

and to write the trace data to a file

`> wtb <filename.rtd>`

The trace file can then be processed with the dqr tool.