/* Copyright 2020 SiFive, Inc */
/* SPDX-License-Identifier: Apache-2.0 */

#ifndef ITC_UTILS_H
#define ITC_UTILS_H
#include <stdio.h>
#include <stdbool.h>

#include <metal/machine/platform.h>

// Register Offsets

#define Offset_teControl	0x0000
#define Offset_teImpl		0x0004
#define	Offset_teSinkBase	0x0010
#define Offset_teSinkBaseHigh	0x0014
#define Offset_teSinkLimit	0x0018
#define Offset_teSinkWP		0x001c
#define Offset_teSinkRP		0x0020
#define	Offset_teSinkData	0x0024
#define	Offset_tsControl	0x0040
#define Offset_tsLower		0x0044
#define Offset_tsUpper		0x0048
#define Offset_xtiControl	0x0050
#define	Offset_xtoControl	0x0054
#define	Offset_wpControl	0x0058
#define Offset_itcTraceEnable	0x0060
#define	Offset_itcTrigEnable	0x0064
#define	Offset_itcStimulus	0x0080
#define Offset_pcControl        0x0100
#define	Offset_atbSink		0x0e00
#define Offset_pibControl	0x0f00

// Register Address and types

#define teControl	((volatile uint32_t*)(baseAddress+Offset_teControl))
#define teImpl		((volatile uint32_t*)(baseAddress+Offset_teImpl))
#define	teSinkBase	((volatile uint32_t*)(baseAddress+Offset_teSinkBase))
#define teSinkBaseHigh	((volatile uint32_t*)(baseAddress+Offset_teSinkBaseHigh))
#define teSinkLimit	((volatile uint32_t*)(baseAddress+Offset_teSinkLimit))
#define teSinkWP	((volatile uint32_t*)(baseAddress+Offset_teSinkWP))
#define teSinkRP	((volatile uint32_t*)(baseAddress+Offset_teSinkRP))
#define	teSinkData	((volatile uint32_t*)(baseAddress+Offset_teSinkData))
#define	tsControl	((volatile uint32_t*)(baseAddress+Offset_tsControl))
#define tsLower		((volatile uint32_t*)(baseAddress+Offset_tsLower))
#define tsUpper		((volatile uint32_t*)(baseAddress+Offset_tsUpper))
#define xtiControl	((volatile uint32_t*)(baseAddress+Offset_xtiControl))
#define	xtoControl	((volatile uint32_t*)(baseAddress+Offset_xtoCtonrol))
#define	wpControl	((volatile uint32_t*)(baseAddress+Offset_wpControl))
#define itcTraceEnable	((volatile uint32_t*)(baseAddress+Offset_itcTraceEnable))
#define	itcTrigEnable	((volatile uint32_t*)(baseAddress+Offset_itcTrigEnable))
#define	itcStimulus	((volatile uint32_t*)(baseAddress+Offset_itcStimulus))
#define	pcControl	((volatile uint32_t*)(baseAddress+Offset_pcControl))
#define	atbSink		((volatile uint32_t*)(baseAddress+Offset_atbSink))
#define pibControl	((volatile uint32_t*)(baseAddress+Offset_pibControl))

#ifndef METAL_SIFIVE_TRACE_0_BASE_ADDRESS
#error METAL_SIFIVE_TRACE_0_BASE_ADDRESS is not defined.  Does this target have trace?
#endif
#define baseAddress (METAL_SIFIVE_TRACE_0_BASE_ADDRESS)

#define PIB_ACTIVE (1<<0)
#define PIB_ENABLE (1<<1)
#define PIB_MODE_NONE (0<<4)
#define PIB_MODE_MANCHESTER (4<<4)
#define PIB_MODE_UART	(5<<4)
#define PIB_REFCENTER (1<<8)
#define PIB_REFCALIBRATE (1<<9)

#define TE_ACTIVE	(1<<0)
#define TE_ENABLE	(1<<1)
#define	TE_TRACING	(1<<2)
#define TE_EMPTY	(1<<3)
#define TE_INST_NONE	(0<<4)
#define TE_INST_PERIODIC	(1<<4)
#define TE_INST_BTM_SYNC	(3<<4)
#define TE_INST_HTM_SYNC_NOOPT	(6<<4)
#define	TE_INST_HTM_SYNC	(7<<4)
#define TE_ITC_NONE	(0<<7)
#define	TE_ITC_ALL	(1<<7)
#define TE_ITC_OWNERSHIP_ONLY	(2<<7)
#define	TE_ITC_OWNERSHIP_ITC	(3<<7)
#define	TE_STALL	(1<<13)
#define TE_STOPONWRAP	(1<<14)
#define TE_INHIBITSRC	(1<<15)
#define TE_SINK_ERROR	(1<<27)
#define TE_SINK_DEFAULT	(0<<28)
#define TE_SINK_SRAM	(4<<28)
#define TE_SINK_ATB		(5<<28)
#define	TE_SINK_PIB		(6<<28)
#define TE_SINK_SBA		(7<<28)
#define TE_SINK_FUNNEL	(8<<28)

#define TS_ACTIVE	(1<<0)
#define TS_COUNT	(1<<1)
#define TS_RESET	(1<<2)
#define TS_DEBUG	(1<<3)
#define TS_ENABLE	(1<<15)
#define TS_INSTRUMENTATION (1<<18)
#define TS_OWNERSHIP	(1<<19)

/*
 * ITC Channel management. Generally you do not need to use these because the
 * debugger will take care of enabling and disabling channels via the trace
 * configuration dialog.  You may want to use these if you want to generate
 * ITC trace data on a target not being controlled by a debugger.
 */
int itc_enable(int channel);
int itc_disable(int channel);

/*
 * ITC Print functions.  If itc_set_print_channel() is not called
 * then channel 0 is assumed.
 */
int itc_set_print_channel(int channel);
int itc_puts(const char *f);
int itc_printf(const char *f, ... );

/*
 * General functions for writing data to an ITC channel.
 */
void itc_write_uint32(int channel, uint32_t data);
void itc_write_uint8(int channel, uint8_t data);
void itc_write_uint16(int channel, uint16_t data);

void resetTraceAll();
void enableTeTrace();
void disableTeTrace();
void enablePib();
void disablePib();
void enableTs();
void disableTs();
void setItcChannels(uint32_t channel_mask);
void setTeControl(uint32_t instruction,uint32_t instrumentation,bool stall,bool src,uint32_t sink);
void setTsControl(bool count_runs,bool debug,bool add_to_ITC,bool add_to_ownership);
void setPibControl(uint16_t divisor,uint8_t mode,bool refCounter,bool refCalibrate);
uint32_t getTeControl();
uint32_t getTsControl();
uint32_t getPibControl();
uint32_t getTeImpl();

#endif // ITC_UTILS_H
