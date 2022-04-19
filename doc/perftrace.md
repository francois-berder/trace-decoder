# Sifive Linux Performance Trace Kernal Module

### Introduction

Collecting performance traces on Linux systems currently requires a device driver to perform certain operations that normally cannot be performed in user mode.

This document describes the functionality of the device driver, and goes over how to build it with the SiFive FUSCK Linux port to the x280 implementation of the RISK-V processor. For information on the SiFive performance trace library and the trace decoder, see the respective documentation in the doc folder of the trace-decoder github repository.

### Description

To successfully trace a user application in Linux, and collect performance counter data, several steps must be accomplished before tracing can begin:

- The trace engine registers must be correctly programmed
- If an SBA buffer is desired, it must be allocated and programmed into the trace engine
- If collecting performance counter data, reading of the performance counters in user mode must be enabled, as well as programming the performance counters to the desired events
- And when tracing is complete, it must be written to a disk file.

However, these operations cannot normally be accomplished in user mode. Instead a device driver (kernal module) is needed that allows to program under trace to request it to:

- Map the trace engine registers into the applications address space
- Allocate an SBA buffer and pass the address back to the application so the application can correctly program the trace engine to use it
- Program the performance counter event (HPMEvent) registers to count the desired events
- Enable reading the performance counter (HPM) registers in user mode, and start them counting
- Copy the collected trace from the previously allocated trace buffer into the applications address space so it can be written to a file.

Details on how to use the API exposed by the device driver are given in the usage section below.

### Usage

The traceperf device driver supports the following operations, which a user-mode application can request to enable the ability to collect performance information and record it in a trace using the hardware trace encoder:

```
int fd = open("/dev/traceperf",O_RDWR | O_SYNC)
```

Description: Opens the traceperf device (named /dev/traceperf) and returns a file descriptor for the open device. Must be called before any other calls. The mode must be O_RDWR for the operations to perform correctly.

To use the open feature of the traceperf device driver, you must include:

```
#include <fcntl.h>
```

Returns: If the value returned is non-negative, the value returned is a valid file descriptor; otherwise error.

```
int close(int fd)
```

Description: Closes the traceperf device using the file descriptor that was previously returned from a successful open() call. If HPM reads were enabled in user space while the device was opened, they remain enabled. If the trace engine registers were mapped into the user applications address space, they remain mapped. Any event counter registers that were set, along with starting or stopping the event counters remain in effect. If a memory buffer was allocated for the trace engine, it remains allocated.

To use the close function of the device driver, you must include:

```
#include <unistd.h>
```

Returns: If the value returned is non-negative, the operation was successful; otherwise error.

```
void *mmap(void *addr,size_t length,int prot,int flags,int fd,off_t offset)
```

Maps the SiFive trace encoder registers into the userspace of the calling application, allowing the user application to access the trace encoder registers directly.

To use the mmap() function of the device driver, you must include:

```
#include <sys/mman.h>
```

Arguments:

`addr:` Should be set to 0 to allow the operating system to choose an address in application memory space to map registers into.

`length:` Size in bytes of the trace engine register space to map. Should be set to 0x19000 on current SiFive trace encoder implementations.

`prot:` Need to have read/write access. Use (PROT_READ | PROT_WRITE). If the open() call did not specify O_RDWR, this call will fail.

`flags:` Set to MAP_SHARED.

`fd:` Use the file descriptor returned from the open() call.

`offset:` The physical address of the beginning of the trace engine register block.

Returns: On success, mmap() returns the address in user space for the trace encoder registers. On failure, NULL is returned.

```
ioctl(int fd,unsigned long request,...)
```

The ioctl() function of the device driver is used to perform operations custom to the device driver, such as programming the event counters, enabling reading the event counters from user space, starting and stopping the counters, and allocating a physical memory buffer for the trace buffer.

To use the ioctl() function of the device driver, the application must include:

```
#include <sys/ioctl.h>
#include "sifive_linux_perf.h"
```

The following custom requests are supported:

```
#define PERF_IOCTL_NONE                         0
#define PERF_IOCTL_GET_HW_CNTR_MASK             100
#define PERF_IOCTL_START_CNTRS                  101     
#define PERF_IOCTL_STOP_CNTRS                   102
#define PERF_IOCTL_CFG_EVENT_CNTR               103     
#define PERF_IOCTL_ENABLE_HW_CNTR_ACCESS        104     
#define PERF_IOCTL_DISABLE_HW_CNTR_ACCESS       105     
#define PERF_IOCTL_ALLOC_SBA_DMA_BUFFER         106
#define PERF_IOCTL_FREE_SBA_DMA_BUFFER          107
#define PERF_IOCTL_GET_SBA_BUFFER_PHYS_ADDR     108
#define PERF_IOCTL_GET_SBA_BUFFER_SIZE          109
#define PERF_IOCTL_READ_SBA_BUFFER              110
#define PERF_IOCTL_GET_EVENT_CNTR_INFO          111
```

`PERF_IOCTL_NONE:` Performs no-operation. Returns success.

`PERF_IOCTL_GET_HW_CNTR_MASK:` Get a 32 bit mask of the supported HPM counters. To use:

```
uint32_t mask;
int rc = ioctl(fd,PERF_IOCTL_GET_HW_CNTR_MASK,&mask);
```

If the bit in position n is set, the HPM counter in that position is present. For example, if bit 3 is set, HPM counter 3 is present.

Returns: If the value returned is non-negative, success; otherwise error.

`PERF_IOCTL_START_CNTRS:` Starts the specified counters.

To use:

```
uint32_t mask = mask_of_counters_to_start;
int rc = ioctl(fd,PERF_IOCTL_START_CNTRS,&mask);
```

If the bit in position n is set, that counter is started if it exists. For example, if bit 3 is set in the mask argument, HPM counter 3 will be started.

Returns: If the value returned is non-negative, the operation was successful; otherwise error.

`PERF_IOCTL_STOP_CNTRS:` Stops the specified counters.

To use:

```
uint32_t mask = mask_of_counters_to_stop;
int rc = ioctl(fd,PERF_IOCTL_STOP_CNTRS,&mask);
```

If the bit in position n is set, that counter is stopped if it exists. For example, if bit 3 is set in the mask argument, HPM counter 3 will be stopped.

Returns: If the value returned is non-negative, the operation was successful; otherwise error.

`PERF_IOCTL_CFG_EVENT_CNTR:` Initializes an event configuration register to count the desired events in the matching HPM counter. Configures a single event configuration register at a time (must be repeated to program more than one event configuration register). On success, returns the size of the corresponding event counter, and the CSR address for the event counter.

To use:

```
perfEvent event;
uint64_t arg;

... event struct must be initialized. See the sifive_perf.md document for additional information

arg = (uint64_t)&event;
int rc = ioctl(devFD,PERF_IOCTL_CFG_EVENT_CNTR,&arg);
```

Returns: If the value returned is non-negative, the operation was successful; otherwise error.

`PERF_IOCTL_ENABLE_HW_CNTR_ACCESS:` Allow reading the specified HPM counters in user mode.

The ability to read the counters in the corresponding non-zero bit positions of the mask argument will be enabled in user mode. Counters in bit positions with a value of 0 will not have the readability in user mode changed (neither enabled or disabled, but left as is).

To use:

```
uint32_t cntrMask = counters_to_enable_access;

rc = ioctl(devFD,PERF_IOCTL_ENABLE_HW_CNTR_ACCESS,&cntrMask);
```

returns: If the value returned is non-negative; the operation was successful, otherwise error. The mask value is altered to reflect what counters are readable from user space.

`PERF_IOCTL_DISABLE_HW_CNTR_ACCESS:` Disable reading the specified HPM counters in user mode.

The ability to read the counters in the corresponding non-zero bit positions of the mask argument will be disabled in user mode.

To use:

```
uint32_t cntrMask = counters to disable access;

rc = ioctl(devFD,PERF_IOCTL_DISABLE_HW_CNTR_ACCESS,&cntrMask);
```

Returns: If the value returned is non-negative; the operation was successful, otherwise error. The mask value is altered to reflect what counters are not readable from user space.

`PERF_IOCTL_ALLOC_SBA_DMA_BUFFER:` Allocate a buffer of physically contiguous memory and return the physical address.

The kmalloc() function is used to allocate memory so it will be guaranteed to be contiguous in physical memory. The use of kmalloc() imposes a maximum buffer size, usually 4MB. Physically contiguous memory is needed for the trace engine sink, as it bypasses all virtual memory translation. In the example below, set arg to the size in bytes being requested for the buffer. Only one buffer may be allocated at a time. Attempting to allocate a buffer while another buffer is allocated will cause the previously allocated buffer to be freed before allocating a new buffer.

To use:

```
uint64_t arg;

arg = sink_size_in_bytes;

rc = ioctl(devFD,PERF_IOCTL_ALLOC_SBA_DMA_BUFFER,&arg);
```

Returns: If the value returned is non-negative, arg will have the physical address of the requested buffer; otherwise, error.

`PERF_IOCTL_FREE_SBA_DMA_BUFFER:` Frees a memory buffer previously allocated with `PERF_IOCTL_ALLOC_SBA_DMA_BUFFER`.

If a memory region has been previously allocated with PERF_IOCTL_ALLOC_SBA_DMA_BUFFER, it will be freed.

To use:

```
rc = ioctl(devFD,PERF_IOCTL_ALLOC_SBA_DMA_BUFFER);
```

Returns: If the value returned is non-negative, any previously allocated buffer is freed; otherwise error.

`PERF_IOCTL_GET_SBA_BUFFER_PHYS_ADDR:` Return the address of a previously allocated buffer using the `PERF_IOCTL_ALLOC_SBA_BUFFER`.

Can be used to reuse a previously allocated memory buffer, or to check for a previously allocated memory buffer. If a buffer has been previously allocated and not freed, the physical address will be returned in the arg parameter. Otherwise, the arg parameter will be set to 0.

To use:

```
uint64_t arg;
rc = ioctl(devFD,PERF_IOCTL_GET_SBA_BUFFER_PHYS_ADDR,&arg);
```

Returns: If the value returned is non-negative, the address or a previously allocated SBA buffer will be returned in the arg parameter. If a SBA buffer has not previously been allocated, arg will be set to 0. On error, a negative value will be returned.

`PERF_IOCTL_GET_SBA_BUFFER_SIZE:` Return the size of a previously allocated SBA buffer.

To use:

```
uint32_t buffsize;
rc = ioctl(devFD,PERF_IOCTL_GET_SBA_BUFFER_SIZE,&buffsize);
```

Returns: If the value returned is non-negative, the buffsize argument will be set the the size of any previously allocated SBA buffer, or 0 if no buffer has been allocated. On error, the value returned will be negative.

`PERF_IOCTL_READ_SBA_BUFFER:` Reads the entire SBA buffer from the physical address into a user space buffer.

Buffers allocated with the PERF_IOCTL_ALLOC_SBA_DMA_BUFFER ioctl are not readable from user space. This ioctl function must be called to copy the SBA buffer into a user space buffer (for such operations as writing it to a file). The user space buffer must be large enough for the entire SBA buffer.

To use:

```
char *buffptr;

... allocate a memory buffer big enough for the SBA buffer and point buffptr to it

rc = ioctl(devFD,PERF_IOCTL_READ_SBA_BUFFER,&buffptr);
```

Returns: If the value returned is non-negative, the entire contents of the SBA buffer will be copied into the user space buffer pointed to by buffptr. On error, a negative value is returned. Note: if the user buffer is not big enough or the value is buffptr is invalid, the returned value may still be non-negative, but likely bad things are about to happen.

`PERF_IOCTL_GET_EVENT_CNTR_INFO:` Retrieve information on the requested counter.

To use:

```
struct perfCntrInfo cinfo;
uint64_t arg;
cinfo.ctrIdx = ctrNum;
arg = (uint64_t)&cinfo;
rc = ioctl(devFD,PERF_IOCTL_GET_EVENT_CNTR_INFO,&arg);
```

Returns: If the value returned is non-negative, the perfCntrInfo struct will have the information for ctrNum filled in if it has been previously programmed with a PERF_IOCTL_CFG_EVENT_CNTR. Also, the CSR address and number of bits in the counter will be returned. On error, a negative value is returned.

### Building perftrace

Building the perftrace device driver requires you have the buildable sources for the Linux you are running 
on the x280 platform, and a collection of tools necessary to build the Linux OS. The following sections cover building the FUSDK Linux for the x280, and then building the device driver.

#### Building FUSDK Linux for the x280:

Get a copy of the FUSDK Linux sources. It is available from various places, and is not covered in this document. After getting the sources, follow the instructions included with the FUSDK distribution. They should be complete and follow them closely unless you are an expert. You may need to install several applications not mentioned in the instructions to get everything to build. Check any error messages carefully to know what applications to install (beyond what is covered in the instructions). You will be building on a Linux system using a cross-development tool chain. Depending on the build system performance, the build may take hours.

The packages I had to install on my build system (Ubuntu) were:

```
curl
gcc
git
gawk
```

To make git work, I also had to do:

```
git config --global user.name "first last"
git config --global user.email "name@place"
```

Where "first last" is your first and last names, and "nam@place" is your email address.

The build process looks for python, and requires python3. It checks for an executable `python` in the search path, and if found verifies it is python3. If the python executable is found, but is not python3, it then tries python3, and if found, all is well. If python is not found, it will fail and not look for python3.

If you have python3 but no python or python link to python3, create one using the command below:

```
sudo ln -s /usr/bin/python /usr/bin/python3
```

After building Linux for the x280 on a VCU-118 board, install the Linux image on an SD card, the x280 multi-core MCS file on the VCU-118, and boot Linux on the x280/VCU-118. Instructions should be in the FUSDK documentation.

#### Booting Linxu on the x280:

Connect to the VCU-118 from a Linux box using a USB serial connection. The USB serial connections are probably already there if you programmed the x280 MCS files onto the VCU-118. If they are not, connect both USB connections on your VCU-118 to your Linux system (you may use a bridge if needed). Begin the boot process on the VCU (power on with the SD card containing the FUSDK Linux image). Then use screen on your Linux build system to connect to the VCU-118, as in:

sudo screen -A /dev/ttyUSB2 115200

Here, you may need to substitute a different device for /dev/ttyUSB2 if you don't get output (such as /dev/ttyUSB1, /dev/ttyUSB3, or /dev/ttyUSB4). When working, you should see boot messages scrolling by in the screen window. It takes about 10 minutes for the VCU-118/x280 to boot. After the system boots, you should get a prompt to login. If you don't see it, try pressing the enter key a few times. When you get the boot prompt, login as user root, password sifive.

If you don't see an /dev/ttyUSB devices in the /dev folder, reboot your Linux box with the VCU-118 powered on and connected.

Enabling the SSH server on the VCU-118 FUSDK Linux:

The FUSDK Linux supports using ssh to login to the VCU-118, but first you must enable the SSH server on the VCU-118 running Linux. To do that, login to the FUSDK system using the screen command. Then on the VCU-118 Linux, type:

```
systemctl enable sshd.socket
```

and

```
systemctl start sshd.socket
```

This should only need to be done once, subsequent boots after this will now have it enabled.

The VCU-118 should be plugged into the same router as your Linux build system. You can now connect to the VCU-118 system by typing the following on the build system:

```
ssh root@sifive-fpga
```

When prompted for the password, enter `sifive`.

Other commands, such as `scp` should also work to copy files between your Linux build system and the VCU-118 x280 system.

#### Building the perftrace device driver:

The following instructions cover how to build the perftrace device driver for the FUSDK x280 Linux built previously.

First, obtain the sources from the github.com/sifive/trace-encoder repository. They will be in the examples/perftrace folder. You will need the traceperf.c file.

The perftrace device driver must be built as part of the FUSDK build, but you can specify to only build the perftrace device driver, and not the entire Linux, assuming the FUSDK Linux has already been built (this will likely save hours).

To add the perftrace device driver to the FUSDK build, do the following (assuming you have the FUSDK build setup in a folder named fusdk in your home directory):

```
cd ~fusdk
cd src-release-dir
cd riscv-sifive

repo init -u ./meta-sifive -m tools/manifests/sifive.xml
repo sync
repo start work --all

./openembedded-core/scripts/install-buildtools -r yocto-3.2_M2 -t 20200729
source ./openembedded-core/buildtoosl/environment-setup-x86_64-pokysdk-linux
source ./meta-sifive/setup.sh
```

You will now be in the `riscv-sifive/build` directory

```
cd .. (back to riscv-sifive directory)
```

Now add a new layer for traceperf:

```
bitbake-layers create-layer meta-traceperf
cd build
bitbake-layers add-layer ../meta-traceperf
```

If you get error messages from the add-layer step, cd into build/conf, and edit bblayers.conf manually. Add the following line to the end of bblayers.conf:

```
"<your home dir>/fusdk/src-release-dir/riscv-sifive/meta-traceperf \"
```

Where `<your home dir>` is the absolute path to where the fusdk folder is.

Next, perform the following steps:

```
cd ../../meta-traceperf
mv recipes-example recipes-kernal
cd recipes-kernal
mv example traceperf
cd traceperf
mkdir files
cd files
```

Copy the traceperf.c file, obtained from the trace-decoder github step earlier, into the files folder. You will also need a Makefile. The Makefile below should work:

Makefile:

```
obj-m := traceperf.o

SRC := $(shell pwd)

all:
	$(MAKE) -C $(KERNEL_SRC) M=$(SRC)

modules_install:
	$(MAKE) -C $(KERNEL_SRC) M=$(SRC) modules_install

clean:
	rm -f *.o *~ core .depend .*.cmd *.ko *.mod.c
	rm -f Module.markers Module.symvers modules.order
	rm -rf .tmp_versions Modules.symvers
```

Besides the Makefile, you will need a COPYING file. If one was not generated, copy a preexisting one from somewhere in the fusdk folders.

You will also need a traceperf_0.1.bb file in the traceperf folder. If one was not already created, use the one below.

traceperf_0.1.bb:

```
SUMMARY = "Example of how to build an external Linux kernel module"
DESCRIPTION = "${SUMMARY}"
LICENSE = "GPLv2"
LIC_FILES_CHKSUM = "file://COPYING;md5=12f884d2ae1ff87c09e5b7ccc2c4ca7e"

inherit module

SRC_URI = "file://Makefile \
           file://traceperf.c \
           file://COPYING \
          "

S = "${WORKDIR}"

# The inherit of module.bbclass will automatically name module packages with
# "kernel-module-" prefix as required by the oe-core build environment.

RPROVIDES_${PN} += "kernel-module-traceperf"
```

A layer.conf file should have been created in the meta-traceperf/conf folder. If not, use the file below.

layer.conf:

```
# We have a conf and classes directory, add to BBPATH
BBPATH .= ":${LAYERDIR}"

# We have recipes-* directories, add to BBFILES
BBFILES += "${LAYERDIR}/recipes-*/*/*.bb \
            ${LAYERDIR}/recipes-*/*/*.bbappend"

BBFILE_COLLECTIONS += "meta-traceperf"
BBFILE_PATTERN_meta-traceperf = "^${LAYERDIR}/"
BBFILE_PRIORITY_meta-traceperf = "6"

LAYERDEPENDS_meta-traceperf = "core"
LAYERSERIES_COMPAT_meta-traceperf = "honister"
```

After these steps, you should have the following directory structure and files:

Directory structure:

```
        ~fusdk
          \- src-release-dir
              \- riscv-sifive
                  \- meta-traceperf
                     |- conf (dir)
                     |   \- layer.conf
                     |- recipes-kernel (dir)
                     |   \- traceperf
                     |       |- files
                     |       |   |- COPYING
                     |       |   |- Makefile
                     |       |   \- traceperf.c
                     |       \- traceperf_0.1.bb
                     |- COPYING.MIT
                     \- README
```

There will be other files and folders in the src-release-dir and riscv-sifive folders, but they are part of the normal fusdk build.

When the files and folders are all in place, the traceperf device driver can be built. To build the traceperf device driver:

```
cd ~fusdk/src-release-dir/riskv-sifive
source ./meta-sifive/setup.sh
MACHINE=sifive-fpga bitbake traceperf
```

This will build only the traceperf device driver, and place it in:

```
~/fusdk/src-release-dir/riscv-sifive/build/tmp-glibc/work/sifive_fpga-oe-loinux/traceperf/0.1-r0/traceperf.ko
```

The scp command can then be used to copy the traceperf device driver to the VCU-118 system, as in:

```
cd ~/fusdk/src-release-dir/riscv-sifive/build/tmp-glibc/work/sifive_fpga-oe-loinux/traceperf/0.1-r0
scp traceperf.ko root@sifive-fpga:
```

When prompted for a password, use `sifive`. Next, on the sifive-fpga system, the device driver can be loaded using the following instructions to load the kernal module.

### Loading and Unloading

To use the device driver, it first must be loaded. After usage, or to load a new version, it must be unloaded first.

The loading process consists of using the Linux insmod utility, and then creating a link in the `/dev` folder so applications can open and use it. A script file to accomplish those tasks is:

```
#!/bin/sh
module="traceperf"
device="traceperf"
mode="664"

echo /sbin/insmod ./${module}.ko
/sbin/insmod ./${module}.ko $* || exit 1

# Remove stale nodes
rm -f /dev/${device}

major =$(awk "\$2==\"${module}\" {print \$1}" /proc/devices)

echo "major: ${major}"

mknod /dev/${device} c {major} 0

group="staff"
grep -q '^staff:' /ect/group || group="wheel"

chgrp $group /dev/${device}
chmod $mode /dev/${device}
```

To unload the kernal module, just do `rmmod traceperf`.

To check if the driver is loaded and running, use the `lsmod` command.

Loading and unloading must be done as root.

### A note on security

The perftrace kernel module does nothing to enforce any kind of security. It is intended to only be used on targets being traced, and only while they are being traced. Use at your own risk. You have been warned.

### Limitations

The largest SBA buffer that can be allocated is 4MB. The maximum size on your system may be different; it will depend on how the Linux kernel was built. The size restriction is due to needing to use kmalloc() to allocate space for the trace buffer. The trace buffer must be in physically contiguous memory because the trace engine writes to not go through any kind of address translations, which would be required if memory was not contiguous.
