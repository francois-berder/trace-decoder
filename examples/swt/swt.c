/* Copyright 2020 SiFive, Inc */
/* SPDX-License-Identifier: Apache-2.0 */

#include <stdio.h>
#include "itc_utils.h"

#define EMIT_ITC_PATTERN 1
#define USE_ITC_DELAY 1
#define CALIBRATION_SETTING 0

int main() {
   resetTraceAll();

   setItcChannels(0x00000001);

   setTeControl(TE_INST_NONE,TE_ITC_ALL,false,false,TE_SINK_PIB);
   setTsControl(true,false,true,false);

   uint32_t clockDivisor = (130*1000000)/115200 - 1;

   setPibControl(clockDivisor,PIB_MODE_UART,false,false);

   enableTs();
   enablePib();
   enableTeTrace();

#if EMIT_ITC_PATTERN
   printf("baseAddr: 0x%08x\n",baseAddress);
   printf("teControl: 0x%08x\n",getTeControl());
   printf("teImpl: 0x%08x\n",getTeImpl());
   printf("pibControl: 0x%08x\n",getPibControl());
   printf("tsControl: 0x%08x\n",getTsControl());

   volatile uint32_t *stimulus;

   stimulus = &itcStimulus[0];

   int lc = 0;
   while (1)
   {
      volatile int counter = 0;

      lc += 1;
      printf("lc: %d\n",lc);

      itc_puts("testing");

      do {} while (*stimulus == 0);

#if USE_ITC_DELAY
      // delay
      while (counter < 1000 * 1000 * 5)
      {
	 counter++;
      }
#endif
   }
#endif
   printf("Hello, World!\n");

}
