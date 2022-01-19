/* Copyright 2021 SiFive, Inc */
/* This is a C macro library to control trace on the target */
/* Supports single/multi cores, TE/CA/TF traces */

#ifndef SIFIVE_PERF_H_
#define SIFIVE_PERF_H_

#include <stdint.h>

#include <metal/cpu.h>
#include <metal/hpm.h>

#define PERF_MAX_CORES		8
#define PERF_MARKER_VAL		(('p' << 24) | ('e' << 16) | ('r' << 8) | ('f' << 0))
#define PERF_FUNCMARKER_VAL (('f' << 24) | ('u' << 16) | ('n' << 8) | ('c' << 0))

int perfInit(int num_cores,int num_funnels);
int perfTimerISRInit(int interval,uint32_t counterMask,int itcChannel,int stopOnWrap,int markerCnt);
int perfManualInit(uint32_t counterMask,int itcChannel,int stopOnWrap,int markerCnt);
int perfFuncEntryExitInit(uint32_t counterMask,int itcChannel,int stopOnWrap,int markerCnt);

int perfTraceOn();
int perfTraceOff();
int perfWriteCntrs();
int perfResetCntrs(uint32_t cntrMask);
void __cyg_profile_func_enter(void *this_fn,void *call_site);
void __cyg_profile_func_exit(void *this_fn,void *call_site);

#endif // SIFIVE_PERF_H_
