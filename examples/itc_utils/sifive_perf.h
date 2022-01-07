/* Copyright 2021 SiFive, Inc */
/* This is a C macro library to control trace on the target */
/* Supports single/multi cores, TE/CA/TF traces */

#ifndef SIFIVE_PERF_H_
#define SIFIVE_PERF_H_

#include <stdint.h>

#include <metal/cpu.h>
#include <metal/hpm.h>

#define PERF_CORES_ALL (numCores)

int perfInit(int num_cores,int num_funnels,unsigned long long timerFreq);
int perfTimerISRInit(int core,int interval,uint32_t counterMask,int itcChannel,int stopOnWrap,int markerCnt);
int perfWriteCntrs();
int perfResetCntr(int hpm_counter, struct metal_cpu *cpu);
int perfManualInit(int core,uint32_t counterMask,int itcChannel,int stopOnWrap,int markerCnt);

#endif // SIFIVE_PERF_H_
