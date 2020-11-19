/* Copyright 2020 SiFive, Inc */
/* SPDX-License-Identifier: Apache-2.0 */

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "itc_utils.h"

/*
 * Default to channel 0.
 */
static int itc_print_channel = 0;
static int _trace_config = 0;

int itc_enable(int channel)
{
	if ((channel < 0) || (channel > 31)) {
		return 1;
	}

	*itcTraceEnable |= (1 << channel);

	return 0;
}

int itc_disable(int channel)
{
	if ((channel < 0) || (channel > 31)) {
		return 1;
	}

	*itcTraceEnable &= ~(1 << channel);

	return 0;
}

static inline void _disable_inst_trace()
{
	_trace_config = *teControl;
	*teControl = _trace_config &= ~(0x70);
	int tmp = *teControl;
}

static inline void _restore_inst_trace()
{
	*teControl = _trace_config  |= 0x3;
	int tmp = *teControl;
}

void resetTraceAll()
{
	   // put everything in reset

	   *teControl = 0;
	   *tsControl = 0;
	   *xtiControl = 0;
	   *pcControl = 0;
	   *pibControl = 0;

	   // everything out of reset, tracing halted

	   *teControl = TE_ACTIVE;
	   *tsControl = TS_ACTIVE;
	   *pibControl = PIB_ACTIVE;

	   // clear some itc stuff

	   *itcTrigEnable = 0;
	   *itcTraceEnable = 0;
//	   *itcTraceEnable = 0x00000001;
}

void enableTeTrace()
{
	*teControl |= TE_TRACING;
}

void disableTeTrace()
{
	*teControl &= ~TE_ENABLE;
}

void enablePib()
{
	*pibControl |= PIB_ENABLE;
}

void disablePib()
{
	*pibControl |= ~PIB_ENABLE;
}

void enableTs()
{
	*tsControl |= TS_ENABLE;
}

void disableTs()
{
	*tsControl |= TS_ENABLE;
}

void setItcChannels(uint32_t channel_mask)
{
	*itcTraceEnable = channel_mask;
}

void setTeControl(uint32_t instruction,uint32_t instrumentation,bool stall,bool src,uint32_t sink)
{
	uint32_t mask;

	*teControl = TE_ACTIVE;

	mask = sink | instrumentation | instruction | TE_ENABLE | TE_ACTIVE;

	if (stall == true) {
		mask |= TE_STALL;
	}

	if (src == true) {
		mask |= TE_STOPONWRAP;
	}

	*teControl = mask;
}

uint32_t getTeControl()
{
	return *teControl;
}

void setTsControl(bool count_runs,bool debug,bool add_to_ITC,bool add_to_ownership)
{
	uint32_t mask;

	*tsControl = TS_ACTIVE;

	mask = TS_ACTIVE;

	if (count_runs != false) {
		mask |= TS_COUNT;
	}

	if (debug != false) {
		mask |= TS_DEBUG;
	}

	if (add_to_ITC != false) {
		mask |= TS_INSTRUMENTATION;
	}

	if (add_to_ownership != false) {
		mask |= TS_OWNERSHIP;
	}

	*tsControl = mask;
}

uint32_t getTsControl()
{
	return *tsControl;
}

void setPibControl(uint16_t divisor,uint8_t mode,bool refCounter,bool refCalibrate)
{
	uint32_t mask;

	*pibControl = PIB_ACTIVE; // make sure pibEnable is 0

	mask = (divisor << 16) | mode | PIB_ACTIVE;

	if (refCounter == true) {
		mask |= PIB_REFCENTER;
	}

	if (refCalibrate == true) {
		mask |= PIB_REFCALIBRATE;
	}

	*pibControl = mask;
}

uint32_t getPibControl()
{
	return *pibControl;
}

uint32_t getTeImpl()
{
	return *teImpl;
}

static inline void _itc_print_write_uint32(uint32_t data)
{
	volatile uint32_t *stimulus = &itcStimulus[itc_print_channel];

	while (*stimulus == 0) {}	// block until room in FIFO

	*stimulus = data;
}

static inline void _itc_print_write_uint8(uint8_t data)
{
	volatile uint32_t *stimulus = &itcStimulus[itc_print_channel];

	while (*stimulus == 0) {}	// block until room in FIFO

	uint8_t *itc_uint8 = (uint8_t*)stimulus;
	itc_uint8[3] = data;
}

static inline void _itc_print_write_uint16(uint16_t data)
{
	volatile uint32_t *stimulus = &itcStimulus[itc_print_channel];

	while (*stimulus == 0) {}	// block until room in FIFO

	uint16_t *itc_uint16 = (uint16_t*)stimulus;
	itc_uint16[1] = data;
}

// itc_fputs(): like itc_puts(), but no \n at end of text. Internal use only

static int _itc_fputs(const char *f)
{
	//_disable_inst_trace();

	int rc;

	rc = strlen(f);

	int words = (rc/4)*4;
	int bytes = rc & 0x03;
	int i;
	uint16_t a;

    for (i = 0; i < words; i += 4) {
        _itc_print_write_uint32(*(uint32_t*)(f+i));
    }

    switch (bytes) {
    case 0:
    	break;
    case 1:
    	_itc_print_write_uint8(*(uint8_t*)(f+i));
    	break;
    case 2:
    	_itc_print_write_uint16(*(uint16_t*)(f+i));
    	break;
    case 3:
    	a = *(uint16_t*)(f+i);
    	_itc_print_write_uint16(a);

    	a = ((uint16_t)(*(uint8_t*)(f+i+2)));
    	_itc_print_write_uint8((uint8_t)a);
    	break;
    }

    //_restore_inst_trace();
	return rc;
}

int itc_set_print_channel(int channel)
{
	itc_print_channel = channel;
	return 0;
}

// itc_puts(): like C puts(), includes \n at end of text

int itc_puts(const char *f)
{
	//_disable_inst_trace();

	int rc = strlen(f);

	int words = (rc/4)*4;
	int bytes = rc & 0x03;
	int i;
	uint16_t a;

    for (i = 0; i < words; i += 4) {
		_itc_print_write_uint32(*(uint32_t*)(f+i));
	}

    // we actually have one more byte to send than bytes, because we need to append
    // a \n to the end

    switch (bytes) {
    case 0:
    	_itc_print_write_uint8('\n');
    	break;
    case 1:
    	a = *(uint8_t*)(f+i);
    	_itc_print_write_uint16(('\n' << 8) | a);
    	break;
    case 2:
    	_itc_print_write_uint16(*(uint16_t*)(f+i));
    	_itc_print_write_uint8('\n');
    	break;
    case 3:
    	a = *(uint16_t*)(f+i);
    	_itc_print_write_uint16(a);

    	a = ((uint16_t)(*(uint8_t*)(f+i+2)));
    	_itc_print_write_uint16(('\n' << 8) | a);
    	break;
    }

    //_restore_inst_trace();
	return rc+1;
}

int itc_printf(const char *f, ... )
{
	char buffer[256];
	va_list args;
	int rc;

	va_start(args, f);
	rc = vsnprintf(buffer,sizeof buffer, f, args);
	va_end(args);

	_itc_fputs(buffer);
	return rc;
}

inline void itc_write_uint32(int channel, uint32_t data)
{
	volatile uint32_t *stimulus = &itcStimulus[channel];

	while (*stimulus == 0) {}	// block until room in FIFO

	*stimulus = data;
}

inline void itc_write_uint8(int channel, uint8_t data)
{
	volatile uint32_t *stimulus = &itcStimulus[channel];

	while (*stimulus == 0) {}	// block until room in FIFO

	uint8_t *itc_uint8 = (uint8_t*)stimulus;
	itc_uint8[3] = data;
}

inline void itc_write_uint16(int channel, uint16_t data)
{
	volatile uint32_t *stimulus = &itcStimulus[channel];

	while (*stimulus == 0) {}	// block until room in FIFO

	uint16_t *itc_uint16 = (uint16_t*)stimulus;
	itc_uint16[1] = data;
}
