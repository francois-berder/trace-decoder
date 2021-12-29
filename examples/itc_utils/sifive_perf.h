/* Copyright 2021 SiFive, Inc */
/* This is a C macro library to control trace on the target */
/* Supports single/multi cores, TE/CA/TF traces */

#ifndef SIFIVE_PERF_H_
#define SIFIVE_PERF_H_

#include <stdint.h>

#include <metal/cpu.h>
#include <metal/hpm.h>

#define PERF_CORES_ALL (numCores)

typedef struct {
    struct {
        uint8_t teInstruction;
        uint8_t teInstrumentation;
        uint8_t teStallOrOverflow;
        uint8_t teStallEnable;
        uint8_t teStopOnWrap;
        uint8_t teInhibitSrc;
        uint8_t teSyncMaxBTM;
        uint8_t teSyncMaxInst;
        uint8_t teSink;
    } teControl;
    uint32_t itcTraceEnable;
    struct {
    	uint8_t tsCount;
        uint8_t tsDebug;
        uint8_t tsPrescale;
        uint8_t tsEnable;
        uint8_t tsBranch;
        uint8_t tsInstrumentation;
        uint8_t tsOwnership;
    } tsControl;
    uint32_t teSinkBase;
    uint32_t teSinkBaseH;
    uint32_t teSinkLimit;
} perf_settings_t;

int perfInit(int num_cores,int num_funnels);
int perfTimerISRInit(int core,int interval,uint32_t counterMask,int itcChannel,int stopOnWrap,int markerCnt);

//add manual protos!

#endif // SIFIVE_PERF_H_
