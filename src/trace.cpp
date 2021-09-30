/*
 * Copyright 2019 Sifive, Inc.
 *
 * main.cpp
 *
 */

/*
   This file is part of dqr, the SiFive Inc. Risc-V Nexus 2001 trace decoder.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, see <https://www.gnu.org/licenses/>.
*/

#include "config.h"

#include <iostream>
#include <iomanip>
#include <fstream>
#include <cstring>
#include <cstdint>
#include <time.h>
#ifdef WINDOWS
#include <winsock2.h>
#else // WINDOWS
#include <unistd.h>
#endif // WINDOWS

#include "dqr.hpp"
#include "trace.hpp"

#ifdef DO_TIMES
Timer::Timer()
{
	struct timespec ts;

	clock_gettime(CLOCK_REALTIME,&ts);

	startTime = ts.tv_sec + (ts.tv_nsec/1000000000.0);
}

Timer::~Timer()
{
}

double Timer::start()
{
	struct timespec ts;

	clock_gettime(CLOCK_REALTIME,&ts);

	startTime = ts.tv_sec + (ts.tv_nsec/1000000000.0);

	return startTime;
}

double Timer::etime()
{
	struct timespec ts;

	clock_gettime(CLOCK_REALTIME,&ts);

	double t = ts.tv_sec + (ts.tv_nsec/1000000000.0);

	return t-startTime;
}
#endif // DO_TIMES

// class CATrace methods

CATraceRec::CATraceRec()
{
	offset = 0;
	address = 0;
}

void CATraceRec::dump()
{
	printf("0x%08x\n",(uint32_t)address);
	for (int i = 0; (size_t)i < sizeof data / sizeof data[0]; i++) {
		printf("%3d  ",(i*30)>>1);

		for (int j = 28; j >= 0; j -= 2) {
			if (j != 28) {
				printf(":");
			}
			printf("%01x",(data[i] >> j) & 0x3);
		}

		printf("\n");
	}
}

void CATraceRec::dumpWithCycle()
{
	printf("0x%08x\n",(uint32_t)address);
	for (int i = 0; (size_t)i < sizeof data / sizeof data[0]; i++) {
		for (int j = 28; j >= 0; j -= 2) {
			printf("%d %01x\n",(i*30+(28-j))>>1,(data[i] >> j) & 0x3);
		}
	}
}

int CATraceRec::consumeCAVector(uint32_t &record,uint32_t &cycles)
{
	int dataIndex;

	// check if we have exhausted all bits in this record

	// for vectors, offset and dataIndex are the array index for data[]

	dataIndex = offset;

	while (((size_t)dataIndex <= sizeof data / sizeof data[0]) && ((data[dataIndex] & 0x3fffffff) == 0)) {
		dataIndex += 1;
	}

	if ((size_t)dataIndex >= sizeof data / sizeof data[0]) {
		// out of records in the trace record. Signal caller to get more records

		record = 0;
		cycles = 0;

		return 0;
	}

	record = data[dataIndex];
	offset = dataIndex+1;

	// cycle is the start cycle of the record returned relative to the start of the 32 word block.
	// The record represents 5 cycles (5 cycles in each 32 bit record)

	cycles = dataIndex * 5;

	return 1;
}

int CATraceRec::consumeCAInstruction(uint32_t &pipe,uint32_t &cycles)
{
	int dataIndex;
	int bitIndex;
	bool found = false;

	// this function looks for pipe finish bits in an instruction trace (non-vector trace)

	// check if we have exhausted all bits in this record

//	printf("CATraceRec::consumCAInstruction(): offset: %d\n",offset);

	if (offset >= 30 * 32) {
		// this record is exhausted. Tell caller to read another record

		return 0;
	}

	// find next non-zero bit field

	dataIndex = offset / 30; // 30 bits of data in each data word. dataIndex is data[] index
	bitIndex = 29 - (offset % 30);  // 0 - 29 is the bit index to start looking at (29 oldest, 0 newest)

//	for (int i = 0; i < 32; i++) {
//		printf("data[%d]: %08x\n",i,data[i]);
//	}

	while (found == false) {
		while ((bitIndex >= 0) && ((data[dataIndex] & (1<<bitIndex)) == 0)) {
			bitIndex -= 1;
			offset += 1;
		}

		if (bitIndex < 0) {
			// didn't find any 1s in data[dataIndex]. Check next data item
			dataIndex += 1;

			if ((size_t)dataIndex >= sizeof data / sizeof data[0]) {
				return 0; // failure
			}

			bitIndex = 29;
		}
		else {
			// found a one

			// cycle is the start cycle of the pipe bit relative to the start of the 32 word block.

//			cycles = dataIndex * 15 + (29-bitIndex)/2;
//			or:
			cycles = offset/2;

//			printf("one at offset: %d, dataIndex: %d, bitindex: %d, cycle: %d\n",offset,dataIndex,bitIndex,cycles);

			// Bump past it
			offset += 1;
			found = true;
		}
	}

	if (bitIndex & 0x01) {
		pipe = TraceDqr::CAFLAG_PIPE0;
	}
	else {
		pipe = TraceDqr::CAFLAG_PIPE1;
	}

//	printf("CATraceRec::consumeCAInstruction(): Found: offset: %d cycles: %d\n",offset,cycles);

	return 1;	// success
}

CATrace::CATrace(char *caf_name,TraceDqr::CATraceType catype)
{
	caBufferSize = 0;
	caBuffer = nullptr;
	caBufferIndex = 0;
	blockRecNum = 0;

	status = TraceDqr::DQERR_OK;

	if (caf_name == nullptr) {
		status = TraceDqr::DQERR_ERR;
		return;
	}

	std::ifstream catf;

	catf.open(caf_name, std::ios::in | std::ios::binary);

	if (!catf) {
		printf("Error: CATrace::CATrace(): could not open cycle accurate trace file %s for input\n",caf_name);
		status = TraceDqr::DQERR_OPEN;
		return;
	}

	catf.seekg(0, catf.end);
	caBufferSize = catf.tellg();
	catf.seekg(0, catf.beg);

	caBuffer = new uint8_t[caBufferSize];

	catf.read((char*)caBuffer,caBufferSize);

	catf.close();

//	printf("caBufferSize: %d\n",caBufferSize);
//
//	int *ip;
//	ip = (int*)caBuffer;
//
//	for (int i = 0; (size_t)i < caBufferSize / sizeof(int); i++) {
//		printf("%3d  ",(i*30)>>1);
//
//		for (int j = 28; j >= 0; j -= 2) {
//			if (j != 28) {
//				printf(":");
//			}
//			printf("%01x",(ip[i] >> j) & 0x3);
//		}
//
//		printf("\n");
//	}

	traceQOut = 0;
	traceQIn = 0;

	caType = catype;

	switch (catype) {
	case TraceDqr::CATRACE_VECTOR:
		traceQSize = 512;
		caTraceQ = new CATraceQItem[traceQSize];
		break;
	case TraceDqr::CATRACE_INSTRUCTION:
		traceQSize = 0;
		caTraceQ = nullptr;
		break;
	case TraceDqr::CATRACE_NONE:
		traceQSize = 0;
		caTraceQ = nullptr;
		status = TraceDqr::DQERR_ERR;

		printf("Error: CATrace::CATrace(): invalid trace type CATRACE_NONE\n");
		return;
	}

	TraceDqr::DQErr rc;

	rc = parseNextCATraceRec(catr);
	if (rc != TraceDqr::DQERR_OK) {
		printf("Error: CATrace::CATrace(): Error parsing first CA trace record\n");
		status = rc;
	}
	else {
		status = TraceDqr::DQERR_OK;
	}

	startAddr = catr.address;
};

CATrace::~CATrace()
{
	if (caBuffer != nullptr) {
		delete [] caBuffer;
		caBuffer = nullptr;
	}

	caBufferSize = 0;
	caBufferIndex = 0;
}

TraceDqr::DQErr CATrace::rewind()
{
	TraceDqr::DQErr rc;

	// this function needs to work for both CA instruction and CA Vector

	caBufferIndex = 0;

	catr.offset = 0;
	catr.address = 0;

	rc = parseNextCATraceRec(catr);
	if (rc != TraceDqr::DQERR_OK) {
		printf("Error: CATrace::rewind(): Error parsing first CA trace record\n");
		status = rc;
	}
	else {
		status = TraceDqr::DQERR_OK;
	}

	startAddr = catr.address;

	traceQOut = 0;
	traceQIn = 0;

	return status;
}

TraceDqr::DQErr CATrace::dumpCurrentCARecord(int level)
{
	switch (level) {
	case 0:
		catr.dump();
		break;
	case 1:
		catr.dumpWithCycle();
		break;
	default:
		printf("Error: CATrace::dumpCurrentCARecord(): invalid level %d\n",level);
		return TraceDqr::DQERR_ERR;
	}

	return TraceDqr::DQERR_OK;
}

TraceDqr::DQErr CATrace::packQ()
{
	int src;
	int dst;

	dst = traceQOut;
	src = traceQOut;

	while ((dst != traceQIn) && (src != traceQIn)) { // still have stuff in Q
		// find next empty record

		while ((dst != traceQIn) && (caTraceQ[dst].record != 0)) {
			// look for an empty slot

			dst += 1;
			if (dst >= traceQSize) {
				dst = 0;
			}
		}

		if (dst != traceQIn) {
			// dst is an empty slot

			// now find next valid record

			src = dst+1;
			if (src >= traceQSize) {
				src = 0;
			}

			while ((src != traceQIn) && (caTraceQ[src].record == 0)) {
				// look for a record with data in it to move

				src += 1;
				if (src >= traceQSize) {
					src = 0;
				}
			}

			if (src != traceQIn) {
				caTraceQ[dst] = caTraceQ[src];
				caTraceQ[src].record = 0; // don't forget to mark this record as empty!

				// zero out the q depth stats fields

				caTraceQ[src].qDepth = 0;
				caTraceQ[src].arithInProcess = 0;
				caTraceQ[src].loadInProcess = 0;
				caTraceQ[src].storeInProcess = 0;
			}
		}
	}

	// dst either points to traceQIn, or the last full record

	if (dst != traceQIn) {
		// update traceQin

		dst += 1;
		if (dst >= traceQSize) {
			dst = 0;
		}
		traceQIn = dst;
	}

	return TraceDqr::DQERR_OK;
}

int CATrace::roomQ()
{
	if (traceQIn == traceQOut) {
		return traceQSize - 1;
	}

	if (traceQIn < traceQOut) {
		return traceQOut - traceQIn - 1;
	}

	return traceQSize - traceQIn + traceQOut - 1;
}

TraceDqr::DQErr CATrace::addQ(uint32_t data,uint32_t t)
{
	// first see if there is enough room in the Q for 5 new entries

	int r;

	r = roomQ();

	if (r < 5) {
		TraceDqr::DQErr rc;

		rc = packQ();
		if (rc != TraceDqr::DQERR_OK) {
			return rc;
		}

		r = roomQ();
		if (r < 5) {
			printf("Error: addQ(): caTraceQ[] full\n");

			dumpCAQ();

			return TraceDqr::DQERR_ERR;
		}
	}

	for (int i = 0; i < 5; i++) {
		uint8_t rec;

		rec = (uint8_t)(data >> (6*(4-i))) & 0x3f;
		if (rec != 0) {
			caTraceQ[traceQIn].record = rec;
			caTraceQ[traceQIn].cycle = t;

			// zero out the q depth stats fields

			caTraceQ[traceQIn].qDepth = 0;
			caTraceQ[traceQIn].arithInProcess = 0;
			caTraceQ[traceQIn].loadInProcess = 0;
			caTraceQ[traceQIn].storeInProcess = 0;

			traceQIn += 1;
			if (traceQIn >= traceQSize) {
				traceQIn = 0;
			}
		}

		t += 1;
	}

	return TraceDqr::DQERR_OK;
}

TraceDqr::DQErr CATrace::parseNextVectorRecord(int &newDataStart)
{
	uint32_t cycles;
	uint32_t record;
	TraceDqr::DQErr rc;

	// get another CA Vector record (32 bits) from the catr object and add to traceQ

	int numConsumed;
	numConsumed = 0;

	while (numConsumed == 0) {
		numConsumed = catr.consumeCAVector(record,cycles);
		if (numConsumed == 0) {
			// need to read another record

			rc = parseNextCATraceRec(catr); // this will reload catr.data[]
			if (rc != TraceDqr::DQERR_OK) {
				status = rc;
				return rc;
			}
		}
	}

	newDataStart = traceQIn;

	cycles += blockRecNum * 5 * 32;

	rc = addQ(record,cycles);

	status = rc;

	return rc;
}

TraceDqr::DQErr CATrace::consumeCAInstruction(uint32_t &pipe,uint32_t &cycles)
{
	// Consume next pipe flag. Reloads catr.data[] from caBuffer if needed

	int numConsumed;
	numConsumed = 0;

	TraceDqr::DQErr rc;

//	printf("CATrace::consumeCAInstruction()\n");

	while (numConsumed == 0) {
		numConsumed = catr.consumeCAInstruction(pipe,cycles);
//		printf("CATrace::consumeCAInstruction(): num consumed: %d\n",numConsumed);

		if (numConsumed == 0) {
			// need to read another record

			rc = parseNextCATraceRec(catr);
			if (rc != TraceDqr::DQERR_OK) {
				status = rc;
				return rc;
			}
		}
	}

	cycles += blockRecNum * 15 * 32;

//	printf("CATrace::consumeCAInstruction(): cycles: %d\n",cycles);

	return TraceDqr::DQERR_OK;
}

TraceDqr::DQErr CATrace::consumeCAPipe(int &QStart,uint32_t &cycles,uint32_t &pipe)
{
	if (caTraceQ == nullptr) {
		return TraceDqr::DQERR_ERR;
	}

	// first look for pipe info in Q

	// look in Q and see if record with matching type is found

	while (QStart != traceQIn) {
		if ((caTraceQ[QStart].record & TraceDqr::CAVFLAG_V0) != 0) {
			pipe = TraceDqr::CAFLAG_PIPE0;
			cycles = caTraceQ[QStart].cycle;
			caTraceQ[QStart].record &= ~TraceDqr::CAVFLAG_V0;

//			QStart += 1;
//			if (QStart >= traceQSize) {
//				QStart = 0;
//			}

			return TraceDqr::DQERR_OK;
		}

		if ((caTraceQ[QStart].record & TraceDqr::CAVFLAG_V1) != 0) {
			pipe = TraceDqr::CAFLAG_PIPE1;
			cycles = caTraceQ[QStart].cycle;
			caTraceQ[QStart].record &= ~TraceDqr::CAVFLAG_V1;

//			QStart += 1;
//			if (QStart >= traceQSize) {
//				QStart = 0;
//			}

			return TraceDqr::DQERR_OK;
		}

		QStart += 1;

		if (QStart >= traceQSize) {
			QStart = 0;
		}
	}

	// otherwise, start reading records and adding them to the Q until
	// matching type is found

	TraceDqr::DQErr rc;

	for (;;) {
		// get next record

		rc = parseNextVectorRecord(QStart);	// reads a record and adds it to the Q (adds five entries to the Q. Packs the Q if needed
		if (rc != TraceDqr::DQERR_OK) {
			status = rc;
			return rc;
		}

		while (QStart != traceQIn) {
			if ((caTraceQ[QStart].record & TraceDqr::CAVFLAG_V0) != 0) {
				pipe = TraceDqr::CAFLAG_PIPE0;
				cycles = caTraceQ[QStart].cycle;
				caTraceQ[QStart].record &= ~TraceDqr::CAVFLAG_V0;

//				QStart += 1;
//				if (QStart >= traceQSize) {
//					QStart = 0;
//				}

				return TraceDqr::DQERR_OK;
			}

			if ((caTraceQ[QStart].record & TraceDqr::CAVFLAG_V1) != 0) {
				pipe = TraceDqr::CAFLAG_PIPE1;
				cycles = caTraceQ[QStart].cycle;
				caTraceQ[QStart].record &= ~TraceDqr::CAVFLAG_V1;

//				QStart += 1;
//				if (QStart >= traceQSize) {
//					QStart = 0;
//				}

				return TraceDqr::DQERR_OK;
			}

			QStart += 1;
			if (QStart >= traceQSize) {
				QStart = 0;
			}
		}
	}

	return TraceDqr::DQERR_ERR;
}

TraceDqr::DQErr CATrace::consumeCAVector(int &QStart,TraceDqr::CAVectorTraceFlags type,uint32_t &cycles,uint8_t &qInfo,uint8_t &arithInfo,uint8_t &loadInfo, uint8_t &storeInfo)
{
	if (caTraceQ == nullptr) {
		return TraceDqr::DQERR_ERR;
	}

	// first look for pipe info in Q

	// look in Q and see if record with matching type is found

	TraceDqr::DQErr rc;

	// QStart will be either traceQOut or after traceQOut

	if (QStart == traceQIn) {
		// get next record

		rc = parseNextVectorRecord(QStart);	// reads a record and adds it to the Q (adds five entries to the Q. Packs the Q if needed
		if (rc != TraceDqr::DQERR_OK) {
			status = rc;

			return rc;
		}
	}

	// we want the stats below at the beginning of the Q, not the end, so we grab them here!

	uint8_t tQInfo = caTraceQ[QStart].qDepth;
	uint8_t tArithInfo = caTraceQ[QStart].arithInProcess;
	uint8_t tLoadInfo = caTraceQ[QStart].loadInProcess;
	uint8_t tStoreInfo = caTraceQ[QStart].storeInProcess;

	while (QStart != traceQIn) {
		switch (type) { // type is what we are looking for. When we find a VISTART in the Q, it means one was removed from the Q!
		case TraceDqr::CAVFLAG_VISTART:
			caTraceQ[QStart].qDepth += 1;
			break;
		case TraceDqr::CAVFLAG_VIARITH:
			caTraceQ[QStart].arithInProcess += 1;
			break;
		case TraceDqr::CAVFLAG_VISTORE:
			caTraceQ[QStart].storeInProcess += 1;
			break;
		case TraceDqr::CAVFLAG_VILOAD:
			caTraceQ[QStart].loadInProcess += 1;
			break;
		default:
			printf("Error: CATrace::consumeCAVector(): invalid type: %08x\n",type);
			return TraceDqr::DQERR_ERR;
		}

		if ((caTraceQ[QStart].record & type) != 0) { // found what we were looking for in the q
			cycles = caTraceQ[QStart].cycle;
			caTraceQ[QStart].record &= ~type;

			switch (type) {
			case TraceDqr::CAVFLAG_VISTART:
				tQInfo += 1;
				break;
			case TraceDqr::CAVFLAG_VIARITH:
				tArithInfo += 1;
				break;
			case TraceDqr::CAVFLAG_VISTORE:
				tStoreInfo += 1;
				break;
			case TraceDqr::CAVFLAG_VILOAD:
				tLoadInfo += 1;
				break;
			default:
				printf("Error: CATrace::consumeCAVector(): invalid type: %08x\n",type);
				return TraceDqr::DQERR_ERR;
			}

			qInfo = tQInfo;
			arithInfo = tArithInfo;
			loadInfo = tLoadInfo;
			storeInfo = tStoreInfo;

			QStart += 1;
			if (QStart >= traceQSize) {
				QStart = 0;
			}

			return TraceDqr::DQERR_OK;
		}

		QStart += 1;

		if (QStart >= traceQSize) {
			QStart = 0;
		}
	}


	// otherwise, start reading records and adding them to the Q until
	// matching type is found

	for (;;) {
		// get next record

		rc = parseNextVectorRecord(QStart);	// reads a record and adds it to the Q (adds five entries to the Q. Packs the Q if needed
		if (rc != TraceDqr::DQERR_OK) {
			status = rc;

			return rc;
		}

		while (QStart != traceQIn) {
			switch (type) {
			case TraceDqr::CAVFLAG_VISTART:
				caTraceQ[QStart].qDepth += 1;
				break;
			case TraceDqr::CAVFLAG_VIARITH:
				caTraceQ[QStart].arithInProcess += 1;
				break;
			case TraceDqr::CAVFLAG_VISTORE:
				caTraceQ[QStart].storeInProcess += 1;
				break;
			case TraceDqr::CAVFLAG_VILOAD:
				caTraceQ[QStart].loadInProcess += 1;
				break;
			default:
				printf("Error: CATrace::consumeCAVector(): invalid type: %08x\n",type);
				return TraceDqr::DQERR_ERR;
			}

			if ((caTraceQ[QStart].record & type) != 0) {
				cycles = caTraceQ[QStart].cycle;
				caTraceQ[QStart].record &= ~type;

				switch (type) {
				case TraceDqr::CAVFLAG_VISTART:
					tQInfo += 1;
					break;
				case TraceDqr::CAVFLAG_VIARITH:
					tArithInfo += 1;
					break;
				case TraceDqr::CAVFLAG_VISTORE:
					tStoreInfo += 1;
					break;
				case TraceDqr::CAVFLAG_VILOAD:
					tLoadInfo += 1;
					break;
				default:
					printf("Error: CATrace::consumeCAVector(): invalid type: %08x\n",type);
					return TraceDqr::DQERR_ERR;
				}

				qInfo = tQInfo;
				arithInfo = tArithInfo;
				loadInfo = tLoadInfo;
				storeInfo = tStoreInfo;

				QStart += 1;
				if (QStart >= traceQSize) {
					QStart = 0;
				}

				return TraceDqr::DQERR_OK;
			}

			QStart += 1;

			if (QStart >= traceQSize) {
				QStart = 0;
			}
		}
	}

	return TraceDqr::DQERR_ERR;
}

void CATrace::dumpCAQ()
{
	printf("dumpCAQ(): traceQSize: %d traceQOut: %d traceQIn: %d\n",traceQSize,traceQOut,traceQIn);

	for (int i = traceQOut; i != traceQIn;) {
		printf("Q[%d]: %4d %02x",i,caTraceQ[i].cycle,caTraceQ[i].record);

		if (caTraceQ[i].record & TraceDqr::CAVFLAG_V0) {
			printf(" V0");
		}
		else {
			printf("   ");
		}

		if (caTraceQ[i].record & TraceDqr::CAVFLAG_V1) {
			printf(" V1");
		}
		else {
			printf("   ");
		}

		if (caTraceQ[i].record & TraceDqr::CAVFLAG_VISTART) {
			printf(" VISTART");
		}
		else {
			printf("        ");
		}

		if (caTraceQ[i].record & TraceDqr::CAVFLAG_VIARITH) {
			printf(" VIARITH");
		}
		else {
			printf("         ");
		}

		if (caTraceQ[i].record & TraceDqr::CAVFLAG_VISTORE) {
			printf(" VSTORE");
		}
		else {
			printf("       ");
		}

		if (caTraceQ[i].record & TraceDqr::CAVFLAG_VILOAD) {
			printf(" VLOAD\n");
		}
		else {
			printf("       \n");
		}

		i += 1;
		if (i >= traceQSize) {
			i = 0;
		}
	}
}

TraceDqr::DQErr CATrace::consume(uint32_t &caFlags,TraceDqr::InstType iType,uint32_t &pipeCycles,uint32_t &viStartCycles,uint32_t &viFinishCycles,uint8_t &qDepth,uint8_t &arithDepth,uint8_t &loadDepth,uint8_t &storeDepth)
{
	int qStart;

	TraceDqr::DQErr rc;

//	printf("CATrace::consume()\n");

	if (status != TraceDqr::DQERR_OK) {
		return status;
	}

	uint8_t tQDepth;
	uint8_t tArithDepth;
	uint8_t tLoadDepth;
	uint8_t tStoreDepth;

	switch (caType) {
	case TraceDqr::CATRACE_NONE:
		printf("Error: CATrace::consume(): invalid trace type CATRACE_NONE\n");
		return TraceDqr::DQERR_ERR;
	case TraceDqr::CATRACE_INSTRUCTION:
		rc = consumeCAInstruction(caFlags,pipeCycles);
		if (rc != TraceDqr::DQERR_OK) {
			status = rc;
			return rc;
		}

		qDepth = 0;
		arithDepth = 0;
		loadDepth = 0;
		storeDepth = 0;
		break;
	case TraceDqr::CATRACE_VECTOR:
		// get pipe

		qStart = traceQOut;

		rc = consumeCAPipe(qStart,pipeCycles,caFlags);
		if (rc != TraceDqr::DQERR_OK) {
			status = rc;
			return status;
		}

		switch(iType) {
		case TraceDqr::INST_VECT_ARITH:
			rc = consumeCAVector(qStart,TraceDqr::CAVFLAG_VISTART,viStartCycles,qDepth,tArithDepth,tLoadDepth,tStoreDepth);
			if (rc != TraceDqr::DQERR_OK) {
				status = rc;
				return rc;
			}

			rc = consumeCAVector(qStart,TraceDqr::CAVFLAG_VIARITH,viFinishCycles,tQDepth,arithDepth,loadDepth,storeDepth);
			if (rc != TraceDqr::DQERR_OK) {
				status = rc;
				return rc;
			}

			caFlags |= TraceDqr::CAFLAG_VSTART | TraceDqr::CAFLAG_VARITH;

			if (globalDebugFlag) {
				printf("CATrace::consume(): INST_VECT_ARITH consumed vector instruction. Current qStart: %d traceQOut: %d traceQIn: %d\n",qStart,traceQOut,traceQIn);
				printf("vector: viFinishCycles: %d\n",viFinishCycles);
				dumpCAQ();
			}
			break;
		case TraceDqr::INST_VECT_LOAD:
			rc = consumeCAVector(qStart,TraceDqr::CAVFLAG_VISTART,viStartCycles,qDepth,tArithDepth,tLoadDepth,tStoreDepth);
			if (rc != TraceDqr::DQERR_OK) {
				status = rc;
				return rc;
			}

			rc = consumeCAVector(qStart,TraceDqr::CAVFLAG_VILOAD,viFinishCycles,tQDepth,arithDepth,loadDepth,storeDepth);
			if (rc != TraceDqr::DQERR_OK) {
				status = rc;
				return rc;
			}

			caFlags |= TraceDqr::CAFLAG_VSTART | TraceDqr::CAFLAG_VLOAD;

			if (globalDebugFlag) {
				printf("CATrace::consume(): INST_VECT_LOAD consumed vector instruction. Current qStart: %d traceQOut: %d traceQIn: %d\n",qStart,traceQOut,traceQIn);
				printf("vector: viFinishCycles: %d\n",viFinishCycles);
				dumpCAQ();
			}
			break;
		case TraceDqr::INST_VECT_STORE:
			rc = consumeCAVector(qStart,TraceDqr::CAVFLAG_VISTART,viStartCycles,qDepth,tArithDepth,tLoadDepth,tStoreDepth);
			if (rc != TraceDqr::DQERR_OK) {
				status = rc;
				return rc;
			}

			rc = consumeCAVector(qStart,TraceDqr::CAVFLAG_VISTORE,viFinishCycles,tQDepth,arithDepth,loadDepth,storeDepth);
			if (rc != TraceDqr::DQERR_OK) {
				status = rc;
				return rc;
			}

			caFlags |= TraceDqr::CAFLAG_VSTART | TraceDqr::CAFLAG_VSTORE;

			if (globalDebugFlag) {
				printf("CATrace::consume(): INST_VECT_STORE consumed vector instruction. Current qStart: %d traceQOut: %d traceQIn: %d\n",qStart,traceQOut,traceQIn);
				printf("vector: viFinishCycles: %d\n",viFinishCycles);
				dumpCAQ();
			}
			break;
		case TraceDqr::INST_VECT_AMO:
			rc = consumeCAVector(qStart,TraceDqr::CAVFLAG_VISTART,viStartCycles,qDepth,tArithDepth,tLoadDepth,tStoreDepth);
			if (rc != TraceDqr::DQERR_OK) {
				status = rc;
				return rc;
			}

			rc = consumeCAVector(qStart,TraceDqr::CAVFLAG_VILOAD,viFinishCycles,tQDepth,arithDepth,loadDepth,storeDepth);
			if (rc != TraceDqr::DQERR_OK) {
				status = rc;
				return rc;
			}

			caFlags |= TraceDqr::CAFLAG_VSTART | TraceDqr::CAFLAG_VLOAD;

			if (globalDebugFlag) {
				printf("CATrace::consume(): INST_VECT_AMO consumed vector instruction. Current qStart: %d traceQOut: %d traceQIn: %d\n",qStart,traceQOut,traceQIn);
				printf("vector: viFinishCycles: %d\n",viFinishCycles);
				dumpCAQ();
			}
			break;
		case TraceDqr::INST_VECT_AMO_WW:
			rc = consumeCAVector(qStart,TraceDqr::CAVFLAG_VISTART,viStartCycles,qDepth,tArithDepth,tLoadDepth,tStoreDepth);
			if (rc != TraceDqr::DQERR_OK) {
				status = rc;
				return rc;
			}

			rc = consumeCAVector(qStart,TraceDqr::CAVFLAG_VILOAD,viFinishCycles,tQDepth,arithDepth,loadDepth,storeDepth);
			if (rc != TraceDqr::DQERR_OK) {
				status = rc;
				return rc;
			}

			rc = consumeCAVector(qStart,TraceDqr::CAVFLAG_VISTORE,viFinishCycles,tQDepth,tArithDepth,tLoadDepth,tStoreDepth);
			if (rc != TraceDqr::DQERR_OK) {
				status = rc;
				return rc;
			}

			caFlags |= TraceDqr::CAFLAG_VSTART | TraceDqr::CAFLAG_VLOAD | TraceDqr::CAFLAG_VSTORE;

			if (globalDebugFlag) {
				printf("CATrace::consume(): INST_VECT_AMO consumed vector instruction. Current qStart: %d traceQOut: %d traceQIn: %d\n",qStart,traceQOut,traceQIn);
				printf("vector: viFinishCycles: %d\n",viFinishCycles);
				dumpCAQ();
			}
			break;
		case TraceDqr::INST_VECT_CONFIG:
			break;
		default:
			break;
		}

		// update traceQOut for vector traces

		while ((caTraceQ[traceQOut].record == 0) && (traceQOut != traceQIn)) {
			traceQOut += 1;
			if (traceQOut >= traceQSize) {
				traceQOut = 0;
			}
		}
		break;
	}

	return TraceDqr::DQERR_OK;
}

TraceDqr::ADDRESS CATrace::getCATraceStartAddr()
{
	// of course it ins't this simple. If bit 29 in data[0] is 0, the start address is actually the address of the next
	// instruction! To compute that, we must know the size of the instruction at the reported address. And to make things
	// worse, if that instruction is a conditional branch or an indirect jump (like a return), we can't compute the next
	// address because there is not instruction trace info for that instruction!

	return startAddr;
}

TraceDqr::DQErr CATrace::parseNextCATraceRec(CATraceRec &car)
{
	// Reload all 32 catr.data[] records from the raw caBuffer[] data. Update caBufferIndex to start of next record in raw data
	// Works for CAInstruction and CAVector traces

	// needs to update offset and blockRecordNum as well!

	if (status != TraceDqr::DQERR_OK) {
		return status;
	}

	if ((int)caBufferIndex > (int)(caBufferSize - sizeof(uint32_t))) {
		status = TraceDqr::DQERR_EOF;
		return TraceDqr::DQERR_EOF;
	}

	uint32_t d = 0;
	bool firstRecord;

	if (caBufferIndex == 0) {
		// find start of first message (in case buffer wrapped)
		uint32_t last;

		firstRecord = true;

		do {
			last = d >> 30;
			d = *(uint32_t*)(&caBuffer[caBufferIndex]);
			caBufferIndex += sizeof(uint32_t);

			if ((int)caBufferIndex > (int)(caBufferSize - sizeof(uint32_t))) {
				status = TraceDqr::DQERR_EOF;
				return TraceDqr::DQERR_EOF;
			}
		} while (((d >> 30) != 0x3) && (last != 0));
	}
	else {
		firstRecord = false;

		// need to get first word into d
		d = *(uint32_t*)(&caBuffer[caBufferIndex]);
		caBufferIndex += sizeof(uint32_t);
	}

	// make sure there are at least 31 more 32 bit records in the caBuffer. If not, EOF

	if ((int)caBufferIndex > (int)(caBufferSize - sizeof(uint32_t)*31)) {
		return TraceDqr::DQERR_EOF;
	}

	TraceDqr::ADDRESS addr;
	addr = 0;

	car.data[0] = d & 0x3fffffff;

	for (int i = 1; i < 32; i++) {
		d = *(uint32_t*)(&caBuffer[caBufferIndex]);
		caBufferIndex += sizeof(uint32_t);

		// don't need to check caBufferIndex for EOF because of the check before for loop

		addr |= (((TraceDqr::ADDRESS)(d >> 30)) << 2*(i-1));
		car.data[i] = d & 0x3fffffff;
	}

	if (firstRecord != false) {
		car.data[0] |= (1<<29); // set the pipe0 finish flag for first bit of trace file (vector or instruction)
		blockRecNum = 0;
	}
	else {
		blockRecNum += 1;
	}

	car.address = addr;
	car.offset = 0;

	return TraceDqr::DQERR_OK;
}

// might need to add binary struct at beginning of metadata file??

static const char * const CTFMetadataHeader =
		"/* CTF 1.8 */\n"
		"\n";

static const char * const CTFMetadataTypeAlias =
		"typealias integer {size = 8; align = 8; signed = false; } := uint8_t;\n"
		"typealias integer {size = 16; align = 8; signed = false; } := uint16_t;\n"
		"typealias integer {size = 32; align = 8; signed = false; } := uint32_t;\n"
		"typealias integer {size = 64; align = 8; signed = false; } := uint64_t;\n"
		"typealias integer {size = 64; align = 8; signed = false; } := unsigned long;\n"
		"typealias integer {size = 5; align = 8; signed = false; } := uint5_t;\n"
		"typealias integer {size = 27; align = 8; signed = false; } := uint27_t;\n"
		"\n";

static const char *const CTFMetadataTraceDef =
		"trace {\n"
		"\tmajor = 1;\n"
		"\tminor = 8;\n"
		"\tbyte_order = le;\n"
		"\tpacket.header := struct {\n"
		"\t\tuint32_t magic;\n"
		"\t\tuint32_t stream_id;\n"
		"\t};\n"
		"};\n"
		"\n";

static const char * const CTFMetadataEnvDef =
		"env {\n"
			"\tdomain = \"ust\";\n"
			"\ttracer_name = \"lttng-ust\";\n"
			"\ttracer_major = 2;\n"
			"\ttracer_minor = 11;\n"
			"\ttracer_buffering_scheme = \"uid\";\n"
			"\ttracer_buffering_id = 1000;\n"
			"\tarchitecture_bit_width = %d;\n"
			"\ttrace_name = \"%s\";\n"
			"\ttrace_creation_datetime = \"%s\";\n"
			"\thostname = \"%s\";\n"
		"};\n"
		"\n";

static const char * const CTFMetadataClockDef =
		"clock {\n"
			"\tname = \"monotonic\";\n"
			"\tuuid = \"cb35f5a5-f0a6-441f-b5c7-c7fb50c2e051\";\n"
			"\tdescription = \"Monotonic Clock\";\n"
			"\tfreq = %d; /* Frequency, in Hz */\n"
			"\t/* clock value offset from Epoch is: offset * (1/freq) */\n"
			"\toffset = %lld;\n"
		"};\n"
		"\n"
		"typealias integer {\n"
			"\tsize = 27; align = 1; signed = false;\n"
			"\tmap = clock.monotonic.value;\n"
		"} := uint27_clock_monotonic_t;\n"
		"\n"
		"typealias integer {\n"
			"\tsize = 32; align = 8; signed = false;\n"
			"\tmap = clock.monotonic.value;\n"
		"} := uint32_clock_monotonic_t;\n"
		"\n"
		"typealias integer {\n"
			"\tsize = 64; align = 8; signed = false;\n"
			"\tmap = clock.monotonic.value;\n"
		"} := uint64_clock_monotonic_t;\n"
		"\n";

static const char * const CTFMetadataPacketContext =
		"struct packet_context {\n"
			"\tuint64_clock_monotonic_t timestamp_begin;\n"
			"\tuint64_clock_monotonic_t timestamp_end;\n"
			"\tuint64_t content_size;\n"
			"\tuint64_t packet_size;\n"
			"\tuint64_t packet_seq_num;\n"
			"\tunsigned long events_discarded;\n"
			"\tuint32_t cpu_id;\n"
		"};\n"
		"\n";

static const char * const CTFMetadataEventHeaders =
		"struct event_header_compact {\n"
			"\tenum : uint5_t { compact = 0 ... 30, extended = 31 } id;\n"
			"\tvariant <id> {\n"
				"\t\tstruct {\n"
					"\t\t\tuint27_clock_monotonic_t timestamp;\n"
				"\t\t} compact;\n"
				"\t\tstruct {\n"
					"\t\t\tuint32_t id;\n"
					"\t\t\tuint64_clock_monotonic_t timestamp;\n"
				"\t\t} extended;\n"
			"\t} v;\n"
		"} align(8);\n"
		"\n"
		"struct event_header_large {\n"
			"\tenum : uint16_t { compact = 0 ... 65534, extended = 65535 } id;\n"
			"\tvariant <id> {\n"
				"\t\tstruct {\n"
					"\t\t\tuint32_clock_monotonic_t timestamp;\n"
				"\t\t} compact;\n"
				"\t\tstruct {\n"
					"\t\t\tuint32_t id;\n"
					"\t\t\tuint64_clock_monotonic_t timestamp;\n"
				"\t\t} extended;\n"
			"\t} v;\n"
		"} align(8);\n"
		"\n";

static const char * const CTFMetadataStreamDef =
		"stream {\n"
			"\tid = 0;\n"
			"\tevent.header := struct event_header_large;\n"
			"\tpacket.context := struct packet_context;\n"
			"\tevent.context := struct {\n"
				"\t\tinteger { size = 32; align = 8; signed = 1; encoding = none; base = 10; } _vpid;\n"
				"\t\tinteger { size = 32; align = 8; signed = 1; encoding = none; base = 10; } _vtid;\n"
				"\t\tinteger { size = 8; align = 8; signed = 1; encoding = UTF8; base = 10; } _procname[17];\n"
			"\t};\n"
		"};\n"
		"\n";

static const char * const CTFMetadataCallEventDef =
		"event {\n"
			"\tname = \"lttng_ust_cyg_profile:func_entry\";\n"
			"\tid = 1;\n"
			"\tstream_id = 0;\n"
			"\tloglevel = 12;\n"
			"\tfields := struct {\n"
				"\t\tinteger { size = 64; align = 8; signed = 0; encoding = none; base = 16; } _addr;\n"
				"\t\tinteger { size = 64; align = 8; signed = 0; encoding = none; base = 16; } _call_site;\n"
			"\t};\n"
			"};\n"
		"\n";

static const char * const CTFMetadataRetEventDef =
		"event {\n"
			"\tname = \"lttng_ust_cyg_profile:func_exit\";\n"
			"\tid = 2;\n"
			"\tstream_id = 0;\n"
			"\tloglevel = 12;\n"
			"\tfields := struct {\n"
				"\t\tinteger { size = 64; align = 8; signed = 0; encoding = none; base = 16; } _addr;\n"
				"\t\tinteger { size = 64; align = 8; signed = 0; encoding = none; base = 16; } _call_site;\n"
			"\t};\n"
		"};\n"
		"\n";

static const char * const CTFMetadataStatedumpStart =
		"event {\n"
			"\tname = \"lttng_ust_statedump:start\";\n"
			"\tid = 3;\n"
			"\tstream_id = 0;\n"
		    "\tloglevel = 13;\n"
			"\tfields := struct {\n"
			"\t};\n"
		"};\n"
		"\n";

static const char * const CTFMetadataStatedumpBinInfo =
		"event {\n"
			"\tname = \"lttng_ust_statedump:bin_info\";\n"
			"\tid = 4;\n"
			"\tstream_id = 0;\n"
		    "\tloglevel = 13;\n"
			"\tfields := struct {\n"
				"\t\tinteger { size = 64; align = 8; signed = 0; encoding = none; base = 16; } _baddr;\n"
				"\t\tinteger { size = 64; align = 8; signed = 0; encoding = none; base = 10; } _memsz;\n"
				"\t\tstring _path;\n"
				"\t\tinteger { size = 8; align = 8; signed = 0; encoding = none; base = 10; } _is_pic;\n"
				"\t\tinteger { size = 8; align = 8; signed = 0; encoding = none; base = 10; } _has_build_id;\n"
				"\t\tinteger { size = 8; align = 8; signed = 0; encoding = none; base = 10; } _has_debug_link;\n"
			"\t};\n"
		"};\n"
		"\n";

static const char * const CTFMetadataStatedumpEnd =
		"event {\n"
			"\tname = \"lttng_ust_statedump:end\";\n"
			"\tid = 7;\n"
			"\tstream_id = 0;\n"
		    "\tloglevel = 13;\n"
			"\tfields := struct {\n"
			"\t};\n"
		"};\n"
		"\n";

static char CTFMetadataEnvDefDoctored[1024];
static char CTFMetadataClockDefDoctored[1024];

static const char * const CTFMetadataStructs[] = {
		CTFMetadataHeader,
		CTFMetadataTypeAlias,
		CTFMetadataTraceDef,
		CTFMetadataEnvDefDoctored,
		CTFMetadataClockDefDoctored, // need to put freq in string!
		CTFMetadataPacketContext,
		CTFMetadataEventHeaders,
		CTFMetadataStreamDef,
		CTFMetadataCallEventDef,
		CTFMetadataRetEventDef,
		CTFMetadataStatedumpStart,
		CTFMetadataStatedumpBinInfo,
		CTFMetadataStatedumpEnd
};

// class CTFConverter methods

// baseNameIn: abs or rel path and name of base file
// fileBaseName: name of file without extention (no path info)
// fileName: just the name of the file at baseNameIn (no path info)
// absPath: abs path to baseNameIn without the file (just the abs path)

static void getPathsNames(char *baseNameIn,char *fileBaseName,char *fileName,char *absPath)
{
	if (fileBaseName != nullptr) {
		fileBaseName[0] = 0;
	}

	if (fileName != nullptr) {
		fileName[0] = 0;
	}

	if (absPath != nullptr) {
		absPath[0] = 0;
	}

	if (baseNameIn == nullptr) {
		return;
	}

	// find elf name by scanning backwords for path separator

	int l;
	int i;

	l = strlen(baseNameIn);

	int sepIndex = -1;
	int extIndex = -1;

	for (i = l-1; (i >= 0) && (sepIndex == -1); i--) {
		if (baseNameIn[i] == '/') {
			sepIndex = i;
		}
		else if (baseNameIn[i] == '\\') {
			sepIndex = i;
		}
		else if ((extIndex == -1) && (baseNameIn[i] == '.')) {
			extIndex = i;
		}
	}

	char en[256];
	char ebn[256];
	char fullPath[512];

	bool haveAbsPath = false;

	strcpy(en,&baseNameIn[sepIndex+1]);

	for (i = sepIndex+1; i < extIndex; i++) {
		ebn[i - (sepIndex+1)] = baseNameIn[i];
	}

	ebn[i - (sepIndex+1)] = 0;

	// figure out if this is an abs path or a rel path in baseNameIn

	if (baseNameIn[0] == '/') {
		haveAbsPath = true;
	}
	else if (baseNameIn[0] == '\\') {
		haveAbsPath = true;
	}
	else if ((baseNameIn[0] != 0) && (baseNameIn[1] == ':') && (baseNameIn[2] == '\\')) {
		haveAbsPath = true;
	}

	if (haveAbsPath == false) {
		// have a rel path. Need to make it an abs path

#ifdef	WINDOWS
		_getcwd(fullPath,sizeof fullPath);
		char pathSep = '\\';
#else	// WINDOWS
		getcwd(fullPath,sizeof fullPath);
		char pathSep = '/';
#endif	// WINDOWS

		l = strlen(fullPath);
		fullPath[l] = pathSep;
		l += 1;

		for (i = 0; i < sepIndex+1; i++) {
			fullPath[l+i] = baseNameIn[i];
		}

		fullPath[l+i] = 0;
	}
	else {
		for (i = 0; i < sepIndex+1; i++) {
			fullPath[i] = baseNameIn[i];
		}

		fullPath[i] = 0;
	}

	if (fileBaseName != nullptr) {
		strcpy(fileBaseName,ebn);
	}

	if (fileName != nullptr) {
		strcpy(fileName,en);
	}

	if (absPath != nullptr) {
		strcpy(absPath,fullPath);
	}

//	printf("baseNameIn: %s\n",baseNameIn);
//	printf("fileBaseName: %s\n",fileBaseName);
//	printf("fileName: %s\n",fileName);
//	printf("absPath: %s\n",absPath);
}

EventConverter::EventConverter(char *elf,char *rtd,int numCores,uint32_t freq)
{
	for (int i = 0; i < (int)(sizeof eventFDs / sizeof eventFDs[0]); i++) {
		eventFDs[i] = -1;
	}

	this->numCores = numCores;
	frequency = freq;

	char elfBaseName[256];
	int eventNameLen;
	char nameBuff[512];


	getPathsNames(elf,elfBaseName,nullptr,nullptr);

	getPathsNames(rtd,nullptr,nullptr,eventNameGen);

	sprintf(nameBuff,"%s%s.events",eventNameGen,elfBaseName);

#ifdef WINDOWS
	eventFD = open(nameBuff,O_WRONLY | O_CREAT | O_TRUNC | O_BINARY,S_IRUSR | S_IWUSR);
#else // WINDOWS
	eventFD = open(nameBuff,O_WRONLY | O_CREAT | O_TRUNC,S_IRUSR | S_IWUSR);
#endif // WINDOWS

	if (eventFD < 0) {
		printf("Error: EventConverter::EventConverter(): Couldn't open file %s for writing\n",nameBuff);
		status = TraceDqr::DQERR_ERR;

		return;
	}

	strcat(eventNameGen,"events");

	// make the event folder

	int rc;

#ifdef WINDOWS
	rc = mkdir(eventNameGen);
	char pathSep = '\\';
#else // WINDOWS
	rc = mkdir(eventNameGen,0775);
	char pathSep = '/';
#endif // WINDOWS

	if ((rc < 0) && (errno != EEXIST)){
		printf("Error: EventConverter::EventConverter(): Couldn't not make directory %s, errono=%d\n",eventNameGen,errno);
		status = TraceDqr::DQERR_ERR;

		return;
	}

	// now add the elf file base name

	eventNameLen = strlen(eventNameGen);
	eventNameGen[eventNameLen] = pathSep;
	eventNameLen += 1;

	strcpy(&eventNameGen[eventNameLen],elfBaseName);
	strcat(&eventNameGen[eventNameLen],".%s");

	status = TraceDqr::DQERR_OK;
}

EventConverter::~EventConverter()
{
	for (int i = 0; i < (int)(sizeof eventFDs / sizeof eventFDs[0]); i++) {
		if (eventFDs[i] >= 0) {
			close(eventFDs[i]);
			eventFDs[i] = -1;
		}
	}

	if (eventFD >= 0) {
		close(eventFD);
		eventFD = -1;
	}
}

TraceDqr::DQErr EventConverter::emitExtTrigEvent(int core,TraceDqr::TIMESTAMP ts,int ckdf,TraceDqr::ADDRESS pc,int id)
{
	char msgBuff[512];
	int n;

	if (eventFDs[CTF::et_extTriggerIndex] < 0) {
		sprintf(msgBuff,eventNameGen,"trigger");

#ifdef WINDOWS
		eventFDs[CTF::et_extTriggerIndex] = open(msgBuff,O_WRONLY | O_CREAT | O_TRUNC | O_BINARY,S_IRUSR | S_IWUSR);
#else // WINDOWS
		eventFDs[CTF::et_extTriggerIndex] = open(msgBuff,O_WRONLY | O_CREAT | O_TRUNC,S_IRUSR | S_IWUSR);
#endif // WINDOWS

		if (eventFDs[CTF::et_extTriggerIndex] < 0) {
			printf("Error: EventConverter::emitExtTrigEvent(): Couldn't open file %s for writing\n",msgBuff);
			status = TraceDqr::DQERR_ERR;

			return TraceDqr::DQERR_OK;
		}
	}

	if ((eventFDs[CTF::et_extTriggerIndex] >= 0) || (eventFD >= 0)) {
		if (ckdf == 0) {
			n = snprintf(msgBuff,sizeof msgBuff,"[%d] %d [External Trigger] PC=0x%08x ID=[--]\n",core,ts,pc);
		}
		else {
			n = snprintf(msgBuff,sizeof msgBuff,"[%d] %d [External Trigger] PC=0x%08x ID=[%d]\n",core,ts,pc,id);
		}

		if (eventFD >= 0) {
			write(eventFD,msgBuff,n);
		}

		if (eventFDs[CTF::et_extTriggerIndex] >= 0) {
			write(eventFDs[CTF::et_extTriggerIndex],msgBuff,n);
		}
	}

	return TraceDqr::DQERR_OK;
}

TraceDqr::DQErr EventConverter::emitWatchpoint(int core,TraceDqr::TIMESTAMP ts,int ckdf,TraceDqr::ADDRESS pc,int id)
{
	char msgBuff[512];
	int n;

	if (eventFDs[CTF::et_watchpointIndex] < 0) {
		sprintf(msgBuff,eventNameGen,"watchpoint");

#ifdef WINDOWS
		eventFDs[CTF::et_watchpointIndex] = open(msgBuff,O_WRONLY | O_CREAT | O_TRUNC | O_BINARY,S_IRUSR | S_IWUSR);
#else // WINDOWS
		eventFDs[CTF::et_watchpointIndex] = open(msgBuff,O_WRONLY | O_CREAT | O_TRUNC,S_IRUSR | S_IWUSR);
#endif // WINDOWS

		if (eventFDs[CTF::et_watchpointIndex] < 0) {
			printf("Error: EventConverter::emitWatchpoint(): Couldn't open file %s for writing\n",msgBuff);
			status = TraceDqr::DQERR_ERR;

			return TraceDqr::DQERR_OK;
		}
	}

	if ((eventFDs[CTF::et_watchpointIndex] >= 0) || (eventFD >= 0)) {
		if (ckdf == 0) {
			n = snprintf(msgBuff,sizeof msgBuff,"[%d] %d [Watchpoint] PC=0x%08x ID=[--]\n",core,ts,pc);
		}
		else {
			n = snprintf(msgBuff,sizeof msgBuff,"[%d] %d [Watchpoint] PC=0x%08x ID=[%d]\n",core,ts,pc,id);
		}

		if (eventFD >= 0) {
			write(eventFD,msgBuff,n);
		}

		if (eventFDs[CTF::et_watchpointIndex] >= 0) {
			write(eventFDs[CTF::et_watchpointIndex],msgBuff,n);
		}
	}

	return TraceDqr::DQERR_OK;
}

const char *EventConverter::getExceptionCauseText(int cause)
{
	const char *exceptionCauseMap[] = {
			"Instruction address misaligned",
			"Instruction access fault",
			"Illegal instruction",
			"Breakpoint",
			"Load address misaligned",
			"Load access fault",
			"Store/AMO address misaligned",
			"Store/AMO access fault",
			"Environment call from U-mode",
			"Environment call from S-mode",
			"Reserved",
			"Environment call from M-mode",
			"Instruction page fault",
			"Load page fault",
			"Reserved",
			"Store/AMO page fault",
	};

	if ((cause >= 0) && (cause < (int)(sizeof exceptionCauseMap / sizeof exceptionCauseMap[0]))) {
		return exceptionCauseMap[cause];
	}
	else if (((cause >= 24) && (cause <=31)) || ((cause >= 48) && (cause <= 63))) {
		return "Custom";
	}
	else {
		return "Reserved";
	}
}

const char *EventConverter::getInterruptCauseText(int cause)
{
	const char *eventCauseMap[] = {
			"Reserved",
			"Supervisor software interrupt",
			"Reserved",
			"Machine software interrupt",
			"Reserved",
			"Supervisor timer interrupt",
			"Reserved",
			"Machine timer interrupt",
			"Reserved",
			"Supervisor external interrupt",
			"Reserved",
			"Machine external interrupt"
	};

	if ((cause >= 0) && (cause < (int)(sizeof eventCauseMap / sizeof eventCauseMap[0]))) {
		return eventCauseMap[cause];
	}
	else if (cause >= 16) {
		return "Custom";
	}
	else {
		return "Reserved";
	}
}

TraceDqr::DQErr EventConverter::emitCallRet(int core,TraceDqr::TIMESTAMP ts,int ckdf,TraceDqr::ADDRESS pc,TraceDqr::ADDRESS pcDest,int crFlags)
{
	char msgBuff[512];
	int n;

	if (eventFDs[CTF::et_callRetIndex] < 0) {
		sprintf(msgBuff,eventNameGen,"callret");

#ifdef WINDOWS
		eventFDs[CTF::et_callRetIndex] = open(msgBuff,O_WRONLY | O_CREAT | O_TRUNC | O_BINARY,S_IRUSR | S_IWUSR);
#else // WINDOWS
		eventFDs[CTF::et_callRetIndex] = open(msgBuff,O_WRONLY | O_CREAT | O_TRUNC,S_IRUSR | S_IWUSR);
#endif // WINDOWS

		if (eventFDs[CTF::et_callRetIndex] < 0) {
			printf("Error: EventConverter::emitWatchpoint(): Couldn't open file %s for writing\n",msgBuff);
			status = TraceDqr::DQERR_ERR;

			return TraceDqr::DQERR_OK;
		}
	}

	if ((eventFDs[CTF::et_callRetIndex] >= 0) || (eventFD >= 0)) {
		if (crFlags & TraceDqr::isCall) {
			n = snprintf(msgBuff,sizeof msgBuff,"[%d] %d [Call] PC=0x%08x PCDest=[0x%08x]\n",core,ts,pc,pcDest);
		}
		else if (crFlags & TraceDqr::isInterrupt) {
			n = snprintf(msgBuff,sizeof msgBuff,"[%d] %d [Interrupt] PC=0x%08x PCDest=[0x%08x]\n",core,ts,pc,pcDest);
		}
		else if (crFlags & TraceDqr::isException) {
			n = snprintf(msgBuff,sizeof msgBuff,"[%d] %d [Exception] PC=0x%08x PCDest=[0x%08x]\n",core,ts,pc,pcDest);
		}
		else if (crFlags & TraceDqr::isReturn) {
			n = snprintf(msgBuff,sizeof msgBuff,"[%d] %d [Return] PC=0x%08x PCDest=[0x%08x]\n",core,ts,pc,pcDest);
		}
		else if (crFlags & TraceDqr::isExceptionReturn) {
			n = snprintf(msgBuff,sizeof msgBuff,"[%d] %d [Exception Return] PC=0x%08x PCDest=[0x%08x]\n",core,ts,pc,pcDest);
		}
		else {
			n = snprintf(msgBuff,sizeof msgBuff,"[%d] %d [Call/Return?? PC=0x%08x PCDest=[0x%08x]\n",core,ts,pc,pcDest);
		}

		if (eventFD >= 0) {
			write(eventFD,msgBuff,n);
		}

		if (eventFDs[CTF::et_callRetIndex] >= 0) {
			write(eventFDs[CTF::et_callRetIndex],msgBuff,n);
		}
	}

	return TraceDqr::DQERR_OK;
}

TraceDqr::DQErr EventConverter::emitException(int core,TraceDqr::TIMESTAMP ts,int ckdf,TraceDqr::ADDRESS pc,int cause)
{
	char msgBuff[512];
	int n;

	if (eventFDs[CTF::et_exeptionIndex] < 0) {
		sprintf(msgBuff,eventNameGen,"exception");

#ifdef WINDOWS
		eventFDs[CTF::et_exeptionIndex] = open(msgBuff,O_WRONLY | O_CREAT | O_TRUNC | O_BINARY,S_IRUSR | S_IWUSR);
#else // WINDOWS
		eventFDs[CTF::et_exeptionIndex] = open(msgBuff,O_WRONLY | O_CREAT | O_TRUNC,S_IRUSR | S_IWUSR);
#endif // WINDOWS

		if (eventFDs[CTF::et_exeptionIndex] < 0) {
			printf("Error: EventConverter::emitWatchpoint(): Couldn't open file %s for writing\n",msgBuff);
			status = TraceDqr::DQERR_ERR;

			return TraceDqr::DQERR_OK;
		}
	}

	if ((eventFDs[CTF::et_exeptionIndex] >= 0) || (eventFD >= 0)) {
		n = snprintf(msgBuff,sizeof msgBuff,"[%d] %d [Exception] PC=0x%08x Cause=[%d:%s]\n",core,ts,pc,cause,getExceptionCauseText(cause));

		if (eventFD >= 0) {
			write(eventFD,msgBuff,n);
		}

		if (eventFDs[CTF::et_exeptionIndex] >= 0) {
			write(eventFDs[CTF::et_exeptionIndex],msgBuff,n);
		}
	}

	return TraceDqr::DQERR_OK;
}

TraceDqr::DQErr EventConverter::emitInterrupt(int core,TraceDqr::TIMESTAMP ts,int ckdf,TraceDqr::ADDRESS pc,int cause)
{
	char msgBuff[512];
	int n;

	if (eventFDs[CTF::et_interruptIndex] < 0) {
		sprintf(msgBuff,eventNameGen,"interrupt");

#ifdef WINDOWS
		eventFDs[CTF::et_interruptIndex] = open(msgBuff,O_WRONLY | O_CREAT | O_TRUNC | O_BINARY,S_IRUSR | S_IWUSR);
#else // WINDOWS
		eventFDs[CTF::et_interruptIndex] = open(msgBuff,O_WRONLY | O_CREAT | O_TRUNC,S_IRUSR | S_IWUSR);
#endif // WINDOWS

		if (eventFDs[CTF::et_interruptIndex] < 0) {
			printf("Error: EventConverter::emitWatchpoint(): Couldn't open file %s for writing\n",msgBuff);
			status = TraceDqr::DQERR_ERR;

			return TraceDqr::DQERR_OK;
		}
	}

	if ((eventFDs[CTF::et_interruptIndex] >= 0) || (eventFD >= 0)) {
		n = snprintf(msgBuff,sizeof msgBuff,"[%d] %d [Interrupt] PC=0x%08x Cause=[%d:%s]\n",core,ts,pc,cause,getInterruptCauseText(cause));

		if (eventFD >= 0) {
			write(eventFD,msgBuff,n);
		}

		if (eventFDs[CTF::et_interruptIndex] >= 0) {
			write(eventFDs[CTF::et_interruptIndex],msgBuff,n);
		}
	}

	return TraceDqr::DQERR_OK;
}

TraceDqr::DQErr EventConverter::emitContext(int core,TraceDqr::TIMESTAMP ts,int ckdf,TraceDqr::ADDRESS pc,int context)
{
	char msgBuff[512];
	int n;
	const char *newContext;

	if (eventFDs[CTF::et_contextIndex] < 0) {
		sprintf(msgBuff,eventNameGen,"context");

#ifdef WINDOWS
		eventFDs[CTF::et_contextIndex] = open(msgBuff,O_WRONLY | O_CREAT | O_TRUNC | O_BINARY,S_IRUSR | S_IWUSR);
#else // WINDOWS
		eventFDs[CTF::et_contextIndex] = open(msgBuff,O_WRONLY | O_CREAT | O_TRUNC,S_IRUSR | S_IWUSR);
#endif // WINDOWS

		if (eventFDs[CTF::et_contextIndex] < 0) {
			printf("Error: EventConverter::emitWatchpoint(): Couldn't open file %s for writing\n",msgBuff);
			status = TraceDqr::DQERR_ERR;

			return TraceDqr::DQERR_OK;
		}
	}

	if ((eventFDs[CTF::et_contextIndex] >= 0) || (eventFD >= 0)) {
		switch (context & 0x3) {
		case 2:
			newContext = "SContext";
			break;
		case 3:
			newContext = "MContext";
			break;
		default:
			newContext = "Reserverd Context";
		}

		n = snprintf(msgBuff,sizeof msgBuff,"[%d] %d [%s] PC=0x%08x Context=[%d]\n",core,ts,newContext,pc,context >> 2);

		if (eventFD >= 0) {
			write(eventFD,msgBuff,n);
		}

		if (eventFDs[CTF::et_contextIndex] >= 0) {
			write(eventFDs[CTF::et_contextIndex],msgBuff,n);
		}
	}

	return TraceDqr::DQERR_OK;
}

TraceDqr::DQErr EventConverter::emitPeriodic(int core,TraceDqr::TIMESTAMP ts,int ckdf,TraceDqr::ADDRESS pc)
{
	char msgBuff[512];
	int n;

	if (eventFDs[CTF::et_periodicIndex] < 0) {
		sprintf(msgBuff,eventNameGen,"periodic");

#ifdef WINDOWS
		eventFDs[CTF::et_periodicIndex] = open(msgBuff,O_WRONLY | O_CREAT | O_TRUNC | O_BINARY,S_IRUSR | S_IWUSR);
#else // WINDOWS
		eventFDs[CTF::et_periodicIndex] = open(msgBuff,O_WRONLY | O_CREAT | O_TRUNC,S_IRUSR | S_IWUSR);
#endif // WINDOWS

		if (eventFDs[CTF::et_periodicIndex] < 0) {
			printf("Error: EventConverter::emitWatchpoint(): Couldn't open file %s for writing\n",msgBuff);
			status = TraceDqr::DQERR_ERR;

			return TraceDqr::DQERR_OK;
		}
	}

	if ((eventFDs[CTF::et_periodicIndex] >= 0) || (eventFD >= 0)) {
		n = snprintf(msgBuff,sizeof msgBuff,"[%d] %d [PC Sample] PC=0x%08x 0=[0]\n",core,ts,pc);

		if (eventFD >= 0) {
			write(eventFD,msgBuff,n);
		}

		if (eventFDs[CTF::et_periodicIndex] >= 0) {
			write(eventFDs[CTF::et_periodicIndex],msgBuff,n);
		}
	}

	return TraceDqr::DQERR_OK;
}

const char *EventConverter::getControlText(int control)
{
	const char *controlMap[] = {
			"No Event",
			"Reserved",
			"Trace On",
			"Trace Off",
			"Exit Debug",
			"Enter Debug",
			"Exit Reset",
			"Enter Reset"
	};

	if ((control >= 0) && (control < (int)(sizeof controlMap / sizeof controlMap[0]))) {
		return controlMap[control];
	}

	return "Reserved";
}

TraceDqr::DQErr EventConverter::emitControl(int core,TraceDqr::TIMESTAMP ts,int ckdf,int control,TraceDqr::ADDRESS pc)
{
	char msgBuff[512];
	int n;

	if (eventFDs[CTF::et_controlIndex] < 0) {
		sprintf(msgBuff,eventNameGen,"control");

#ifdef WINDOWS
		eventFDs[CTF::et_controlIndex] = open(msgBuff,O_WRONLY | O_CREAT | O_TRUNC | O_BINARY,S_IRUSR | S_IWUSR);
#else // WINDOWS
		eventFDs[CTF::et_controlIndex] = open(msgBuff,O_WRONLY | O_CREAT | O_TRUNC,S_IRUSR | S_IWUSR);
#endif // WINDOWS

		if (eventFDs[CTF::et_controlIndex] < 0) {
			printf("Error: EventConverter::emitWatchpoint(): Couldn't open file %s for writing\n",msgBuff);
			status = TraceDqr::DQERR_ERR;

			return TraceDqr::DQERR_OK;
		}
	}

	if ((eventFDs[CTF::et_controlIndex] >= 0) || (eventFD >= 0)) {
		if (ckdf == 0) {
			n = snprintf(msgBuff,sizeof msgBuff,"[%d] %d [Control] PC=0x0 Control=[%d:%s]\n",core,ts,control,getControlText(control));
		}
		else {
			n = snprintf(msgBuff,sizeof msgBuff,"[%d] %d [Control] PC=0x%08x Control=[%d:%s]\n",core,ts,pc,control,getControlText(control));
		}

		if (eventFD >= 0) {
				write(eventFD,msgBuff,n);
		}

		if (eventFDs[CTF::et_controlIndex] >= 0) {
			write(eventFDs[CTF::et_controlIndex],msgBuff,n);
		}
	}

	return TraceDqr::DQERR_OK;
}

CTFConverter::CTFConverter(char *elf,char *rtd,int numCores,int arch_size,uint32_t freq,int64_t t,char *hostName)
{
	status = TraceDqr::DQERR_OK;

	archSize = arch_size;

	bool tFlag;

	if (t == -1) {
		tFlag = true;
	}
	else {
		tFlag = false;
	}

	for (int i = 0; i < (int)(sizeof eventIndex / sizeof eventIndex[0]); i++) {
		eventIndex[i] = 0;
	}

	for (int i = 0; i < (int)(sizeof eventBuffer / sizeof eventBuffer[0]); i++) {
		eventBuffer[i] = nullptr;
	}

	for (int i = 0; i < (int)(sizeof fd / sizeof fd[0]); i++) {
		fd[i] = -1;
	}

	metadataFd = -1;

	for (int i = 0; i < (int)(sizeof headerFlag / sizeof headerFlag[0]); i++) {
		headerFlag[i] = false;
	}

	packetSeqNum = 0;
	if (freq == 0) {
		frequency = 1000000000;
	}
	else {
		frequency = freq;
	}

	this->elfName = nullptr;

	// fill in the uuid member

	if (elf == nullptr) {
		status = TraceDqr::DQERR_ERR;
		return;
	}

	if (numCores > DQR_MAXCORES) {
		status = TraceDqr::DQERR_ERR;
		return;
	}

	this->numCores = numCores;

	for (int i = 0; i < DQR_MAXCORES; i++) {
		eventContext[i]._vpid = 1; // don't use 0 for vpid; trace compasss uses 0 for kernel processes and does symbol resolution differently
		eventContext[i]._vtid = i;
		for (int j = 0; j < (int)(sizeof eventContext[i]._procname / sizeof eventContext[i]._procname[0]); j++) {
			eventContext[i]._procname[j] = 0;
		}
	}

	char elfBaseName[256];
	char elfName[256];
	char elfPath[512];

	getPathsNames(elf,elfBaseName,elfName,elfPath);

	// set this->elfName to the complete path and elf name

	this->elfName = new char[strlen(elfPath)+strlen(elfName)+1];
	strcpy(this->elfName,elfPath);
	strcat(this->elfName,elfName);

//	printf("full elf name: %s\n",this->elfName);

	char ctfNameGen[512];
	int ctfNameLen;

	getPathsNames(rtd,nullptr,nullptr,ctfNameGen);

	strcat(ctfNameGen,"ctf");

	// make the ctf folder

	int rc;

#ifdef WINDOWS
	rc = mkdir(ctfNameGen);
	char pathSep = '\\';
#else // WINDOWS
	rc = mkdir(ctfNameGen,0775);
	char pathSep = '/';
#endif // WINDOWS

	if ((rc < 0) && (errno != EEXIST)) {
		printf("Error: EventConverter::CTFConveter(): Couldn't not make directory %s, errono=%d\n",ctfNameGen,errno);
		status = TraceDqr::DQERR_ERR;

		return;
	}

	// now add the elf file base name

	ctfNameLen = strlen(ctfNameGen);
	ctfNameGen[ctfNameLen] = pathSep;
	ctfNameLen += 1;

	strcpy(&ctfNameGen[ctfNameLen],elfBaseName);
	strcat(&ctfNameGen[ctfNameLen],"_%d.ctf");

	char nameBuff[512];

	for (int i = 0; i < numCores; i++) {
		sprintf(nameBuff,ctfNameGen,i);

//		printf("nameBuff %d: %s\n",i,nameBuff);

#ifdef WINDOWS
		fd[i] = open(nameBuff,O_WRONLY | O_CREAT | O_TRUNC | O_BINARY,S_IRUSR | S_IWUSR);
#else // WINDOWS
		fd[i] = open(nameBuff,O_WRONLY | O_CREAT | O_TRUNC,S_IRUSR | S_IWUSR);
#endif // WINDOWS

		if (fd[i] < 0) {
			printf("Error: CTFConverter::CTFConverter(): Couldn't open file %s for writing\n",nameBuff);
			status = TraceDqr::DQERR_ERR;

			return;
		}
	}

	// save procname in eventContext struct for all cores

	if (strlen(elfName) < (int)(sizeof eventContext[0]._procname)) {
		for (int i = 0; i < numCores; i++) {
			strcpy((char*)eventContext[i]._procname,elfName);
		}
	}
	else {
		for (int i = 0; i < numCores; i++) {
			strncpy((char*)eventContext[i]._procname,elfName,sizeof eventContext[i]._procname - 1);
		}
	}

	strcpy(&nameBuff[ctfNameLen],"metadata");

#ifdef WINDOWS
	metadataFd = open(nameBuff,O_WRONLY | O_CREAT | O_TRUNC | O_BINARY,S_IRUSR | S_IWUSR);
#else // WINDOWS
	metadataFd = open(nameBuff,O_WRONLY | O_CREAT | O_TRUNC,S_IRUSR | S_IWUSR);
#endif // WINDOWS

	if (metadataFd < 0) {
		printf("Error: CTFConverter::CTFConverter(): Couldn't open file %s for writing\n",nameBuff);
		status = TraceDqr::DQERR_ERR;

		return;
	}

	// time() will return time in seconds, which should be close enough

	struct tm *lt;
	char tbuff[256];

	if (t == -1) {
		t = (int64_t)time(nullptr);
	}

	lt = localtime((time_t*)&t);

	if (tFlag == true) {
		strftime(tbuff,sizeof tbuff,"%Y%m%dT%H%M%S%z",lt);
	}
	else {
		strftime(tbuff,sizeof tbuff,"%Y%m%dT%H%M%S",lt);
	}

	// get hostname

	char hn[256];

	if (hostName == nullptr) {
		int rc;

#ifdef WINDOWS
		WORD wVersionRequested;
		WSADATA wsaData;

		wVersionRequested = MAKEWORD(2,2);
		rc = WSAStartup(wVersionRequested,&wsaData);
		if (rc != 0) {
			printf("Error: CTFConverter::CTFConverter(): WSAStartUP() failed with error %d\n",rc);
			status = TraceDqr::DQERR_ERR;
			return;
		}
#endif // WINDOWS

		rc = gethostname(hn,sizeof hn);
		if (rc != 0) {
			strcpy(hn,"localhost");
		}

#ifdef WINDOWS
		WSACleanup();
#endif // WINDOWS
	}
	else {
		strcpy(hn,hostName);
	}

	// convert seconds to nanoseconds

	sprintf(CTFMetadataClockDefDoctored,CTFMetadataClockDef,frequency,t * 1000000000);
	sprintf(CTFMetadataEnvDefDoctored,CTFMetadataEnvDef,archSize,elfBaseName,tbuff,hn);

	writeCTFMetadata();
	close(metadataFd);
	metadataFd = -1;

	status = TraceDqr::DQERR_OK;
}

CTFConverter::~CTFConverter()
{
	for (int i = 0; i < (int)(sizeof eventIndex / sizeof eventIndex[0]); i++) {
		if (eventIndex[i] > 0) {
			flushEvents(i);
		}
	}

	for (int i = 0; i < (int)(sizeof eventBuffer / sizeof eventBuffer[0]); i++) {
		if (eventBuffer[i] != nullptr) {
			delete [] eventBuffer[i];
			eventBuffer[i] = nullptr;
		}
	}

	for (int i = 0; i < (int)(sizeof fd / sizeof fd[0]); i++) {
		if (fd[i] >= 0) {
			close(fd[i]);
			fd[i] = -1;
		}
	}

	if (elfName != nullptr) {
		delete [] elfName;
		elfName = nullptr;
	}
}

TraceDqr::DQErr CTFConverter::writeCTFMetadata()
{
	for (int i = 0; i < (int)(sizeof CTFMetadataStructs / sizeof CTFMetadataStructs[0]); i++) {
		write(metadataFd,CTFMetadataStructs[i],strlen(CTFMetadataStructs[i]));
	}

	return TraceDqr::DQERR_OK;
}

TraceDqr::DQErr CTFConverter::writeTracePacketHeader(int core)
{
	struct __attribute__ ((packed)) tracePacketHeader {
		uint32_t magic;
		uint32_t stream_id;
	} tph;

	tph.magic = 0xc1fc1fc1;
	tph.stream_id = core;

	int n;

	n = write(fd[core],&tph,sizeof(tph));
	if (n != sizeof(tph)) {
		printf("Error: writeTracePacketHeader(): Could not write trace.packet.header\n");

		return TraceDqr::DQERR_ERR;
	}

	return TraceDqr::DQERR_OK;
}

TraceDqr::DQErr CTFConverter::writeStreamPacketContext(int core,uint64_t ts_begin,uint64_t ts_end,int size)
{
	struct __attribute__ ((packed)) streamPacketContext {
		uint64_t timestamp_begin;
		uint64_t timestamp_end;
		uint64_t content_size;
		uint64_t packet_size;
		uint64_t packet_seq_num;
		uint64_t events_discarded;
		uint32_t cpu_id;
	} spc;

	spc.timestamp_begin = ts_begin;
	spc.timestamp_end = ts_end;
//	spc.content_size = size*8;
	spc.content_size = size * 8 + sizeof spc * 8 + 8 * 8; // size of packet - padding (we don't pad)
	spc.packet_size  = size * 8 + sizeof spc * 8 + 8 * 8; // size of ENTIRE packet!

	spc.packet_seq_num = packetSeqNum;
	spc.events_discarded = 0;
	spc.cpu_id = core;

	int n;

	n = write(fd[core],&spc,sizeof spc);
	if (n != sizeof spc) {
		printf("Error: writeStreamPacketContext(): Could not write trace.packet.context\n");

		return TraceDqr::DQERR_ERR;
	}

	return TraceDqr::DQERR_OK;
}

TraceDqr::DQErr CTFConverter::writeStreamHeaders(int core,uint64_t ts_begin,uint64_t ts_end,int size)
{
//	printf("write headers core: %d num events: %d\n",core,eventIndex[core]);

	writeTracePacketHeader(core);
	writeStreamPacketContext(core,ts_begin,ts_end,size);

	return TraceDqr::DQERR_OK;
}

TraceDqr::DQErr CTFConverter::computeEventSizes(int core,int &size)
{
	uint32_t et;

	size = 0;

	for (int i = 0; i < eventIndex[core]; i++) {
		size += sizeof eventBuffer[core][i].event_header.event_id;

		if (eventBuffer[core][i].event_header.event_id == CTF::event_extended) {
			size += sizeof eventBuffer[core][i].event_header.extended;
			et = eventBuffer[core][i].event_header.extended.event_id;
		}
		else {
			size += sizeof eventBuffer[core][i].event_header.compact;
			et = eventBuffer[core][i].event_header.event_id;
		}

		size += sizeof(event_context);

		switch (et) {
		case CTF::event_funcEntry:
			size += sizeof eventBuffer[core][i].call;
			break;
		case CTF::event_funcExit:
			size += sizeof eventBuffer[core][i].ret;
			break;
		default:
			printf("Error: computeEventSizes(): Invalid event type (%d)\n",eventBuffer[core][i].event_header.event_id);
			status = TraceDqr::DQERR_ERR;

			return TraceDqr::DQERR_ERR;
		}
	}

	return TraceDqr::DQERR_OK;
}

TraceDqr::DQErr CTFConverter::computeBinInfoSize(int &size)
{
	// Compute size of event headers (there will be three)

	size = sizeof eventBuffer[0][0].event_header.event_id * 3;

	size += sizeof eventBuffer[0][0].event_header.extended * 3;

	// Compute size of event contexts (there will be three)

	size += sizeof(event_context) * 3;

	// compute size of events (there will be three)

	// first event is a statedump start

	size += 0;	// start event has 0 size

	// next event is a statedump binInfo

	// binInfo has a variable lenght string. We need to compute its length

	int l = 0;
	for (l = 0; elfName[l] != 0; l++) { /* empty */	}

	l += 1; // account for null

	size += sizeof(uint64_t) + sizeof(uint64_t) + l + sizeof(uint8_t) + sizeof(uint8_t) + sizeof(uint8_t);

	// last event is a statedump end

	size += 0;	// end event has 0 size

	return TraceDqr::DQERR_OK;
}

TraceDqr::DQErr CTFConverter::writeBinInfo(int core,uint64_t timestamp)
{
	int size;
	event e;
	TraceDqr::DQErr rc;

	rc = computeBinInfoSize(size);

	if (rc != TraceDqr::DQERR_OK) {
		printf("Error: writeBinInfo(): could not compute BinInfoSize\n");

		return rc;
	}

	// write stream header first

	writeStreamHeaders(core,timestamp,timestamp,size); // packet header, packet context

	// write event header for state dump start

	e.event_header.event_id = CTF::event_extended;
	e.event_header.extended.event_id = CTF::event_stateDumpStart;
	e.event_header.extended.timestamp = timestamp;

	write(fd[core],&e.event_header,sizeof e.event_header);

	// write event context for state dump

	write(fd[core],&eventContext[core],sizeof(event_context));

	// empty event for state dump start, so nothing to write for event

	// write event header for bin info

	e.event_header.event_id = CTF::event_extended;
	e.event_header.extended.event_id = CTF::event_stateDumpBinInfo;
	e.event_header.extended.timestamp = timestamp;

	write(fd[core],&e.event_header,sizeof e.event_header);

	// write event context for binInfo

	write(fd[core],&eventContext[core],sizeof(event_context));

	// now write bininfo event

//	printf("need to get memory base address and size from symtab sections\n");fflush(stdout);

	e.binInfo._baddr = 0x40000000;

	write(fd[core],&e.binInfo._baddr,sizeof e.binInfo._baddr);

	e.binInfo._memsz = 0x10000000;

	write(fd[core],&e.binInfo._memsz,sizeof e.binInfo._memsz);

	write(fd[core],elfName,strlen(elfName)+1);

	e.binInfo._is_pic = 0;

	write(fd[core],&e.binInfo._is_pic,sizeof e.binInfo._is_pic);

	e.binInfo._has_build_id = 0;

	write(fd[core],&e.binInfo._has_build_id,sizeof e.binInfo._has_build_id);

	e.binInfo._has_debug_link = 0;

	write(fd[core],&e.binInfo._baddr,sizeof e.binInfo._has_debug_link);

	// write event header for state dump end

	e.event_header.event_id = CTF::event_extended;
	e.event_header.extended.event_id = CTF::event_stateDumpEnd;
	e.event_header.extended.timestamp = timestamp;

	write(fd[core],&e.event_header,sizeof e.event_header);

	// write event context for state dump end

	write(fd[core],&eventContext[core],sizeof(event_context));

	// empty event for state dump end, so nothing to write for event

	return TraceDqr::DQERR_OK;
}

TraceDqr::DQErr CTFConverter::writeEvent(int core,int index)
{
	uint32_t et;

	// write event headers

	write(fd[core],&eventBuffer[core][index].event_header.event_id,sizeof eventBuffer[core][index].event_header.event_id);

	if (eventBuffer[core][index].event_header.event_id == CTF::event_extended) {
		et = eventBuffer[core][index].event_header.extended.event_id;
		write(fd[core],&eventBuffer[core][index].event_header.extended,sizeof eventBuffer[core][index].event_header.extended);
	}
	else {
		et = eventBuffer[core][index].event_header.event_id;
		write(fd[core],&eventBuffer[core][index].event_header.compact,sizeof eventBuffer[core][index].event_header.compact);
	}

	// write stream.event.context

	write(fd[core],&eventContext[core],sizeof(event_context));

	// write event payload

	switch (et) {
	case CTF::event_funcEntry:
		write(fd[core],&eventBuffer[core][index].call,sizeof eventBuffer[core][index].call);
		break;
	case CTF::event_funcExit:
		write(fd[core],&eventBuffer[core][index].ret,sizeof eventBuffer[core][index].ret);
		break;
	default:
		printf("Error: writeEvent(): Invalid event type (%d)\n",et);
		return TraceDqr::DQERR_ERR;
	}

	return TraceDqr::DQERR_OK;
}

TraceDqr::DQErr CTFConverter::flushEvents(int core)
{
	int size;
	TraceDqr::DQErr rc;

	if (eventIndex[core] == 0) {
		return TraceDqr::DQERR_OK;
	}

	if (eventBuffer[core] == nullptr) {
		return TraceDqr::DQERR_OK;
	}

	if (headerFlag[core] == false) {
		rc = writeBinInfo(core, eventBuffer[core][0].event_header.extended.timestamp);
		if (rc != TraceDqr::DQERR_OK) {
			printf("Error: writeEvent(%d): write binInfo failed.\n",core);
			return rc;
		}

		headerFlag[core] = true;
	}

	// need to write packet headers before events. But we need to know the size of the events we are going
	// to write, so it can be included in the packet context fields

	rc = computeEventSizes(core,size);
	if (rc != TraceDqr::DQERR_OK) {
		printf("Error: CTFConverter::flushEvent(): computeEventSizes() failed\n");

		return TraceDqr::DQERR_OK;
	}

	uint64_t ts_begin;
	uint64_t ts_end;

	ts_begin = eventBuffer[core][0].event_header.extended.timestamp;
	if (eventIndex[core] > 1) {
		ts_end = ((ts_begin >> 32) << 32) | (uint64_t)eventBuffer[core][eventIndex[core]-1].event_header.compact.timestamp;
	}
	else {
		ts_end = ts_begin;
	}

	writeStreamHeaders(core,ts_begin,ts_end,size);

	for (int i = 0; i < eventIndex[core]; i++) {
		writeEvent(core,i);
	}

	eventIndex[core] = 0;

//	printf("wrote packet %d\n",packetSeqNum);

	packetSeqNum += 1;

	return TraceDqr::DQERR_OK;
}

TraceDqr::DQErr CTFConverter::addCall(int core,TraceDqr::ADDRESS srcAddr,TraceDqr::ADDRESS dstAddr,TraceDqr::TIMESTAMP eventTS)
{
	TraceDqr::DQErr rc;

	if (eventBuffer[core] == nullptr) {
		eventBuffer[core] = new  event[20];
	}

	// if q is full, flush it

	if (eventIndex[core] == sizeof eventBuffer / sizeof eventBuffer[0]) {
		rc = flushEvents(core);
		if (rc != TraceDqr::DQERR_OK) {
			printf("Error: CTFConverter::addCall() failed\n");

			status = rc;
			return rc;
		}
	}

	if (eventIndex[core] == 0) {
		// use a full timestamp (64 bits) on first event

		eventBuffer[core][eventIndex[core]].event_header.event_id = CTF::event_extended;
		eventBuffer[core][eventIndex[core]].event_header.extended.event_id = CTF::event_funcEntry;
		eventBuffer[core][eventIndex[core]].event_header.extended.timestamp = eventTS;
	}
	else {
		// use a compressed timestamp (32 bits) on all but first event

		eventBuffer[core][eventIndex[core]].event_header.event_id = CTF::event_funcEntry;
		eventBuffer[core][eventIndex[core]].event_header.compact.timestamp = eventTS;
	}

	eventBuffer[core][eventIndex[core]].call.pc = srcAddr;
	eventBuffer[core][eventIndex[core]].call.pcDst = dstAddr;

	eventIndex[core] += 1;

	return TraceDqr::DQERR_OK;
}

TraceDqr::DQErr CTFConverter::addRet(int core,TraceDqr::ADDRESS srcAddr,TraceDqr::ADDRESS dstAddr,TraceDqr::TIMESTAMP eventTS)
{
	TraceDqr::DQErr rc;

	if (eventBuffer[core] == nullptr) {
		eventBuffer[core] = new  event[20];
	}

	// if q is full, flush it

	if (eventIndex[core] == sizeof eventBuffer / sizeof eventBuffer[0]) {
		rc = flushEvents(core);
		if (rc != TraceDqr::DQERR_OK) {
			printf("Error: CTFConverter::addRet() failed\n");

			status = rc;
			return rc;
		}
	}

	if (eventIndex[core] == 0) {
		// use a full timestamp (64 bits) on first event

		eventBuffer[core][eventIndex[core]].event_header.event_id = CTF::event_extended;
		eventBuffer[core][eventIndex[core]].event_header.extended.event_id = CTF::event_funcExit;
		eventBuffer[core][eventIndex[core]].event_header.extended.timestamp = eventTS;
	}
	else {
		// use a compressed timestamp (32 bits) on all but first event

		eventBuffer[core][eventIndex[core]].event_header.event_id = CTF::event_funcExit;
		eventBuffer[core][eventIndex[core]].event_header.compact.timestamp = eventTS;
	}

	eventBuffer[core][eventIndex[core]].call.pc = srcAddr;
	eventBuffer[core][eventIndex[core]].call.pcDst = dstAddr;

	eventIndex[core] += 1;

	return TraceDqr::DQERR_OK;
}

// class TraceSettings methods

TraceSettings::TraceSettings()
{
	tfName = nullptr;
	efName = nullptr;
	caName = nullptr;
	caType = TraceDqr::CATRACE_NONE;
	srcBits = 0;
	numAddrBits = 0;
	itcPrintOpts = TraceDqr::ITC_OPT_NLS;
	itcPrintBufferSize = 4096;
	itcPrintChannel = 0;
	cutPath = nullptr;
	srcRoot = nullptr;
	pathType = TraceDqr::PATH_TO_UNIX;
	labelsAsFunctions = true;
	freq = 0;
	addrDispFlags = 0;
	tsSize = 40;
	CTFConversion = false;
	eventConversionEnable = false;
	startTime = -1;
	hostName = nullptr;
	filterControlEvents = false;
}

TraceSettings::~TraceSettings()
{
	if (tfName != nullptr) {
		delete [] tfName;
		tfName = nullptr;
	}

	if (efName != nullptr) {
		delete [] efName;
		efName = nullptr;
	}

	if (caName != nullptr) {
		delete [] caName;
		caName = nullptr;
	}

	if (srcRoot != nullptr) {
		delete [] srcRoot;
		srcRoot = nullptr;
	}

	if (cutPath != nullptr) {
		delete [] cutPath;
		cutPath = nullptr;
	}

	if (hostName != nullptr) {
		delete [] hostName;
		hostName = nullptr;
	}
}

TraceDqr::DQErr TraceSettings::addSettings(propertiesParser *properties)
{
	TraceDqr::DQErr rc;
	char *name = nullptr;
	char *value = nullptr;

	properties->rewind();

	do {
		rc = properties->getNextProperty(&name,&value);
		if (rc == TraceDqr::DQERR_OK) {
//			printf("name: %s, value: %s\n",name,value);

			if (strcasecmp("rtd",name) == 0) {
				rc = propertyToTFName(value);
				if (rc != TraceDqr::DQERR_OK) {
					printf("Error: TraceSettings::addSettings(): Could not set trace file name in settings\n");
					return rc;
				}
			}
			else if (strcasecmp("elf",name) == 0) {
				rc = propertyToEFName(value);
				if (rc != TraceDqr::DQERR_OK) {
					printf("Error: TraceSettings::addSettings(): Could not set elf file name in settings\n");
					return rc;
				}
			}
			else if (strcasecmp("srcbits",name) == 0) {
				rc = propertyToSrcBits(value);
				if (rc != TraceDqr::DQERR_OK) {
					printf("Error: TraceSettings::addSettings(): Could not set srcBits in settings\n");
					return rc;
				}
			}
			else if (strcasecmp("bits",name) == 0) {
				rc = propertyToNumAddrBits(value);
				if (rc != TraceDqr::DQERR_OK) {
					printf("Error: TraceSettings::addSettings(): Could not set numAddrBits in settings\n");
					return rc;
				}
			}
			else if (strcasecmp("trace.config.boolean.enable.itc.print.processing",name) == 0) {
				rc = propertyToITCPrintOpts(value); // value should be nul, true, or false
				if (rc != TraceDqr::DQERR_OK) {
					printf("Error: TraceSettings::addSettings(): Could not set ITC print options in settings\n");
					return rc;
				}
			}
			else if (strcasecmp("trace.config.int.itc.print.channel",name) == 0) {
				rc = propertyToITCPrintChannel(value);
				if (rc != TraceDqr::DQERR_OK) {
					printf("Error: TraceSettings::addSettings(): Could not set ITC print channel value in settings\n");
					return rc;
				}
			}
			else if (strcasecmp("trace.config.int.itc.print.buffersize",name) == 0) {
				rc = propertyToITCPrintBufferSize(value);
				if (rc != TraceDqr::DQERR_OK) {
					printf("Error: TraceSettings::addSettings(): Could not set ITC print buffer size in settings\n");
					return rc;
				}
			}
			else if (strcasecmp("source.root",name) == 0) {
				rc = propertyToSrcRoot(value);
				if (rc != TraceDqr::DQERR_OK) {
					printf("Error: TraceSettings::addSettings(): Could not set src root path in settings\n");
					return rc;
				}
			}
			else if (strcasecmp("source.cutpath",name) == 0) {
				rc = propertyToSrcCutPath(value);
				if (rc != TraceDqr::DQERR_OK) {
					printf("Error: TraceSettings::addSettings(): Could not set src cut path in settings\n");
					return rc;
				}
			}
			else if (strcasecmp("caFile",name) == 0) {
				rc = propertyToCAName(value);
				if (rc != TraceDqr::DQERR_OK) {
					printf("Error: TraceSettings::addSettings(): Could not set CA file name in settings\n");
					return rc;
				}
			}
			else if (strcasecmp("caType",name) == 0) {
				rc = propertyToCAType(value);
				if (rc != TraceDqr::DQERR_OK) {
					printf("Error: TraceSettings::addSettings(): Could not set CA type in settings\n");
					return rc;
				}
			}
			else if (strcasecmp("TSSize",name) == 0) {
				rc = propertyToTSSize(value);
				if (rc != TraceDqr::DQERR_OK) {
					printf("Error: TraceSettings::addSettings(): Could not set TS size in settings\n");
					return rc;
				}
			}
			else if (strcasecmp("pathType",name) == 0) {
				rc = propertyToPathType(value);
				if (rc != TraceDqr::DQERR_OK) {
					printf("Error: TraceSettings::addSettings(): Could not set path type in settings\n");
					return rc;
				}
			}
			else if (strcasecmp("labelsAsFunctions",name) == 0) {
				rc = propertyToLabelsAsFuncs(value);
				if (rc != TraceDqr::DQERR_OK) {
					printf("Error: TraceSettings::addSettings(): Could not set labels as functions in settings\n");
					return rc;
				}
			}
			else if (strcasecmp("freq",name) == 0) {
				rc = propertyToFreq(value);
				if (rc != TraceDqr::DQERR_OK) {
					printf("Error: TraceSettings::addSettings(): Could not set frequency in settings\n");
					return rc;
				}
			}
			else if (strcasecmp("ctfenable",name) == 0) {
				rc = propertyToCTFEnable(value);
				if (rc != TraceDqr::DQERR_OK) {
					printf("Error: TraceSettings::addSettings(): Could not set ctfEnable in settings\n");
					return rc;
				}
			}
			else if (strcasecmp("eventConversionEnable",name) == 0) {
				rc = propertyToEventConversionEnable(value);
				if (rc != TraceDqr::DQERR_OK) {
					printf("Error: TraceSettings::addSettings(): Could not set eventConversionEnable in settings\n");
					return rc;
				}
			}
			else if (strcasecmp("filterControlEvents",name) == 0) {
				rc = propertyToFilterControlEvents(value);
				if (rc != TraceDqr::DQERR_OK) {
					printf("Error: TraceSettings::addSettings(): Could not set eventConversionEnable in settings\n");
					return rc;
				}
			}
			else if (strcasecmp("addressdisplayflags", name) == 0) {
				rc = propertyToAddrDispFlags(value);
				if (rc != TraceDqr::DQERR_OK) {
					printf("Error: TraceSettings::addSettings(): Could not set address display flags in settings\n");
					return rc;
				}
			}
			else if (strcasecmp("starttime", name) == 0) {
				rc = propertyToStartTime(value);
				if (rc != TraceDqr::DQERR_OK) {
					printf("Error: TraceSettings::addSettings(): Could not set start time in settings\n");
					return rc;
				}
			}
			else if (strcasecmp("hostname", name) == 0) {
				rc = propertyToHostName(value);
				if (rc != TraceDqr::DQERR_OK) {
					printf("Error: TraceSettings::addSettings(): Could not set host name in settings\n");
					return rc;
				}
			}
		}
	} while (rc == TraceDqr::DQERR_OK);

	if (rc != TraceDqr::DQERR_EOF) {
		printf("Error: TraceSettings::addSettings(): problem parsing properties file: %d\n",rc);
		return TraceDqr::DQERR_ERR;
	}

	return TraceDqr::DQERR_OK;
}

TraceDqr::DQErr TraceSettings::propertyToTFName(char *value)
{
	if (value != nullptr) {
		if (tfName != nullptr) {
			delete [] tfName;
			tfName = nullptr;
		}

		int l;
		l = strlen(value) + 1;

		tfName = new char [l];
		strcpy(tfName,value);
	}

	return TraceDqr::DQERR_OK;
}

TraceDqr::DQErr TraceSettings::propertyToEFName(char *value)
{
	if (value != nullptr) {
		if (efName != nullptr) {
			delete [] efName;
			efName = nullptr;
		}

		int l;
		l = strlen(value) + 1;

		efName = new char [l];
		strcpy(efName,value);
	}

	return TraceDqr::DQERR_OK;
}

TraceDqr::DQErr TraceSettings::propertyToAddrDispFlags(char *value)
{
	if ((value != nullptr) && (value[0] != '\0')) {
		addrDispFlags = 0;

		int l;
		char *endptr;

		l = strtol(value, &endptr, 10);

		if (endptr[0] == 0 ) {
			numAddrBits = l;
			addrDispFlags = addrDispFlags  & ~TraceDqr::ADDRDISP_WIDTHAUTO;
		}
		else if (endptr[0] == '+') {
			numAddrBits = l;
			addrDispFlags = addrDispFlags | TraceDqr::ADDRDISP_WIDTHAUTO;
		}
		else {
			return TraceDqr::DQERR_ERR;
		}

		if ((l < 32) || (l > 64)) {
			return TraceDqr::DQERR_ERR;
		}
	}

	return TraceDqr::DQERR_OK;
}

TraceDqr::DQErr TraceSettings::propertyToSrcBits(char *value)
{
	if ((value != nullptr) && (value[0] != '\0')) {
		char *endp;

		srcBits = strtol(value,&endp,0);

		if (endp == value) {
			return TraceDqr::DQERR_ERR;
		}
	}

	return TraceDqr::DQERR_OK;
}

TraceDqr::DQErr TraceSettings::propertyToNumAddrBits(char *value)
{
	if ((value != nullptr) && (value[0] != '\0')) {
		char *endp;

		numAddrBits = strtol(value,&endp,0);

		if (endp == value) {
			return TraceDqr::DQERR_ERR;
		}
	}

	return TraceDqr::DQERR_OK;
}

TraceDqr::DQErr TraceSettings::propertyToITCPrintOpts(char *value)
{
	TraceDqr::DQErr rc;
	bool opts;

	rc = propertyToBool(value,opts);

	if (rc != TraceDqr::DQERR_OK) {
		return rc;
	}

	if (opts) {
		itcPrintOpts = TraceDqr::ITC_OPT_PRINT | TraceDqr::ITC_OPT_NLS;
	}
	else {
		itcPrintOpts = TraceDqr::ITC_OPT_NLS;
	}

	return TraceDqr::DQERR_OK;
}

TraceDqr::DQErr TraceSettings::propertyToITCPrintChannel(char *value)
{
	if ((value != nullptr) && (value[0] != '\0')) {
		char *endp;

		itcPrintChannel = strtol(value,&endp,0);

		if (endp == value) {
			return TraceDqr::DQERR_ERR;
		}
	}

	return TraceDqr::DQERR_OK;
}

TraceDqr::DQErr TraceSettings::propertyToITCPrintBufferSize(char *value)
{
	if ((value != nullptr) && (value[0] != '\0')) {
		char *endp;

		itcPrintBufferSize = strtol(value,&endp,0);

		if (endp == value) {
			return TraceDqr::DQERR_ERR;
		}
	}

	return TraceDqr::DQERR_OK;
}

TraceDqr::DQErr TraceSettings::propertyToSrcRoot(char *value)
{
	if (value != nullptr) {
		if (srcRoot != nullptr) {
			delete [] srcRoot;
			srcRoot = nullptr;
		}

		int l;
		l = strlen(value) + 1;

		srcRoot = new char [l];
		strcpy(srcRoot,value);
	}

	return TraceDqr::DQERR_OK;
}

TraceDqr::DQErr TraceSettings::propertyToSrcCutPath(char *value)
{
	if (value != nullptr) {
		if (cutPath != nullptr) {
			delete [] cutPath;
			cutPath = nullptr;
		}

		int l;
		l = strlen(value) + 1;

		cutPath = new char [l];
		strcpy(cutPath,value);
	}

	return TraceDqr::DQERR_OK;
}

TraceDqr::DQErr TraceSettings::propertyToCAName(char *value)
{
	if (value != nullptr) {
		int l;
		l = strlen(value) + 1;

		caName = new char [l];
		strcpy(caName,value);
	}

	return TraceDqr::DQERR_OK;
}

TraceDqr::DQErr TraceSettings::propertyToCAType(char *value)
{
	if ((value != nullptr) && (value[0] != '\0')) {
		if (strcasecmp(value,"none") == 0) {
			caType = TraceDqr::CATRACE_NONE;
		}
		else if (strcasecmp(value,"catrace_none") == 0) {
			caType = TraceDqr::CATRACE_NONE;
		}
		else if (strcasecmp(value,"vector") == 0) {
			caType = TraceDqr::CATRACE_VECTOR;
		}
		else if (strcasecmp(value,"catrace_vector") == 0) {
			caType = TraceDqr::CATRACE_VECTOR;
		}
		else if (strcasecmp(value,"instruction") == 0) {
			caType = TraceDqr::CATRACE_INSTRUCTION;
		}
		else if (strcasecmp(value,"catrace_instruction") == 0) {
			caType = TraceDqr::CATRACE_INSTRUCTION;
		}
		else {
			return TraceDqr::DQERR_ERR;
		}
	}

	return TraceDqr::DQERR_OK;
}

TraceDqr::DQErr TraceSettings::propertyToPathType(char *value)
{
	if ((value != nullptr) && (value[0] != '\0')) {
		if (strcasecmp("unix",value) == 0) {
			pathType = TraceDqr::PATH_TO_UNIX;
		}
		else if (strcasecmp("windows",value) == 0) {
			pathType = TraceDqr::PATH_TO_WINDOWS;
		}
		else if (strcasecmp("raw",value) == 0) {
			pathType = TraceDqr::PATH_RAW;
		}
		else {
			return TraceDqr::DQERR_ERR;
		}
	}

	return TraceDqr::DQERR_OK;
}

TraceDqr::DQErr TraceSettings::propertyToBool(char *src,bool &value)
{
	if ((src != nullptr) && (src[0] != '\0')) {
		if (strcasecmp("true",src) == 0) {
			value = true;		}
		else if (strcasecmp("false",src) == 0) {
			value = false;
		}
		else {
			char *endp;

			value = strtol(src,&endp,0);
			if (endp == src) {
				return TraceDqr::DQERR_ERR;
			}
		}
	}

	return TraceDqr::DQERR_OK;
}


TraceDqr::DQErr TraceSettings::propertyToLabelsAsFuncs(char *value)
{
	return propertyToBool(value,labelsAsFunctions);
}

TraceDqr::DQErr TraceSettings::propertyToCTFEnable(char *value)
{
	return propertyToBool(value,CTFConversion);
}

TraceDqr::DQErr TraceSettings::propertyToEventConversionEnable(char *value)
{
	return propertyToBool(value,eventConversionEnable);
}

TraceDqr::DQErr TraceSettings::propertyToFilterControlEvents(char *value)
{
	return propertyToBool(value,filterControlEvents);
}

TraceDqr::DQErr TraceSettings::propertyToFreq(char *value)
{
	if ((value != nullptr) && (value[0] != '\0')) {
		char *endp;

		freq = strtol(value,&endp,0);

		if (endp == value) {
			return TraceDqr::DQERR_ERR;
		}
	}

	return TraceDqr::DQERR_OK;
}

TraceDqr::DQErr TraceSettings::propertyToStartTime(char *value)
{
	if ((value != nullptr) && (value[0] != '\0')) {
		char *endp;

		startTime = (int64_t)strtoll(value,&endp,0);

		if (endp == value) {
			return TraceDqr::DQERR_ERR;
		}
	}

	return TraceDqr::DQERR_OK;
}

TraceDqr::DQErr TraceSettings::propertyToHostName(char *value)
{
	if ((value != nullptr) && (value[0] != '\0')) {
		if (hostName != nullptr) {
			delete [] hostName;
			hostName = nullptr;
		}

		int l;
		l = strlen(value) + 1;

		hostName = new char [l];
		strcpy(hostName,value);
	}

	return TraceDqr::DQERR_OK;
}

TraceDqr::DQErr TraceSettings::propertyToTSSize(char *value)
{
	if ((value != nullptr) && (value[0] != '\0')) {
		char *endp;

		tsSize = strtol(value,&endp,0);

		if (endp == value) {
			return TraceDqr::DQERR_ERR;
		}
	}

	return TraceDqr::DQERR_OK;
}

// class propertiesParser methods

propertiesParser::propertiesParser(char *srcData)
{
	status = TraceDqr::DQERR_OK;

	propertiesBuff = nullptr;
	lines = nullptr;
	numLines = 0;
	nextLine = 0;
	size = 0;

	if (srcData == nullptr) {
		return;
	}

	std::ifstream  f;

	f.open(srcData, std::ifstream::binary);
	if (!f) {
		printf("Error: propertiesParser::propertiesParser(): could not open file %s for input\n",srcData);

		status = TraceDqr::DQERR_OPEN;
		return;
	}

	// get length of file:

	f.seekg(0, f.end);
	size = f.tellg();
	f.seekg(0, f.beg);

	if (size < 0) {
		printf("Error: propertiesParser::propertiesParser(): could not get size of file %s for input\n",srcData);

		f.close();

		status = TraceDqr::DQERR_OPEN;
		return;
	}

	// allocate memory:

	propertiesBuff = new char [size+1]; // allocate an extra byte in case the file doesn't end with \n

	// read file into buffer

	f.read(propertiesBuff,size);
	int numRead = f.gcount();
	f.close();

	if (numRead != size) {
		printf("Error: propertiesParser::propertiesParser(): could not read file %s into memory\n",srcData);

		delete [] propertiesBuff;
		propertiesBuff = nullptr;
		size = 0;

		status = TraceDqr::DQERR_OPEN;
		return;
	}

	// count lines

	numLines = 0;

	for (int i = 0; i < size; i++) {
		if (propertiesBuff[i] == '\n') {
			numLines += 1;
		}
		else if (i == size-1) {
			// last line does not have a \n
			numLines += 1;
		}
	}

	// create array of line pointers

	lines = new line [numLines];

	// initialize array of ptrs

	int l;
	int s;

	l = 0;
	s = 1;

	for (int i = 0; i < numLines; i++) {
		lines[i].line = nullptr;
		lines[i].name = nullptr;
		lines[i].value = nullptr;
	}

	for (int i = 0; i < size;i++) {
		if (s != 0) {
			lines[l].line = &propertiesBuff[i];
			l += 1;
			s = 0;
		}

		// strip out CRs and LFs

		if (propertiesBuff[i] == '\r') {
			propertiesBuff[i] = 0;
		}
		else if (propertiesBuff[i] == '\n') {
			propertiesBuff[i] = 0;
			s = 1;
		}
	}

	propertiesBuff[size] = 0;	// make sure last line is nul terminated

	if (l != numLines) {
		printf("Error: propertiesParser::propertiesParser(): Error computing line count for file %s, l:%d, lc: %d\n",srcData,l,numLines);

		delete [] lines;
		lines = nullptr;
		delete [] propertiesBuff;
		propertiesBuff = nullptr;
		size = 0;
		numLines = 0;

		status = TraceDqr::DQERR_ERR;
		return;
	}
}

propertiesParser::~propertiesParser()
{
	if (propertiesBuff != nullptr) {
		delete [] propertiesBuff;
		propertiesBuff = nullptr;
	}

	if (lines != nullptr) {
		delete [] lines;
		lines = nullptr;
	}
}

void propertiesParser::rewind()
{
	nextLine = 0;
}

TraceDqr::DQErr propertiesParser::getNextToken(char *inputText,int &startIndex,int &endIndex)
{
	if (inputText == nullptr) {
		return TraceDqr::DQERR_ERR;
	}

	// stripi ws

	bool found;

	for (found = false; !found; ) {
		switch (inputText[startIndex]) {
		case '\t':
		case ' ':
			// skip this char
			startIndex += 1;
			break;
		default:
			found = true;
			break;
		}
	}

	endIndex = startIndex;

	// check for end of line

	switch (inputText[startIndex]) {
	case '#':
	case '\0':
	case '\n':
	case '\r':
		// end of line. If end == start, nothing was found

		return TraceDqr::DQERR_OK;
	}

	// scan to end of token

	// will not start with #, =, \0, \r, \n
	// so scan until we find an end

	for (found = false; !found; ) {
		switch (inputText[endIndex]) {
		case ' ':
		case '#':
		case '\0':
		case '\n':
		case '\r':
			found = true;
			break;
		case '=':
			if (startIndex == endIndex) {
				endIndex += 1;
			}
			found = true;
			break;
		default:
			endIndex += 1;
		}
	}

//	printf("getNextToken(): start %d, end %d ,'",startIndex,endIndex);
//	for (int i = startIndex; i < endIndex; i++) {
//		printf("%c",inputText[i]);
//	}
//	printf("'\n");

	return TraceDqr::DQERR_OK;
}

TraceDqr::DQErr propertiesParser::getNextProperty(char **name,char **value)
{
	if (status != TraceDqr::DQERR_OK) {
		return status;
	}

	if (lines == nullptr) {
		status = TraceDqr::DQERR_EOF;
		return TraceDqr::DQERR_EOF;
	}

	if ((name == nullptr) || (value == nullptr)) {
		return TraceDqr::DQERR_ERR;
	}

	// if we are at the end, return EOF

	if (nextLine >= numLines) {
		return TraceDqr::DQERR_EOF;
	}

	// If this name/value pair has already been found, return it

	if ((lines[nextLine].name != nullptr) && (lines[nextLine].value != nullptr)) {
		*name = lines[nextLine].name;
		*value = lines[nextLine].value;

		nextLine += 1;

		return TraceDqr::DQERR_OK;
	}

	// get name

	int nameStart = 0;
	int nameEnd = 0;

	TraceDqr::DQErr rc;

	do {
		rc = getNextToken(lines[nextLine].line,nameStart,nameEnd);
		if (rc != TraceDqr::DQERR_OK) {
			status = rc;
			return rc;
		}

		if (nameStart == nameEnd) {
			nextLine += 1;
		}
	} while (nameStart == nameEnd);

	// check if we got a name, or an '='

	if (((nameStart - nameEnd) == 1) && (lines[nextLine].line[nameStart] == '=')) {
		// error - name cannot be '='
		printf("Error: propertiesParser::getNextProperty(): Line %d: syntax error\n",nextLine);

		status = TraceDqr::DQERR_ERR;
		return TraceDqr::DQERR_ERR;
	}

	int eqStart = nameEnd;
	int eqEnd = nameEnd;

	// get '='
	rc = getNextToken(lines[nextLine].line,eqStart,eqEnd);
	if (rc != TraceDqr::DQERR_OK) {
		status = rc;
		return rc;
	}

	if ((eqStart == eqEnd) || ((eqEnd - eqStart) != 1) || (lines[nextLine].line[eqStart] != '=')) {
		printf("Error: propertiesParser::getNextProperty(): Line %d: expected '='\n",nextLine);

		status = TraceDqr::DQERR_ERR;
		return TraceDqr::DQERR_ERR;
	}

	// get value or end of line

	int valueStart = eqEnd;
	int valueEnd = eqEnd;

	rc = getNextToken(lines[nextLine].line,valueStart,valueEnd);
	if (rc != TraceDqr::DQERR_OK) {
		status = rc;
		return rc;
	}

	if (((valueStart - valueEnd) == 1) && (lines[nextLine].line[nameStart] == '=')) {
		// error - value cannot be '='
		printf("Error: propertiesParser::getNextProperty(): Line %d: syntax error\n",nextLine);

		status = TraceDqr::DQERR_ERR;
		return TraceDqr::DQERR_ERR;
	}

	lines[nextLine].line[nameEnd] = 0;
	lines[nextLine].name = &lines[nextLine].line[nameStart];

	*name = lines[nextLine].name;

	lines[nextLine].line[valueEnd] = 0;
	lines[nextLine].value = &lines[nextLine].line[valueStart];

	*value = lines[nextLine].value;

	nextLine += 1;

	return TraceDqr::DQERR_OK;
}


// class trace methods

int Trace::decodeInstructionSize(uint32_t inst, int &inst_size)
{
  return disassembler->decodeInstructionSize(inst,inst_size);
}

int Trace::decodeInstruction(uint32_t instruction,int &inst_size,TraceDqr::InstType &inst_type,TraceDqr::Reg &rs1,TraceDqr::Reg &rd,int32_t &immediate,bool &is_branch)
{
	return disassembler->decodeInstruction(instruction,getArchSize(),inst_size,inst_type,rs1,rd,immediate,is_branch);
}

Trace::Trace(char *mf_name)
{
	if (mf_name == nullptr) {
		printf("Error: Trace(): mf_name argument null\n");

		status = TraceDqr::DQERR_ERR;
		return;
	}

	TraceDqr::DQErr rc;

	propertiesParser properties(mf_name);

	rc = properties.getStatus();
	if (rc != TraceDqr::DQERR_OK) {
		printf("Error: Trace(): new propertiesParser(%s) from file failed with %d\n",mf_name,rc);
		status = rc;
		return;
	}

	TraceSettings settings;

	rc = settings.addSettings(&properties);
	if (rc != TraceDqr::DQERR_OK) {
		printf("Error: Trace(): addSettings() failed\n");

		status = rc;

		return;
	}

	rc = configure(settings);
	if (rc != TraceDqr::DQERR_OK) {
		status = rc;

		return;
	}

	status = TraceDqr::DQERR_OK;
}

Trace::Trace(char *tf_name,char *ef_name,int numAddrBits,uint32_t addrDispFlags,int srcBits,uint32_t freq)
{
	TraceDqr::DQErr rc;

	TraceSettings ts;
	ts.propertyToTFName(tf_name);
	ts.propertyToEFName(ef_name);
	ts.numAddrBits = numAddrBits;
	ts.addrDispFlags = addrDispFlags;
	ts.srcBits = srcBits;
	ts.freq = freq;

	rc = configure(ts);
	status = rc;
}

Trace::~Trace()
{
	cleanUp();
}

// configure should probably take a options object that contains the seetings for all the options. Easier to add
// new options that way without the arg list getting unmanageable

TraceDqr::DQErr Trace::configure(TraceSettings &settings)
{
	TraceDqr::DQErr rc;

	status = TraceDqr::DQERR_OK;

	sfp          = nullptr;
	elfReader    = nullptr;
	symtab       = nullptr;
	disassembler = nullptr;
	caTrace      = nullptr;
	counts       = nullptr;//delete this line if compile error
	efName       = nullptr;
	rtdName      = nullptr;
	cutPath      = nullptr;
	newRoot      = nullptr;
	itcPrint     = nullptr;
	nlsStrings   = nullptr;
	ctf          = nullptr;
	eventConverter = nullptr;
	eventFilterMask = 0;

	syncCount = 0;
	caSyncAddr = (TraceDqr::ADDRESS)-1;

	if (settings.tfName == nullptr) {
		printf("Error: Trace::configure(): No trace file name specified\n");
		status = TraceDqr::DQERR_ERR;

		return TraceDqr::DQERR_ERR;
	}

	traceType = TraceDqr::TRACETYPE_BTM;

	pathType = settings.pathType;

	srcbits = settings.srcBits;

	if (settings.filterControlEvents) {
		eventFilterMask = (1 << CTF::et_controlIndex);
	}

	analytics.setSrcBits(srcbits);

	rtdName = new char[strlen(settings.tfName)+1];
	strcpy(rtdName,settings.tfName);

	sfp = new (std::nothrow) SliceFileParser(settings.tfName,srcbits);

	if (sfp == nullptr) {
		printf("Error: Trace::configure(): Could not create SliceFileParser object\n");

		status = TraceDqr::DQERR_ERR;

		return TraceDqr::DQERR_ERR;
	}

	if (sfp->getErr() != TraceDqr::DQERR_OK) {
		printf("Error: Trace::Configure(): Could not open trace file '%s' for input\n",settings.tfName);

		delete sfp;
		sfp = nullptr;

		status = TraceDqr::DQERR_ERR;

		return TraceDqr::DQERR_ERR;
	}

	if (settings.efName != nullptr ) {
		int l = strlen(settings.efName)+1;
		efName = new char[l];
		strcpy(efName,settings.efName);

		// create elf object

		elfReader = new (std::nothrow) ElfReader(settings.efName);

		if (elfReader == nullptr) {
			printf("Error: Trace::Configure(): Could not create ElfReader object\n");

			status = TraceDqr::DQERR_ERR;

			return TraceDqr::DQERR_ERR;
		}

	    if (elfReader->getStatus() != TraceDqr::DQERR_OK) {
	    	if (sfp != nullptr) {
	    		delete sfp;
	    		sfp = nullptr;
	    	}

	    	delete elfReader;
	    	elfReader = nullptr;

	    	status = TraceDqr::DQERR_ERR;

	    	return TraceDqr::DQERR_ERR;
	    }

	    // create disassembler object

	    bfd *abfd;
	    abfd = elfReader->get_bfd();

		disassembler = new (std::nothrow) Disassembler(abfd,settings.labelsAsFunctions);

		if (disassembler == nullptr) {
			printf("Error: Trace::Configure(): Could not creat disassembler object\n");

			status = TraceDqr::DQERR_ERR;

			return TraceDqr::DQERR_ERR;
		}

		if (disassembler->getStatus() != TraceDqr::DQERR_OK) {
			if (sfp != nullptr) {
				delete sfp;
				sfp = nullptr;
			}

			if (elfReader != nullptr) {
				delete elfReader;
				elfReader = nullptr;
			}

			delete disassembler;
			disassembler = nullptr;

			status = TraceDqr::DQERR_ERR;

			return TraceDqr::DQERR_ERR;
		}

		rc = disassembler->setPathType(settings.pathType);
		if (rc != TraceDqr::DQERR_OK) {
			if (sfp != nullptr) {
				delete sfp;
				sfp = nullptr;
			}

			if (elfReader != nullptr) {
				delete elfReader;
				elfReader = nullptr;
			}

			delete disassembler;
			disassembler = nullptr;

			status = rc;
			return rc;
		}

	    // get symbol table

	    symtab = elfReader->getSymtab();
	    if (symtab == nullptr) {
	    	delete elfReader;
	    	elfReader = nullptr;

	    	delete sfp;
	    	sfp = nullptr;

	    	status = TraceDqr::DQERR_ERR;

	    	return TraceDqr::DQERR_ERR;
	    }
	}
	else {
		elfReader = nullptr;
		disassembler = nullptr;
		symtab = nullptr;
	}

	for (int i = 0; (size_t)i < sizeof lastFaddr / sizeof lastFaddr[0]; i++ ) {
		lastFaddr[i] = 0;
	}

	for (int i = 0; (size_t)i < sizeof currentAddress / sizeof currentAddress[0]; i++ ) {
		currentAddress[i] = 0;
	}

	counts = new Count[DQR_MAXCORES];

	for (int i = 0; (size_t)i < sizeof state / sizeof state[0]; i++ ) {
		state[i] = TRACE_STATE_GETFIRSTSYNCMSG;
	}

	readNewTraceMessage = true;
	currentCore = 0;	// as good as eny!

	for (int i = 0; (size_t)i < sizeof lastTime / sizeof lastTime[0]; i++) {
		lastTime[i] = 0;
	}

	for (int i = 0; (size_t)i < sizeof lastCycle / sizeof lastCycle[0]; i++) {
		lastCycle[i] = 0;
	}

	for (int i = 0; (size_t)i < sizeof eCycleCount / sizeof eCycleCount[0]; i++) {
		eCycleCount[i] = 0;
	}

	instructionInfo.CRFlag = TraceDqr::isNone;
	instructionInfo.brFlags = TraceDqr::BRFLAG_none;

	instructionInfo.address = 0;
	instructionInfo.instruction = 0;
	instructionInfo.instSize = 0;

	if (settings.numAddrBits != 0 ) {
		instructionInfo.addrSize = settings.numAddrBits;
	}
	else if (elfReader == nullptr) {
		instructionInfo.addrSize = 0;
	}
	else {
		instructionInfo.addrSize = elfReader->getBitsPerAddress();
	}

	instructionInfo.addrDispFlags = settings.addrDispFlags;

	instructionInfo.addrPrintWidth = (instructionInfo.addrSize + 3) / 4;

	instructionInfo.addressLabel = nullptr;
	instructionInfo.addressLabelOffset = 0;
	instructionInfo.haveOperandAddress = false;
	instructionInfo.operandAddress = 0;
	instructionInfo.operandLabel = nullptr;
	instructionInfo.operandLabelOffset = 0;

	instructionInfo.timestamp = 0;
	instructionInfo.caFlags = TraceDqr::CAFLAG_NONE;
	instructionInfo.pipeCycles = 0;
	instructionInfo.VIStartCycles = 0;
	instructionInfo.VIFinishCycles = 0;

	sourceInfo.sourceFile = nullptr;
	sourceInfo.sourceFunction = nullptr;
	sourceInfo.sourceLineNum = 0;
	sourceInfo.sourceLine = nullptr;

	freq = settings.freq;
	NexusMessage::targetFrequency = settings.freq;

	tsSize = settings.tsSize;

	for (int i = 0; (size_t)i < sizeof enterISR / sizeof enterISR[0]; i++) {
		enterISR[i] = TraceDqr::isNone;
	}

	status = setITCPrintOptions(TraceDqr::ITC_OPT_NLS,4096,0);

	if (settings.itcPrintOpts != TraceDqr::ITC_OPT_NONE) {
		rc = setITCPrintOptions(settings.itcPrintOpts,settings.itcPrintBufferSize,settings.itcPrintChannel);
		if (rc != TraceDqr::DQERR_OK) {
			cleanUp();

			status = rc;
			return status;
		}
	}

	if ((settings.caName != nullptr) && (settings.caType != TraceDqr::CATRACE_NONE)) {
		rc = setCATraceFile(settings.caName,settings.caType);
		if (rc != TraceDqr::DQERR_OK) {
			cleanUp();

			status = rc;
			return status;
		}
	}

	if (settings.CTFConversion != false ) {

		// Do the code below only after setting efName above

		rc = enableCTFConverter(settings.startTime,settings.hostName);
		if (rc != TraceDqr::DQERR_OK) {
			status = rc;
			return status;
		}
	}

	if (settings.eventConversionEnable != false) {

		// Do the code below only after setting efName above

		rc = enableEventConverter();
		if (rc != TraceDqr::DQERR_OK) {
			status = rc;
			return status;
		}
	}

	if ((settings.cutPath != nullptr) || (settings.srcRoot != nullptr)) {
		rc = subSrcPath(settings.cutPath,settings.srcRoot);
		if (rc != TraceDqr::DQERR_OK) {
			status = rc;

			return status;
		}
	}

	return status;
}

void Trace::cleanUp()
{
	for (int i = 0; (size_t)i < (sizeof state / sizeof state[0]); i++) {
		state[i] = TRACE_STATE_DONE;
	}

	if (sfp != nullptr) {
		delete sfp;
		sfp = nullptr;
	}

	if (elfReader != nullptr) {
		delete elfReader;
		elfReader = nullptr;
	}

	if (cutPath != nullptr) {
		delete [] cutPath;
		cutPath = nullptr;
	}

	if (newRoot != nullptr) {
		delete [] newRoot;
		newRoot = nullptr;
	}

	if (rtdName != nullptr) {
		delete [] rtdName;
		rtdName = nullptr;
	}

	if (efName != nullptr) {
		delete [] efName;
		efName = nullptr;
	}

	// do not delete the symtab object!! It is the same symtab object type the elfReader object
	// contains, and deleting the elfRead above will delete the symtab object below!

	if (symtab != nullptr) {
//		delete symtab;
		symtab = nullptr;
	}

	if (itcPrint  != nullptr) {
		delete itcPrint;
		itcPrint = nullptr;
	}

	if (nlsStrings != nullptr) {
		for (int i = 0; i < 32; i++) {
			if (nlsStrings[i].format != nullptr) {
				delete [] nlsStrings[i].format;
				nlsStrings[i].format = nullptr;
			}
		}

		delete [] nlsStrings;
		nlsStrings = nullptr;
	}

	if (counts != nullptr) {
		delete [] counts;
		counts = nullptr;
	}

	if (disassembler != nullptr) {
		delete disassembler;
		disassembler = nullptr;
	}

	if (caTrace != nullptr) {
		delete caTrace;
		caTrace = nullptr;
	}

	if (ctf != nullptr) {
		delete ctf;
		ctf = nullptr;
	}

	if (eventConverter != nullptr) {
		delete eventConverter;
		eventConverter = nullptr;
	}
}

const char *Trace::version()
{
	return DQR_VERSION;
}

int Trace::getArchSize()
{
	if (elfReader == nullptr) {
		return 0;
	}

	return elfReader->getArchSize();
}

int Trace::getAddressSize()
{
	if (elfReader == nullptr) {
		return 0;
	}

	return elfReader->getBitsPerAddress();
}

TraceDqr::DQErr Trace::setTraceType(TraceDqr::TraceType tType)
{
	switch (tType) {
	case TraceDqr::TRACETYPE_BTM:
	case TraceDqr::TRACETYPE_HTM:
		traceType = tType;
		return TraceDqr::DQERR_OK;
	default:
		break;
	}

	return TraceDqr::DQERR_ERR;
}

TraceDqr::DQErr Trace::setPathType(TraceDqr::pathType pt)
{
	pathType = pt;

	if (disassembler != nullptr) {
		TraceDqr::DQErr rc;

		rc = disassembler->setPathType(pt);

		status = rc;
		return rc;
	}

	return TraceDqr::DQERR_ERR;
}

TraceDqr::DQErr Trace::subSrcPath(const char *cutPath,const char *newRoot)
{
	if (this->cutPath != nullptr) {
		delete [] this->cutPath;
		this->cutPath = nullptr;
	}

	if (this->newRoot != nullptr) {
		delete [] this->newRoot;
		this->newRoot = nullptr;
	}

	if (cutPath != nullptr) {
		int l = strlen(cutPath)+1;

		this->cutPath = new char[l];
		strcpy(this->cutPath,cutPath);
	}

	if (newRoot != nullptr) {
		int l = strlen(newRoot)+1;

		this->newRoot = new char[l];
		strcpy(this->newRoot,newRoot);
	}

	if (disassembler != nullptr) {
		TraceDqr::DQErr rc;

		rc = disassembler->subSrcPath(cutPath,newRoot);

		status = rc;
		return rc;
	}

	status = TraceDqr::DQERR_ERR;

	return TraceDqr::DQERR_ERR;
}

TraceDqr::DQErr Trace::setCATraceFile( char *caf_name,TraceDqr::CATraceType catype)
{
	caTrace = new CATrace(caf_name,catype);

	TraceDqr::DQErr rc;
	rc = caTrace->getStatus();
	if (rc != TraceDqr::DQERR_OK) {
		status = rc;
		return rc;
	}

	// need to sync up ca trace file and trace file. Here, or in next instruction?

	for (int i = 0; (size_t)i < sizeof state / sizeof state[0]; i++ ) {
		state[i] = TRACE_STATE_SYNCCATE;
	}

	return status;
}

TraceDqr::DQErr Trace::enableCTFConverter(int64_t startTime,char *hostName)
{
	if (ctf != nullptr) {
		delete ctf;
		ctf = nullptr;
	}

	if (efName == nullptr) {
		return TraceDqr::DQERR_ERR;
	}

	ctf = new CTFConverter(efName,rtdName,1 << srcbits,getArchSize(),freq,startTime,hostName);

	status = ctf->getStatus();
	if (status != TraceDqr::DQERR_OK) {
		return status;
	}

	return status;
}

TraceDqr::DQErr Trace::enableEventConverter()
{
	if (eventConverter != nullptr) {
		delete eventConverter;
		eventConverter = nullptr;
	}

	if (efName == nullptr) {
		return TraceDqr::DQERR_ERR;
	}

	eventConverter = new EventConverter(efName,rtdName,1 << srcbits,freq);

	status = eventConverter->getStatus();
	if (status != TraceDqr::DQERR_OK) {
		return status;
	}

	return status;
}

TraceDqr::DQErr Trace::setTSSize(int size)
{
	tsSize = size;

	return TraceDqr::DQERR_OK;
}

TraceDqr::DQErr Trace::setLabelMode(bool labelsAreFuncs)
{
	if (elfReader == nullptr) {
		status = TraceDqr::DQERR_ERR;
		return status;
	}

	bfd *abfd;
	abfd = elfReader->get_bfd();

	if (disassembler != nullptr) {
		delete disassembler;
		disassembler = nullptr;
	}

	disassembler = new (std::nothrow) Disassembler(abfd,labelsAreFuncs);

	if (disassembler == nullptr) {
		printf("Error: Trace::setLabelMode(): Could not create disassembler object\n");

		status = TraceDqr::DQERR_ERR;
		return status;
	}

	if (disassembler->getStatus() != TraceDqr::DQERR_OK) {
		if (elfReader != nullptr) {
			delete elfReader;
			elfReader = nullptr;
		}

		delete disassembler;
		disassembler = nullptr;

		status = TraceDqr::DQERR_ERR;

		return status;
	}

	TraceDqr::DQErr rc;

	rc = disassembler->subSrcPath(cutPath,newRoot);
	if (rc != TraceDqr::DQERR_OK) {
		status = rc;

		return rc;
	}

	status = TraceDqr::DQERR_OK;

	return status;
}

TraceDqr::TIMESTAMP Trace::processTS(TraceDqr::tsType tstype, TraceDqr::TIMESTAMP lastTs, TraceDqr::TIMESTAMP newTs)
{
	TraceDqr::TIMESTAMP ts;

	if (tstype == TraceDqr::TS_full) {
		// add in the wrap from previous timestamps
		ts = newTs + (lastTs & (~((((TraceDqr::TIMESTAMP)1) << tsSize)-1)));
	}
	else if (lastTs != 0) {
		ts = lastTs ^ newTs;
	}
	else {
		ts = 0;
	}

	if (ts < lastTs) {
		// adjust for wrap
		ts += ((TraceDqr::TIMESTAMP)1) << tsSize;
	}

	return ts;
}

TraceDqr::DQErr Trace::getNumBytesInSWTQ(int &numBytes)
{
	if (sfp == nullptr) {
		return TraceDqr::DQERR_ERR;
	}

	return sfp->getNumBytesInSWTQ(numBytes);
}

TraceDqr::DQErr Trace::getTraceFileOffset(int &size,int &offset)
{
	return sfp->getFileOffset(size,offset);
}

int Trace::getITCPrintMask()
{
	if (itcPrint == nullptr) {
		return 0;
	}

	return itcPrint->getITCPrintMask();
}

int Trace::getITCFlushMask()
{
	if (itcPrint == nullptr) {
		return 0;
	}

	return itcPrint->getITCFlushMask();
}

TraceDqr::ADDRESS Trace::computeAddress()
{
	switch (nm.tcode) {
	case TraceDqr::TCODE_DEBUG_STATUS:
		break;
	case TraceDqr::TCODE_DEVICE_ID:
		break;
	case TraceDqr::TCODE_OWNERSHIP_TRACE:
		break;
	case TraceDqr::TCODE_DIRECT_BRANCH:
//		currentAddress = target of branch.
		break;
	case TraceDqr::TCODE_INDIRECT_BRANCH:
		currentAddress[currentCore] = currentAddress[currentCore] ^ (nm.indirectBranch.u_addr << 1);	// note - this is the next address!
		break;
	case TraceDqr::TCODE_DATA_WRITE:
		break;
	case TraceDqr::TCODE_DATA_READ:
		break;
	case TraceDqr::TCODE_DATA_ACQUISITION:
		break;
	case TraceDqr::TCODE_ERROR:
		break;
	case TraceDqr::TCODE_SYNC:
		currentAddress[currentCore] = nm.sync.f_addr << 1;
		break;
	case TraceDqr::TCODE_CORRECTION:
		break;
	case TraceDqr::TCODE_DIRECT_BRANCH_WS:
		currentAddress[currentCore] = nm.directBranchWS.f_addr << 1;
		break;
	case TraceDqr::TCODE_INDIRECT_BRANCH_WS:
		currentAddress[currentCore] = nm.indirectBranchWS.f_addr << 1;
		break;
	case TraceDqr::TCODE_DATA_WRITE_WS:
		break;
	case TraceDqr::TCODE_DATA_READ_WS:
		break;
	case TraceDqr::TCODE_WATCHPOINT:
		break;
	case TraceDqr::TCODE_CORRELATION:
		break;
	case TraceDqr::TCODE_INDIRECTBRANCHHISTORY:
		currentAddress[currentCore] = currentAddress[currentCore] ^ (nm.indirectHistory.u_addr << 1);	// note - this is the next address!
		break;
	case TraceDqr::TCODE_INDIRECTBRANCHHISTORY_WS:
		currentAddress[currentCore] = nm.indirectHistoryWS.f_addr << 1;
		break;
	case TraceDqr::TCODE_RESOURCEFULL:
		break;
	default:
		break;
	}

	std::cout << "New address 0x" << std::hex << currentAddress[currentCore] << std::dec << std::endl;

	return currentAddress[currentCore];
}

int Trace::Disassemble(TraceDqr::ADDRESS addr)
{
	if (disassembler == nullptr) {
		printf("Error: Trace::Disassemble(): No disassembler object\n");

		status = TraceDqr::DQERR_ERR;

		return 0;
	}

	int   rc;
	TraceDqr::DQErr s;

	rc = disassembler->Disassemble(addr);

	s = disassembler->getStatus();

	if (s != TraceDqr::DQERR_OK ) {
	  status = s;
	  return 0;
	}

	// the two lines below copy each structure completely. This is probably
	// pretty inefficient, and just returning pointers and using pointers
	// would likely be better

	instructionInfo = disassembler->getInstructionInfo();
	sourceInfo = disassembler->getSourceInfo();

	return rc;
}

const char *Trace::getSymbolByAddress(TraceDqr::ADDRESS addr)
{
	return symtab->getSymbolByAddress(addr);
}

const char *Trace::getNextSymbolByAddress()
{
	return symtab->getNextSymbolByAddress();
}

TraceDqr::DQErr Trace::setITCPrintOptions(int itcFlags,int buffSize,int channel)
{
	if (itcPrint != nullptr) {
		delete itcPrint;
		itcPrint = nullptr;
	}

	if (itcFlags != TraceDqr::ITC_OPT_NONE) {
		if ((nlsStrings == nullptr) && (elfReader != nullptr)) {
			TraceDqr::DQErr rc;

			nlsStrings = new TraceDqr::nlStrings[32];

			rc = elfReader->parseNLSStrings(nlsStrings);
			if (rc != TraceDqr::DQERR_OK) {
				status = rc;

				delete [] nlsStrings;
				nlsStrings = nullptr;

				return rc;
			}
		}

		itcPrint = new ITCPrint(itcFlags,1 << srcbits,buffSize,channel,nlsStrings);
	}

	return TraceDqr::DQERR_OK;
}

TraceDqr::DQErr Trace::haveITCPrintData(int numMsgs[DQR_MAXCORES], bool havePrintData[DQR_MAXCORES])
{
	if (itcPrint == nullptr) {
		return TraceDqr::DQERR_ERR;
	}

	itcPrint->haveITCPrintData(numMsgs, havePrintData);

	return TraceDqr::DQERR_OK;
}

bool Trace::getITCPrintMsg(int core, char *dst, int dstLen, TraceDqr::TIMESTAMP &startTime, TraceDqr::TIMESTAMP &endTime)
{
	if (itcPrint == nullptr) {
		return false;
	}

	return itcPrint->getITCPrintMsg(core,dst,dstLen,startTime,endTime);
}

bool Trace::flushITCPrintMsg(int core, char *dst, int dstLen,TraceDqr::TIMESTAMP &startTime,TraceDqr::TIMESTAMP &endTime)
{
	if (itcPrint == nullptr) {
		return false;
	}

	return itcPrint->flushITCPrintMsg(core,dst,dstLen,startTime,endTime);
}

std::string Trace::getITCPrintStr(int core, bool &haveData,TraceDqr::TIMESTAMP &startTime,TraceDqr::TIMESTAMP &endTime)
{
	std::string s = "";

	if (itcPrint == nullptr) {
		haveData = false;
	}
	else {
		haveData = itcPrint->getITCPrintStr(core,s,startTime,endTime);
	}

	return s;
}

std::string Trace::getITCPrintStr(int core, bool &haveData,double &startTime,double &endTime)
{
	std::string s = "";
	TraceDqr::TIMESTAMP sts, ets;

	if (itcPrint == nullptr) {
		haveData = false;
	}
	else {
		haveData = itcPrint->getITCPrintStr(core,s,sts,ets);

		if (haveData != false) {
			if (NexusMessage::targetFrequency != 0) {
				startTime = ((double)sts)/NexusMessage::targetFrequency;
				endTime = ((double)ets)/NexusMessage::targetFrequency;
			}
			else {
				startTime = sts;
				endTime = ets;
			}
		}
	}

	return s;
}

std::string Trace::flushITCPrintStr(int core, bool &haveData,TraceDqr::TIMESTAMP &startTime,TraceDqr::TIMESTAMP &endTime)
{
	std::string s = "";

	if (itcPrint == nullptr) {
		haveData = false;
	}
	else {
		haveData = itcPrint->flushITCPrintStr(core,s,startTime,endTime);
	}

	return s;
}

std::string Trace::flushITCPrintStr(int core, bool &haveData,double &startTime,double &endTime)
{
	std::string s = "";
	TraceDqr::TIMESTAMP sts, ets;

	if (itcPrint == nullptr) {
		haveData = false;
	}
	else {
		haveData = itcPrint->flushITCPrintStr(core,s,sts,ets);

		if (haveData != false) {
			if (NexusMessage::targetFrequency != 0) {
				startTime = ((double)sts)/NexusMessage::targetFrequency;
				endTime = ((double)ets)/NexusMessage::targetFrequency;
			}
			else {
				startTime = sts;
				endTime = ets;
			}
		}
	}

	return s;
}

// This routine only works for event traces! In particular, brFlags will assumes there is an
// event message at addresses for conditional branches because the branch was taken!

TraceDqr::DQErr Trace::getCRBRFlags(TraceDqr::ICTReason cksrc,TraceDqr::ADDRESS addr,int &crFlag,int &brFlag)
{
	int rc;
	TraceDqr::DQErr ec;
	uint32_t inst;
	int inst_size;
	TraceDqr::InstType inst_type;
	int32_t immediate;
	bool isBranch;
	TraceDqr::Reg rs1;
	TraceDqr::Reg rd;

	//	Need to get the destination of the call, which is in the immediate field

	crFlag = TraceDqr::isNone;
	brFlag = TraceDqr::BRFLAG_none;

	switch (cksrc) {
	case TraceDqr::ICT_CONTROL:
	case TraceDqr::ICT_EXT_TRIG:
	case TraceDqr::ICT_WATCHPOINT:
	case TraceDqr::ICT_PC_SAMPLE:
		break;
	case TraceDqr::ICT_INFERABLECALL:
		ec = elfReader->getInstructionByAddress(addr,inst);
		if (ec != TraceDqr::DQERR_OK) {
			printf("Error: getCRBRFlags() failed\n");

			status = ec;
			return ec;
		}

		rc = decodeInstruction(inst,inst_size,inst_type,rs1,rd,immediate,isBranch);
		if (rc != 0) {
			printf("Error: getCRBRFlags(): Cann't decode size of instruction %04x\n",inst);

			status = TraceDqr::DQERR_ERR;
			return TraceDqr::DQERR_ERR;
		}

		switch (inst_type) {
		case TraceDqr::INST_JALR:
			if ((rd == TraceDqr::REG_1) || (rd == TraceDqr::REG_5)) { // rd == link
				if ((rs1 != TraceDqr::REG_1) && (rs1 != TraceDqr::REG_5)) { // rd == link; rs1 != link
					crFlag = TraceDqr::isCall;
				}
				else if (rd != rs1) { // rd == link; rs1 == link; rd != rs1
					crFlag = TraceDqr::isSwap;
				}
				else { // rd == link; rs1 == link; rd == rs1
					crFlag = TraceDqr::isCall;
				}
			}
			else if ((rs1 == TraceDqr::REG_1) || (rs1 == TraceDqr::REG_5)) { // rd != link; rs1 == link
				crFlag = TraceDqr::isReturn;
			}
			break;
		case TraceDqr::INST_JAL:
			if ((rd == TraceDqr::REG_1) || (rd == TraceDqr::REG_5)) { // rd == link
				crFlag = TraceDqr::isCall;
			}
			break;
		case TraceDqr::INST_C_JAL:
			if ((rd == TraceDqr::REG_1) || (rd == TraceDqr::REG_5)) { // rd == link
				crFlag = TraceDqr::isCall;
			}
			break;
		case TraceDqr::INST_C_JR:
			if ((rs1 == TraceDqr::REG_1) || (rs1 == TraceDqr::REG_5)) {
				crFlag = TraceDqr::isReturn;
			}
			break;
		case TraceDqr::INST_EBREAK:
		case TraceDqr::INST_ECALL:
			crFlag = TraceDqr::isException;
			break;
		case TraceDqr::INST_MRET:
		case TraceDqr::INST_SRET:
		case TraceDqr::INST_URET:
			crFlag = TraceDqr::isExceptionReturn;
			break;
		case TraceDqr::INST_BEQ:
		case TraceDqr::INST_BNE:
		case TraceDqr::INST_BLT:
		case TraceDqr::INST_BGE:
		case TraceDqr::INST_BLTU:
		case TraceDqr::INST_BGEU:
		case TraceDqr::INST_C_BEQZ:
		case TraceDqr::INST_C_BNEZ:
			brFlag = TraceDqr::BRFLAG_taken;
			break;
		default:
			break;
		}
		break;
	case TraceDqr::ICT_EXCEPTION:
		crFlag = TraceDqr::isException;
		break;
	case TraceDqr::ICT_INTERRUPT:
		crFlag = TraceDqr::isInterrupt;
		break;
	case TraceDqr::ICT_CONTEXT:
		crFlag = TraceDqr::isSwap;
		break;
	default:
		printf("Error: getCRBRFlags(): Invalid crsrc\n");

		status = TraceDqr::DQERR_ERR;
		return TraceDqr::DQERR_ERR;
	}

	return TraceDqr::DQERR_OK;
}

// Note: This next instruction only computes nextAddr for inferable calls. It does set the crFlag
// correctly for others.

TraceDqr::DQErr Trace::nextAddr(TraceDqr::ADDRESS addr,TraceDqr::ADDRESS &nextAddr,int &crFlag)
{
	int rc;
	TraceDqr::DQErr ec;
	uint32_t inst;
	int inst_size;
	TraceDqr::InstType inst_type;
	int32_t immediate;
	bool isBranch;
	TraceDqr::Reg rs1;
	TraceDqr::Reg rd;

	ec = elfReader->getInstructionByAddress(addr,inst);
	if (ec != TraceDqr::DQERR_OK) {
		printf("Error: nextAddr() failed\n");

		status = ec;
		return ec;
	}

	//	Need to get the destination of the call, which is in the immediate field

	crFlag = TraceDqr::isNone;
	nextAddr = 0;

	rc = decodeInstruction(inst,inst_size,inst_type,rs1,rd,immediate,isBranch);
	if (rc != 0) {
		printf("Error: Cann't decode size of instruction %04x\n",inst);

		status = TraceDqr::DQERR_ERR;
		return TraceDqr::DQERR_ERR;
	}

	switch (inst_type) {
	case TraceDqr::INST_JALR:
		if ((rd == TraceDqr::REG_1) || (rd == TraceDqr::REG_5)) { // rd == link
			if ((rs1 != TraceDqr::REG_1) && (rs1 != TraceDqr::REG_5)) { // rd == link; rs1 != link
				crFlag |= TraceDqr::isCall;
			}
			else if (rd != rs1) { // rd == link; rs1 == link; rd != rs1
				crFlag |= TraceDqr::isSwap;
			}
			else { // rd == link; rs1 == link; rd == rs1
				crFlag |= TraceDqr::isCall;
			}
		}
		else if ((rs1 == TraceDqr::REG_1) || (rs1 == TraceDqr::REG_5)) { // rd != link; rs1 == link
			crFlag |= TraceDqr::isReturn;
		}
		break;
	case TraceDqr::INST_JAL:
		if ((rd == TraceDqr::REG_1) || (rd == TraceDqr::REG_5)) { // rd == link
			crFlag = TraceDqr::isCall;
		}

		nextAddr = addr + immediate;
		break;
	case TraceDqr::INST_C_JAL:
		if ((rd == TraceDqr::REG_1) || (rd == TraceDqr::REG_5)) { // rd == link
			crFlag = TraceDqr::isCall;
		}

		nextAddr = addr + immediate;
		break;
	case TraceDqr::INST_C_JR:
		// pc = pc + rs1
		// not inferrable unconditional

		if ((rs1 == TraceDqr::REG_1) || (rs1 == TraceDqr::REG_5)) {
			crFlag |= TraceDqr::isReturn;
		}
		break;
	case TraceDqr::INST_C_JALR:
		if (rs1 == TraceDqr::REG_5) { // is it reg5 only, or also reg1 like non-compact JALR?
			crFlag |= TraceDqr::isSwap;
		}
		else {
			crFlag |= TraceDqr::isCall;
		}
		break;
	case TraceDqr::INST_EBREAK:
	case TraceDqr::INST_ECALL:
		crFlag = TraceDqr::isException;
		break;
	case TraceDqr::INST_MRET:
	case TraceDqr::INST_SRET:
	case TraceDqr::INST_URET:
		crFlag = TraceDqr::isExceptionReturn;
		break;
	default:
		printf("Error: Trace::nextAddr(): Instruction at 0x%08x is not a JAL, JALR, C_JAL, C_JR, C_JALR, EBREAK, ECALL, MRET, SRET, or URET\n",addr);

#ifdef foodog
		printf("Instruction type: %d\n",inst_type);

		// disassemble and display instruction

		Disassemble(addr);

		char dst[256];
		instructionInfo.addressToText(dst,sizeof dst,0);

		if (instructionInfo.addressLabel != nullptr) {
			printf("<%s",instructionInfo.addressLabel);
			if (instructionInfo.addressLabelOffset != 0) {
				printf("+%x",instructionInfo.addressLabelOffset);
			}
			printf(">\n");
		}

		printf("    %s:    ",dst);

		instructionInfo.instructionToText(dst,sizeof dst,2);
		printf("  %s\n",dst);
#endif // foodog

		status = TraceDqr::DQERR_ERR;
		return TraceDqr::DQERR_ERR;
	}

	return TraceDqr::DQERR_OK;
}

// this function takes the starting address and runs one instruction only!!
// The result is the address it stops at. It also consumes the counts (i-cnt,
// history, taken, not-taken) when appropriate!

TraceDqr::DQErr Trace::nextAddr(int core,TraceDqr::ADDRESS addr,TraceDqr::ADDRESS &pc,TraceDqr::TCode tcode,int &crFlag,TraceDqr::BranchFlags &brFlag)
{
	TraceDqr::CountType ct;
	uint32_t inst;
	int inst_size;
	TraceDqr::InstType inst_type;
	int32_t immediate;
	bool isBranch;
	int rc;
	TraceDqr::Reg rs1;
	TraceDqr::Reg rd;
	bool isTaken;

	status = elfReader->getInstructionByAddress(addr,inst);
	if (status != TraceDqr::DQERR_OK) {
		printf("Error: nextAddr(): getInstructionByAddress() failed\n");

		return status;
	}

	crFlag = TraceDqr::isNone;
	brFlag = TraceDqr::BRFLAG_none;

	// figure out how big the instruction is
	// Note: immediate will already be adjusted - don't need to mult by 2 before adding to address

	rc = decodeInstruction(inst,inst_size,inst_type,rs1,rd,immediate,isBranch);
	if (rc != 0) {
		printf("Error: nextAddr(): Cannot decode instruction %04x\n",inst);

		status = TraceDqr::DQERR_ERR;

		return status;
	}

	switch (inst_type) {
	case TraceDqr::INST_UNKNOWN:
		// btm and htm same

		pc = addr + inst_size/8;
		break;
	case TraceDqr::INST_JAL:
		// btm and htm same

		// rd = pc+4 (rd can be r0)
		// pc = pc + (sign extended immediate offset)
		// plan unconditional jumps use rd -> r0
		// inferrable unconditional

		if ((rd == TraceDqr::REG_1) || (rd == TraceDqr::REG_5)) { // rd == link
			counts->push(core,addr + inst_size/8);
			if (globalDebugFlag) printf("Debug: call: core %d, pushing address %08llx, %d item now on stack\n",core,addr+inst_size/8,counts->getNumOnStack(core));
			crFlag |= TraceDqr::isCall;
		}

		pc = addr + immediate;
		break;
	case TraceDqr::INST_JALR:
		// btm: indirect branch; return pc = -1
		// htm: indirect branch with history; return pc = pop'd addr if possible, else -1

		// rd = pc+4 (rd can be r0)
		// pc = pc + ((sign extended immediate offset) + rs) & 0xffe
		// plain unconditional jumps use rd -> r0
		// not inferrable unconditional

		if ((rd == TraceDqr::REG_1) || (rd == TraceDqr::REG_5)) { // rd == link
			if ((rs1 != TraceDqr::REG_1) && (rs1 != TraceDqr::REG_5)) { // rd == link; rs1 != link
				counts->push(core,addr+inst_size/8);
				if (globalDebugFlag) printf("Debug: indirect call: core %d, pushing address %08llx, %d item now on stack\n",core,addr+inst_size/8,counts->getNumOnStack(core));
				pc = -1;
				crFlag |= TraceDqr::isCall;
			}
			else if (rd != rs1) { // rd == link; rs1 == link; rd != rs1
				pc = counts->pop(core);
				counts->push(core,addr+inst_size/8);
				if (globalDebugFlag) printf("Debug: indirect call: core %d, pushing address %08llx, %d item now on stack\n",core,addr+inst_size/8,counts->getNumOnStack(core));
				crFlag |= TraceDqr::isSwap;
			}
			else { // rd == link; rs1 == link; rd == rs1
				counts->push(core,addr+inst_size/8);
				if (globalDebugFlag) printf("Debug: indirect call: core %d, pushing address %08llx, %d item now on stack\n",core,addr+inst_size/8,counts->getNumOnStack(core));
				pc = -1;
				crFlag |= TraceDqr::isCall;
			}
		}
		else if ((rs1 == TraceDqr::REG_1) || (rs1 == TraceDqr::REG_5)) { // rd != link; rs1 == link
			pc = counts->pop(core);
			if (globalDebugFlag) printf("Debug: return: core %d, new address %08llx, %d item now on stack\n",core,pc,counts->getNumOnStack(core));
			crFlag |= TraceDqr::isReturn;
		}
		else {
			pc = -1;
		}

		// Try to tell if this is a btm or htm based on counts and isReturn | isSwap

		if (traceType == TraceDqr::TRACETYPE_BTM) {
			if (crFlag & (TraceDqr::isReturn | TraceDqr::isSwap)) {
				if (counts->consumeICnt(core,0) > inst_size / 16) {
					traceType = TraceDqr::TRACETYPE_HTM;
					if (globalDebugFlag) printf("JALR: switching to HTM trace\n");
				}
			}
		}

		if (traceType == TraceDqr::TRACETYPE_BTM) {
			if (counts->consumeICnt(core,0) > inst_size / 16) {
				// this handles the case of jumping to the instruction following the jump!

				pc = addr + inst_size/8;
			}
			else {
				pc = -1;
			}
		}
		break;
	case TraceDqr::INST_BEQ:
	case TraceDqr::INST_BNE:
	case TraceDqr::INST_BLT:
	case TraceDqr::INST_BGE:
	case TraceDqr::INST_BLTU:
	case TraceDqr::INST_BGEU:
	case TraceDqr::INST_C_BEQZ:
	case TraceDqr::INST_C_BNEZ:
		// htm: follow history bits
		// btm: there will only be a trace record following this for taken branch. not taken branches are not
		// reported. If btm mode, we can look at i-count. If it is going to go to 0, branch was taken (direct branch message
		// will follow). If not going to 0, not taken

		// pc = pc + (sign extend immediate offset) (BLTU and BGEU are not sign extended)
		// inferrable conditional

		if (traceType == TraceDqr::TRACETYPE_HTM) {
			// htm mode
			ct = counts->getCurrentCountType(core);
			switch (ct) {
			case TraceDqr::COUNTTYPE_none:
				printf("Error: nextAddr(): instruction counts consumed\n");

				return TraceDqr::DQERR_ERR;
			case TraceDqr::COUNTTYPE_i_cnt:
				if (globalDebugFlag) printf("Debug: Conditional branch: No history. I-cnt: %d\n",counts->getICnt(core));

				// don't know if the branch is taken or not, so we don't know the next addr

				// This can happen with resource full messages where an i-cnt type resource full
				// may be emitted by the encoder due to i-cnt overflow, and it still have non-emitted
				// history bits. We will need to keep reading trace messages until we get a some
				// history. The current trace message should be retired.

				// this is not an error. Just keep retrying until we get a trace message that
				// kicks things loose again

				pc = -1;

				// The caller can detect this has happened and read a new trace message and retry, by
				// checking the brFlag for BRFLAG_unkown

				brFlag = TraceDqr::BRFLAG_unknown;
				break;
			case TraceDqr::COUNTTYPE_history:
				//consume history bit here and set pc accordingly

				if (globalDebugFlag) printf("Debug: Conditional branch: Have history, taken mask: %08x, bit %d, taken: %d\n",counts->getHistory(core),counts->getNumHistoryBits(core),counts->isTaken(core));

				rc = counts->consumeHistory(core,isTaken);
				if ( rc != 0) {
					printf("Error: nextAddr(): consumeHistory() failed\n");

					status = TraceDqr::DQERR_ERR;

					return status;
				}

				if (isTaken) {
					pc = addr + immediate;
					brFlag = TraceDqr::BRFLAG_taken;
				}
				else {
					pc = addr + inst_size / 8;
					brFlag = TraceDqr::BRFLAG_notTaken;
				}
				break;
			case TraceDqr::COUNTTYPE_taken:
				if (globalDebugFlag) printf("Debug: Conditional branch: Have takenCount: %d, taken: %d\n",counts->getTakenCount(core), counts->getTakenCount(core) > 0);

				rc = counts->consumeTakenCount(core);
				if ( rc != 0) {
					printf("Error: nextAddr(): consumeTakenCount() failed\n");

					status = TraceDqr::DQERR_ERR;

					return status;
				}

				pc = addr + immediate;
				brFlag = TraceDqr::BRFLAG_taken;
				break;
			case TraceDqr::COUNTTYPE_notTaken:
				if (globalDebugFlag) printf("Debug: Conditional branch: Have notTakenCount: %d, not taken: %d\n",counts->getNotTakenCount(core), counts->getNotTakenCount(core) > 0);

				rc = counts->consumeNotTakenCount(core);
				if ( rc != 0) {
					printf("Error: nextAddr(): consumeTakenCount() failed\n");

					status = TraceDqr::DQERR_ERR;

					return status;
				}

				pc = addr + inst_size / 8;
				brFlag = TraceDqr::BRFLAG_notTaken;
				break;
			}
		}
		else {
			// btm mode

			// if i-cnts don't go to zero for this instruction, this branch is not taken in btmmode.
			// if i-cnts go to zero for this instruciotn, it might be a taken branch. Need to look
			// at the tcode for the current nexus message. If it is a direct branch with or without
			// sync, it is taken. Otherwise, not taken (it could be the result of i-cnt reaching the
			// limit, forcing a sync type message, but branch not taken).

			if (counts->consumeICnt(core,0) > inst_size / 16) {
				// not taken

				pc = addr + inst_size / 8;

				brFlag = TraceDqr::BRFLAG_notTaken;
			}
			else if ((tcode == TraceDqr::TCODE_DIRECT_BRANCH) || (tcode == TraceDqr::TCODE_DIRECT_BRANCH_WS)) {
				// taken

				pc = addr + immediate;

				brFlag = TraceDqr::BRFLAG_taken;
			}
			else {
				// not taken

				pc = addr + inst_size / 8;

				brFlag = TraceDqr::BRFLAG_notTaken;
			}
		}
		break;
	case TraceDqr::INST_C_J:
		// btm, htm same

		// pc = pc + (signed extended immediate offset)
		// inferrable unconditional

		pc = addr + immediate;
		break;
	case TraceDqr::INST_C_JAL:
		// btm, htm same

		// x1 = pc + 2
		// pc = pc + (signed extended immediate offset)
		// inferrable unconditional

		if ((rd == TraceDqr::REG_1) || (rd == TraceDqr::REG_5)) { // rd == link
			counts->push(core,addr + inst_size/8);
			if (globalDebugFlag) printf("Debug: call: core %d, pushing address %08llx, %d item now on stack\n",core,addr+inst_size/8,counts->getNumOnStack(core));
			crFlag |= TraceDqr::isCall;
		}

		pc = addr + immediate;
		break;
	case TraceDqr::INST_C_JR:
		// pc = pc + rs1
		// not inferrable unconditional

		if ((rs1 == TraceDqr::REG_1) || (rs1 == TraceDqr::REG_5)) {
			pc = counts->pop(core);
			if (globalDebugFlag) printf("Debug: return: core %d, new address %08llx, %d item now on stack\n",core,pc,counts->getNumOnStack(core));
			crFlag |= TraceDqr::isReturn;
		}
		else {
			pc = -1;
		}

		// Try to tell if this is a btm or htm based on counts and isReturn

		if (traceType == TraceDqr::TRACETYPE_BTM) {
			if (crFlag & TraceDqr::isReturn) {
				if (counts->consumeICnt(core,0) > inst_size / 16) {
					traceType = TraceDqr::TRACETYPE_HTM;
					if (globalDebugFlag) printf("C_JR: switching to HTM trace\n");
				}
			}
		}

		if (traceType == TraceDqr::TRACETYPE_BTM) {
			if (counts->consumeICnt(core,0) > inst_size / 16) {
				// this handles the case of jumping to the instruction following the jump!

				pc = addr + inst_size / 8;
			}
			else {
				pc = -1;
			}
		}
		break;
	case TraceDqr::INST_C_JALR:
		// x1 = pc + 2
		// pc = pc + rs1
		// not inferrble unconditional

		if (rs1 == TraceDqr::REG_5) {
			pc = counts->pop(core);
			counts->push(core,addr+inst_size/8);
			if (globalDebugFlag) printf("Debug: return/call: core %d, new address %08llx, pushing %08xllx, %d item now on stack\n",core,pc,addr+inst_size/8,counts->getNumOnStack(core));
			crFlag |= TraceDqr::isSwap;
		}
		else {
			counts->push(core,addr+inst_size/8);
			if (globalDebugFlag) printf("Debug: call: core %d, new address %08llx (don't know dst yet), pushing %08llx, %d item now on stack\n",core,pc,addr+inst_size/8,counts->getNumOnStack(core));
			pc = -1;
			crFlag |= TraceDqr::isCall;
		}

		// Try to tell if this is a btm or htm based on counts and isSwap

		if (traceType == TraceDqr::TRACETYPE_BTM) {
			if (crFlag & TraceDqr::isSwap) {
				if (counts->consumeICnt(core,0) > inst_size / 16) {
					traceType = TraceDqr::TRACETYPE_HTM;
					if (globalDebugFlag) printf("C_JALR: switching to HTM trace\n");
				}
			}
		}

		if (traceType == TraceDqr::TRACETYPE_BTM) {
			if (counts->consumeICnt(core,0) > inst_size / 16) {
				// this handles the case of jumping to the instruction following the jump!

				pc = addr + inst_size / 8;
			}
			else {
				pc = -1;
			}
		}
		break;
	case TraceDqr::INST_EBREAK:
	case TraceDqr::INST_ECALL:
		crFlag |= TraceDqr::isException;
		pc = -1;
		break;
	case TraceDqr::INST_MRET:
	case TraceDqr::INST_SRET:
	case TraceDqr::INST_URET:
		crFlag |= TraceDqr::isExceptionReturn;
		pc = -1;
		break;
	default:
		pc = addr + inst_size / 8;
		break;
	}

	// Always consume i-cnt unless brFlag == BRFLAG_unknown because we will retry computing next
	// addr for this instruction later

	if (brFlag != TraceDqr::BRFLAG_unknown) {
		counts->consumeICnt(core,inst_size/16);
	}

	return TraceDqr::DQERR_OK;
}

TraceDqr::DQErr Trace::nextCAAddr(TraceDqr::ADDRESS &addr,TraceDqr::ADDRESS &savedAddr)
{
	uint32_t inst;
	int inst_size;
	TraceDqr::InstType inst_type;
	int32_t immediate;
	bool isBranch;
	int rc;
	TraceDqr::Reg rs1;
	TraceDqr::Reg rd;
//	bool isTaken;

	// note: since saveAddr is a single address, we are only implementing a one address stack (not much of a stack)

	status = elfReader->getInstructionByAddress(addr,inst);
	if (status != TraceDqr::DQERR_OK) {
		printf("Error: nextCAAddr(): getInstructionByAddress() failed\n");

		return status;
	}

	// figure out how big the instruction is
	// Note: immediate will already be adjusted - don't need to mult by 2 before adding to address

	rc = decodeInstruction(inst,inst_size,inst_type,rs1,rd,immediate,isBranch);
	if (rc != 0) {
		printf("Error: nextCAAddr(): Cannot decode instruction %04x\n",inst);

		status = TraceDqr::DQERR_ERR;

		return status;
	}

	switch (inst_type) {
	case TraceDqr::INST_UNKNOWN:
		addr = addr + inst_size/8;
		break;
	case TraceDqr::INST_JAL:
		if ((rd == TraceDqr::REG_1) || (rd == TraceDqr::REG_5)) { // rd == link
			savedAddr = addr + inst_size/8;
		}

		addr = addr + immediate;
		break;
	case TraceDqr::INST_JALR:
		if ((rd == TraceDqr::REG_1) || (rd == TraceDqr::REG_5)) { // rd == link
			if ((rs1 != TraceDqr::REG_1) && (rs1 != TraceDqr::REG_5)) { // rd == link; rs1 != link
				savedAddr = addr+inst_size/8;
				addr = -1;
			}
			else if (rd != rs1) { // rd == link; rs1 == link; rd != rs1
				addr = savedAddr;
				savedAddr = -1;
			}
			else { // rd == link; rs1 == link; rd == rs1
				savedAddr = addr+inst_size/8;
				addr = -1;
			}
		}
		else if ((rs1 == TraceDqr::REG_1) || (rs1 == TraceDqr::REG_5)) { // rd != link; rs1 == link
			addr = savedAddr;
			savedAddr = -1;
		}
		else {
			addr = -1;
		}
		break;
	case TraceDqr::INST_BEQ:
	case TraceDqr::INST_BNE:
	case TraceDqr::INST_BLT:
	case TraceDqr::INST_BGE:
	case TraceDqr::INST_BLTU:
	case TraceDqr::INST_BGEU:
	case TraceDqr::INST_C_BEQZ:
	case TraceDqr::INST_C_BNEZ:
		if ((addr + inst_size/8) == (addr + immediate)) {
			addr += immediate;
		}
		else {
			addr = -1;
		}
		break;
	case TraceDqr::INST_C_J:
		addr += immediate;
		break;
	case TraceDqr::INST_C_JAL:
		if ((rd == TraceDqr::REG_1) || (rd == TraceDqr::REG_5)) { // rd == link
			savedAddr = addr + inst_size/8;
		}

		addr += immediate;
		break;
	case TraceDqr::INST_C_JR:
		if ((rs1 == TraceDqr::REG_1) || (rs1 == TraceDqr::REG_5)) {
			addr = savedAddr;
			savedAddr = -1;
		}
		else {
			addr = -1;
		}
		break;
	case TraceDqr::INST_C_JALR:
		if (rs1 == TraceDqr::REG_5) {
			TraceDqr::ADDRESS taddr;

			// swap addr, saveAddr
			taddr = addr;
			addr = savedAddr;
			savedAddr = taddr;
		}
		else {
			savedAddr = addr+inst_size/8;
			addr = -1;
		}
		break;
	case TraceDqr::INST_EBREAK:
	case TraceDqr::INST_ECALL:
		addr = -1;
		break;
	case TraceDqr::INST_MRET:
	case TraceDqr::INST_SRET:
	case TraceDqr::INST_URET:
		addr = -1;
		break;
	default:
		addr += inst_size / 8;
		break;
	}

	if (addr == (TraceDqr::ADDRESS)-1) {
		return TraceDqr::DQERR_ERR;
	}

	return TraceDqr::DQERR_OK;
}

// adjust pc, faddr, timestamp based on faddr, uaddr, timestamp, and message type.
// Do not adjust counts! They are handled elsewhere

TraceDqr::DQErr Trace::processTraceMessage(NexusMessage &nm,TraceDqr::ADDRESS &pc,TraceDqr::ADDRESS &faddr,TraceDqr::TIMESTAMP &ts)
{
	switch (nm.tcode) {
	case TraceDqr::TCODE_ERROR:
		if (nm.haveTimestamp) {
			ts = processTS(TraceDqr::TS_rel,ts,nm.timestamp);
		}

		// set addrs to 0 because we have dropped some messages and don't know what is going on

		faddr = 0;
		pc = 0;
		break;
	case TraceDqr::TCODE_OWNERSHIP_TRACE:
	case TraceDqr::TCODE_DIRECT_BRANCH:
	case TraceDqr::TCODE_DATA_ACQUISITION:
	case TraceDqr::TCODE_AUXACCESS_WRITE:
	case TraceDqr::TCODE_RESOURCEFULL:
	case TraceDqr::TCODE_CORRELATION:
		if (nm.haveTimestamp) {
			ts = processTS(TraceDqr::TS_rel,ts,nm.timestamp);
		}
		break;
	case TraceDqr::TCODE_INDIRECT_BRANCH:
		if (nm.haveTimestamp) {
			ts = processTS(TraceDqr::TS_rel,ts,nm.timestamp);
		}
		faddr = faddr ^ (nm.indirectBranch.u_addr << 1);
		pc = faddr;
		break;
	case TraceDqr::TCODE_SYNC:
		if (nm.haveTimestamp) {
			ts = processTS(TraceDqr::TS_full,ts,nm.timestamp);
		}
		faddr = nm.sync.f_addr << 1;
		pc = faddr;
		counts->resetStack(nm.coreId);
		counts->resetCounts(nm.coreId);
		break;
	case TraceDqr::TCODE_DIRECT_BRANCH_WS:
		if (nm.haveTimestamp) {
			ts = processTS(TraceDqr::TS_full,ts,nm.timestamp);
		}
		faddr = nm.directBranchWS.f_addr << 1;
		pc = faddr;
		counts->resetStack(nm.coreId);
		counts->resetCounts(nm.coreId);
		break;
	case TraceDqr::TCODE_INDIRECT_BRANCH_WS:
		if (nm.haveTimestamp) {
			ts = processTS(TraceDqr::TS_full,ts,nm.timestamp);
		}
		faddr = nm.indirectBranchWS.f_addr << 1;
		pc = faddr;
		counts->resetStack(nm.coreId);
		counts->resetCounts(nm.coreId);
		break;
	case TraceDqr::TCODE_INDIRECTBRANCHHISTORY:
		if (nm.haveTimestamp) {
			ts = processTS(TraceDqr::TS_rel,ts,nm.timestamp);
		}
		faddr = faddr ^ (nm.indirectHistory.u_addr << 1);
		pc = faddr;
		break;
	case TraceDqr::TCODE_INDIRECTBRANCHHISTORY_WS:
		if (nm.haveTimestamp) {
			ts = processTS(TraceDqr::TS_full,ts,nm.timestamp);
		}
		faddr = nm.indirectHistoryWS.f_addr << 1;
		pc = faddr;
		counts->resetStack(nm.coreId);
		counts->resetCounts(nm.coreId);
		break;
	case TraceDqr::TCODE_INCIRCUITTRACE:
		// for 8, 0; 14, 0 do not update pc, only faddr. 0, 0 has no address, so it never updates
		// this is because those message types all appear in instruction traces (non-event) and
		// do not want to update the current address because they have no icnt to say when to do it

		if (nm.haveTimestamp) {
			ts = processTS(TraceDqr::TS_rel,ts,nm.timestamp);
		}

		switch (nm.ict.cksrc) {
		case TraceDqr::ICT_EXT_TRIG:
			if (nm.ict.ckdf == 0) {
				faddr = faddr ^ (nm.ict.ckdata[0] << 1);
				// don't update pc

				if (eventConverter != nullptr) {
					eventConverter->emitExtTrigEvent(nm.coreId,ts,nm.ict.ckdf,faddr,0);
				}
			}
			else if (nm.ict.ckdf == 1) {
				faddr = faddr ^ (nm.ict.ckdata[0] << 1);
				pc = faddr;

				if (eventConverter != nullptr) {
					eventConverter->emitExtTrigEvent(nm.coreId,ts,nm.ict.ckdf,faddr,nm.ict.ckdata[1]);
				}
			}
			else {
				printf("Error: processTraceMessage(): Invalid ckdf field: %d\n",nm.ict.ckdf);
				return TraceDqr::DQERR_ERR;
			}
			break;
		case TraceDqr::ICT_WATCHPOINT:
			if (nm.ict.ckdf == 0) {
				faddr = faddr ^ (nm.ict.ckdata[0] << 1);
				// don't update pc

				if (eventConverter != nullptr) {
					eventConverter->emitWatchpoint(nm.coreId,ts,nm.ict.ckdf,faddr,0);
				}
			}
			else if (nm.ict.ckdf == 1) {
				faddr = faddr ^ (nm.ict.ckdata[0] << 1);
				pc = faddr;

				if (eventConverter != nullptr) {
					eventConverter->emitWatchpoint(nm.coreId,ts,nm.ict.ckdf,faddr,nm.ict.ckdata[1]);
				}
			}
			else {
				printf("Error: processTraceMessage(): Invalid ckdf field: %d\n",nm.ict.ckdf);
				return TraceDqr::DQERR_ERR;
			}
			break;
		case TraceDqr::ICT_INFERABLECALL:
			if (nm.ict.ckdf == 0) {
				pc = faddr ^ (nm.ict.ckdata[0] << 1);
				faddr = pc;

				TraceDqr::DQErr rc;
				TraceDqr::ADDRESS nextPC;
				int crFlags;

				rc = nextAddr(pc,nextPC,crFlags);
				if (rc != TraceDqr::DQERR_OK) {
					printf("Error: processTraceMessage(): Could not compute next address for CTF conversion\n");
					return TraceDqr::DQERR_ERR;
				}

				// we will store the target address back in ckdata[1] in case it is needed later

				nm.ict.ckdata[1] = nextPC;

				if (ctf != nullptr) {
					ctf->addCall(nm.coreId,pc,nextPC,ts);
				}

				if (eventConverter != nullptr) {
					eventConverter->emitCallRet(nm.coreId,ts,nm.ict.ckdf,pc,nm.ict.ckdata[1],TraceDqr::isCall);
				}
			}
			else if (nm.ict.ckdf == 1) {
				pc = faddr ^ (nm.ict.ckdata[0] << 1);
				faddr = pc ^ (nm.ict.ckdata[1] << 1);

				if ((ctf != nullptr) || (eventConverter != nullptr)) {
					TraceDqr::DQErr rc;
					TraceDqr::ADDRESS nextPC;
					int crFlags;

					rc = nextAddr(pc,nextPC,crFlags);
					if (rc != TraceDqr::DQERR_OK) {
						printf("Error: processTraceMessage(): Could not compute next address for CTF conversion\n");
						return TraceDqr::DQERR_ERR;
					}

					if (ctf != nullptr) {
						if (crFlags & TraceDqr::isCall) {
							ctf->addCall(nm.coreId,pc,faddr,ts);
						}
						else if ((crFlags & TraceDqr::isReturn) || (crFlags & TraceDqr::isExceptionReturn)) {
							ctf->addRet(nm.coreId,pc,faddr,ts);
						}
						else {
							printf("Error: processTraceMEssage(): Unsupported crFlags in CTF conversion\n");
							return TraceDqr::DQERR_ERR;
						}
					}

					if (eventConverter != nullptr) {
						eventConverter->emitCallRet(nm.coreId,ts,nm.ict.ckdf,pc,faddr,crFlags);
					}
				}
			}
			else {
				printf("Error: processTraceMessage(): Invalid ckdf field: %d\n",nm.ict.ckdf);
				return TraceDqr::DQERR_ERR;
			}
			break;
		case TraceDqr::ICT_EXCEPTION:
			if (nm.ict.ckdf == 1) {
				faddr = faddr ^ (nm.ict.ckdata[0] << 1);
				pc = faddr;
			}
			else {
				printf("Error: processTraceMessage(): Invalid ckdf field: %d\n",nm.ict.ckdf);
				return TraceDqr::DQERR_ERR;
			}

			if (eventConverter != nullptr) {
				eventConverter->emitException(nm.coreId,ts,nm.ict.ckdf,pc,nm.ict.ckdata[1]);
			}
			break;
		case TraceDqr::ICT_INTERRUPT:
			if (nm.ict.ckdf == 1) {
				faddr = faddr ^ (nm.ict.ckdata[0] << 1);
				pc = faddr;
			}
			else {
				printf("Error: processTraceMessage(): Invalid ckdf field: %d\n",nm.ict.ckdf);
				return TraceDqr::DQERR_ERR;
			}

			if (eventConverter != nullptr) {
				eventConverter->emitInterrupt(nm.coreId,ts,nm.ict.ckdf,pc,nm.ict.ckdata[1]);
			}
			break;
		case TraceDqr::ICT_CONTEXT:
			if (nm.ict.ckdf == 1) {
				faddr = faddr ^ (nm.ict.ckdata[0] << 1);
				pc = faddr;
			}
			else {
				printf("Error: processTraceMessage(): Invalid ckdf field: %d\n",nm.ict.ckdf);
				return TraceDqr::DQERR_ERR;
			}

			if (eventConverter != nullptr) {
				eventConverter->emitContext(nm.coreId,ts,nm.ict.ckdf,pc,nm.ict.ckdata[1]);
			}
			break;
		case TraceDqr::ICT_PC_SAMPLE:
			if (nm.ict.ckdf == 0) {
				faddr = faddr ^ (nm.ict.ckdata[0] << 1);
				pc = faddr;
			}
			else {
				printf("Error: processTraceMessage(): Invalid ckdf field: %d\n",nm.ict.ckdf);
				return TraceDqr::DQERR_ERR;
			}

			if (eventConverter != nullptr) {
				eventConverter->emitPeriodic(nm.coreId,ts,nm.ict.ckdf,pc);
			}
			break;
		case TraceDqr::ICT_CONTROL:
			if (nm.ict.ckdf == 0) {
				// nothing to do - no address
				// does not update faddr or pc!

				if (eventConverter != nullptr) {
					eventConverter->emitControl(nm.coreId,ts,nm.ict.ckdf,nm.ict.ckdata[0],0);
				}
			}
			else if (nm.ict.ckdf == 1) {
				faddr = faddr ^ (nm.ict.ckdata[0] << 1);
				pc = faddr;

				if (eventConverter != nullptr) {
					eventConverter->emitControl(nm.coreId,ts,nm.ict.ckdf,nm.ict.ckdata[1],pc);
				}
			}
			else {
				printf("Error: processTraceMessage(): Invalid ckdf field: %d\n",nm.ict.ckdf);
				return TraceDqr::DQERR_ERR;
			}
			break;
		default:
			printf("Error: processTraceMessage(): Invalid ICT Event: %d\n",nm.ict.cksrc);
			return TraceDqr::DQERR_ERR;
		}
		break;
	case TraceDqr::TCODE_INCIRCUITTRACE_WS:
		// for 8, 0; 14, 0 do not update pc, only faddr. 0, 0 has no address, so it never updates
		// this is because those message types all apprear in instruction traces (non-event) and
		// do not want to update the current address because they have no icnt to say when to do it

		if (nm.haveTimestamp) {
			ts = processTS(TraceDqr::TS_full,ts,nm.timestamp);
		}

		switch (nm.ictWS.cksrc) {
		case TraceDqr::ICT_EXT_TRIG:
			if (nm.ictWS.ckdf == 0) {
				faddr = nm.ictWS.ckdata[0] << 1;
				// don't update pc

				if (eventConverter != nullptr) {
					eventConverter->emitExtTrigEvent(nm.coreId,ts,nm.ictWS.ckdf,faddr,0);
				}
			}
			else if (nm.ictWS.ckdf == 1) {
				faddr = nm.ictWS.ckdata[0] << 1;
				pc = faddr;

				if (eventConverter != nullptr) {
					eventConverter->emitExtTrigEvent(nm.coreId,ts,nm.ictWS.ckdf,faddr,nm.ictWS.ckdata[1]);
				}
			}
			else {
				printf("Error: processTraceMessage(): Invalid ckdf field: %d\n",nm.ictWS.ckdf);
				return TraceDqr::DQERR_ERR;
			}
			break;
		case TraceDqr::ICT_WATCHPOINT:
			if (nm.ictWS.ckdf == 0) {
				faddr = nm.ictWS.ckdata[0] << 1;
				// don'tupdate pc

				if (eventConverter != nullptr) {
					eventConverter->emitWatchpoint(nm.coreId,ts,nm.ictWS.ckdf,faddr,0);
				}
			}
			else if (nm.ictWS.ckdf <= 1) {
				faddr = nm.ictWS.ckdata[0] << 1;
				pc = faddr;

				if (eventConverter != nullptr) {
					eventConverter->emitWatchpoint(nm.coreId,ts,nm.ictWS.ckdf,faddr,nm.ictWS.ckdata[1]);
				}
			}
			else {
				printf("Error: processTraceMessage(): Invalid ckdf field: %d\n",nm.ictWS.ckdf);
				return TraceDqr::DQERR_ERR;
			}
			break;
		case TraceDqr::ICT_INFERABLECALL:
			if (nm.ictWS.ckdf == 0) {
				pc = nm.ictWS.ckdata[0] << 1;
				faddr = pc;

				TraceDqr::DQErr rc;
				TraceDqr::ADDRESS nextPC;
				int crFlags;

				rc = nextAddr(pc,nextPC,crFlags);
				if (rc != TraceDqr::DQERR_OK) {
					printf("Error: processTraceMessage(): Could not compute next address for CTF conversion\n");
					return TraceDqr::DQERR_ERR;
				}

				// we will store the target address back in ckdata[1] in case it is needed later

				nm.ict.ckdata[1] = nextPC;

				if (ctf != nullptr) {
					ctf->addCall(nm.coreId,pc,nextPC,ts);
				}

				if (eventConverter != nullptr) {
					eventConverter->emitCallRet(nm.coreId,ts,nm.ictWS.ckdf,pc,faddr,TraceDqr::isCall);
				}
			}
			else if (nm.ictWS.ckdf == 1) {
				pc = nm.ictWS.ckdata[0] << 1;
				faddr = pc ^ (nm.ictWS.ckdata[1] << 1);

				if ((ctf != nullptr) || (eventConverter != nullptr)) {
					TraceDqr::DQErr rc;
					TraceDqr::ADDRESS nextPC;
					int crFlags;

					rc = nextAddr(pc,nextPC,crFlags);
					if (rc != TraceDqr::DQERR_OK) {
						printf("Error: processTraceMessage(): Could not compute next address for CTF conversion\n");
						return TraceDqr::DQERR_ERR;
					}

					if (ctf != nullptr) {
						if (crFlags & TraceDqr::isCall) {
							ctf->addCall(nm.coreId,pc,faddr,ts);
						}
						else if ((crFlags & TraceDqr::isReturn) || (crFlags & TraceDqr::isExceptionReturn)) {
							ctf->addRet(nm.coreId,pc,faddr,ts);
						}
						else {
							printf("Error: processTraceMEssage(): Unsupported crFlags in CTF conversion\n");
							return TraceDqr::DQERR_ERR;
						}
					}

					if (eventConverter != nullptr) {
						eventConverter->emitCallRet(nm.coreId,ts,nm.ictWS.ckdf,pc,faddr,crFlags);
					}
				}
			}
			else {
				printf("Error: processTraceMessage(): Invalid ckdf field: %d\n",nm.ictWS.ckdf);
				return TraceDqr::DQERR_ERR;
			}
			break;
		case TraceDqr::ICT_EXCEPTION:
			if (nm.ictWS.ckdf == 1) {
				faddr = nm.ictWS.ckdata[0] << 1;
				pc = faddr;
			}
			else {
				printf("Error: processTraceMessage(): Invalid ckdf field: %d\n",nm.ictWS.ckdf);
				return TraceDqr::DQERR_ERR;
			}

			if (eventConverter != nullptr) {
				eventConverter->emitException(nm.coreId,ts,nm.ictWS.ckdf,pc,nm.ictWS.ckdata[1]);
			}
			break;
		case TraceDqr::ICT_INTERRUPT:
			if (nm.ictWS.ckdf == 1) {
				faddr = nm.ictWS.ckdata[0] << 1;
				pc = faddr;
			}
			else {
				printf("Error: processTraceMessage(): Invalid ckdf field: %d\n",nm.ictWS.ckdf);
				return TraceDqr::DQERR_ERR;
			}

			if (eventConverter != nullptr) {
				eventConverter->emitInterrupt(nm.coreId,ts,nm.ictWS.ckdf,pc,nm.ictWS.ckdata[1]);
			}
			break;
		case TraceDqr::ICT_CONTEXT:
			if (nm.ictWS.ckdf == 1) {
				faddr = nm.ictWS.ckdata[0] << 1;
				pc = faddr;
			}
			else {
				printf("Error: processTraceMessage(): Invalid ckdf field: %d\n",nm.ictWS.ckdf);
				return TraceDqr::DQERR_ERR;
			}

			if (eventConverter != nullptr) {
				eventConverter->emitContext(nm.coreId,ts,nm.ictWS.ckdf,pc,nm.ictWS.ckdata[1]);
			}
			break;
		case TraceDqr::ICT_PC_SAMPLE:
			if (nm.ictWS.ckdf == 0) {
				faddr = nm.ictWS.ckdata[0] << 1;
				pc = faddr;
			}
			else {
				printf("Error: processTraceMessage(): Invalid ckdf field: %d\n",nm.ictWS.ckdf);
				return TraceDqr::DQERR_ERR;
			}

			if (eventConverter != nullptr) {
				eventConverter->emitPeriodic(nm.coreId,ts,nm.ictWS.ckdf,pc);
			}
			break;
		case TraceDqr::ICT_CONTROL:
			if (nm.ictWS.ckdf == 0) {
				// nothing to do
				// does not update faddr or pc!

				if (eventConverter != nullptr) {
					eventConverter->emitControl(nm.coreId,ts,nm.ictWS.ckdf,nm.ictWS.ckdata[0],0);
				}
			}
			else if (nm.ictWS.ckdf == 1) {
				faddr = nm.ictWS.ckdata[0] << 1;
				pc = faddr;

				if (eventConverter != nullptr) {
					eventConverter->emitControl(nm.coreId,ts,nm.ictWS.ckdf,nm.ictWS.ckdata[1],pc);
				}
			}
			else {
				printf("Error: processTraceMessage(): Invalid ckdf field: %d\n",nm.ictWS.ckdf);
				return TraceDqr::DQERR_ERR;
			}
			break;
		default:
			printf("Error: processTraceMessage(): Invalid ICT Event: %d\n",nm.ictWS.cksrc);
			return TraceDqr::DQERR_ERR;
		}
		break;
	case TraceDqr::TCODE_DEBUG_STATUS:
	case TraceDqr::TCODE_DEVICE_ID:
	case TraceDqr::TCODE_DATA_WRITE:
	case TraceDqr::TCODE_DATA_READ:
	case TraceDqr::TCODE_CORRECTION:
	case TraceDqr::TCODE_DATA_WRITE_WS:
	case TraceDqr::TCODE_DATA_READ_WS:
	case TraceDqr::TCODE_WATCHPOINT:
	case TraceDqr::TCODE_OUTPUT_PORTREPLACEMENT:
	case TraceDqr::TCODE_INPUT_PORTREPLACEMENT:
	case TraceDqr::TCODE_AUXACCESS_READ:
	case TraceDqr::TCODE_AUXACCESS_READNEXT:
	case TraceDqr::TCODE_AUXACCESS_WRITENEXT:
	case TraceDqr::TCODE_AUXACCESS_RESPONSE:
	case TraceDqr::TCODE_REPEATBRANCH:
	case TraceDqr::TCODE_REPEATINSTRUCTION:
	case TraceDqr::TCODE_REPEATINSTRUCTION_WS:
	case TraceDqr::TCODE_UNDEFINED:
	default:
		printf("Error: Trace::processTraceMessage(): Unsupported TCODE\n");

		return TraceDqr::DQERR_ERR;
	}

	return TraceDqr::DQERR_OK;
}

TraceDqr::DQErr Trace::getInstructionByAddress(TraceDqr::ADDRESS addr, Instruction *instInfo,Source *srcInfo,int *flags)
{
	Disassemble(addr); // should error check disassembl() call!

	*flags = 0;

	if (instInfo != nullptr) {
		instructionInfo.qDepth = 0;
		instructionInfo.arithInProcess = 0;
		instructionInfo.loadInProcess = 0;
		instructionInfo.storeInProcess = 0;

		instructionInfo.coreId = 0;
		*instInfo = instructionInfo;
		instInfo->CRFlag = TraceDqr::isNone;
		instInfo->brFlags = TraceDqr::BRFLAG_none;

		instInfo->timestamp = lastTime[currentCore];

		*flags |= TraceDqr::TRACE_HAVE_INSTINFO;
	}

	if (srcInfo != nullptr) {
		sourceInfo.coreId = 0;
		*srcInfo = sourceInfo;
		*flags |= TraceDqr::TRACE_HAVE_SRCINFO;
	}

	return TraceDqr::DQERR_OK;
}

TraceDqr::DQErr Trace::NextInstruction(Instruction *instInfo,NexusMessage *msgInfo,Source *srcInfo,int *flags)
{
	TraceDqr::DQErr ec;

	Instruction  *instInfop = nullptr;
	NexusMessage *msgInfop  = nullptr;
	Source       *srcInfop  = nullptr;

	Instruction  **instInfopp = nullptr;
	NexusMessage **msgInfopp  = nullptr;
	Source       **srcInfopp  = nullptr;

	if (instInfo != nullptr) {
		instInfopp = &instInfop;
	}

	if (msgInfo != nullptr) {
		msgInfopp = &msgInfop;
	}

	if (srcInfo != nullptr) {
		srcInfopp = &srcInfop;
	}

	ec = NextInstruction(instInfopp, msgInfopp, srcInfopp);

	*flags = 0;

	if (ec == TraceDqr::DQERR_OK) {
		if (instInfo != nullptr) {
			if (instInfop != nullptr) {
				*instInfo = *instInfop;
				*flags |= TraceDqr::TRACE_HAVE_INSTINFO;
			}
		}

		if (msgInfo != nullptr) {
			if (msgInfop != nullptr) {
				*msgInfo = *msgInfop;
				*flags |= TraceDqr::TRACE_HAVE_MSGINFO;
			}
		}

		if (srcInfo != nullptr) {
			if (srcInfop != nullptr) {
				*srcInfo = *srcInfop;
				*flags |= TraceDqr::TRACE_HAVE_SRCINFO;
			}
		}

		if (itcPrint != nullptr) {
			if (itcPrint->haveITCPrintMsgs() != false) {
				*flags |= TraceDqr::TRACE_HAVE_ITCPRINTINFO;
			}
		}
	}

	return ec;
}

//NextInstruction() want to return address, instruction, trace message if any, label+offset for instruction, target of instruciton
//		source code for instruction (file, function, line)
//
//		return instruction object (include label informatioon)
//		return message object
//		return source code object//
//
//				if instruction object ptr is null, don't return any instruction info
//				if message object ptr is null, don't return any message info
//				if source code object is null, don't return source code info

TraceDqr::DQErr Trace::NextInstruction(Instruction **instInfo, NexusMessage **msgInfo, Source **srcInfo)
{
	if (sfp == nullptr) {
		printf("Error: Trace::NextInstructin(): Null sfp object\n");

		status = TraceDqr::DQERR_ERR;
		return status;
	}

	if (status != TraceDqr::DQERR_OK) {
		return status;
	}

	TraceDqr::DQErr rc;
	TraceDqr::ADDRESS addr;
	int crFlag;
	TraceDqr::BranchFlags brFlags;
	uint32_t caFlags;
	uint32_t pipeCycles;
	uint32_t viStartCycles;
	uint32_t viFinishCycles;

	uint8_t qDepth;
	uint8_t arithInProcess;
	uint8_t loadInProcess;
	uint8_t storeInProcess;

	Instruction  **savedInstPtr = nullptr;
	NexusMessage **savedMsgPtr = nullptr;
	Source       **savedSrcPtr = nullptr;

	if (instInfo != nullptr) {
		*instInfo = nullptr;
	}

	if (msgInfo != nullptr) {
		*msgInfo = nullptr;
	}

	if (srcInfo != nullptr) {
		*srcInfo = nullptr;
	}

	for (;;) {
//		need to set readNewTraceMessage where it is needed! That includes
//		staying in the same state that expects to get another message!!

		bool haveMsg;

		if (savedInstPtr != nullptr) {
			instInfo = savedInstPtr;
			savedInstPtr = nullptr;
		}

		if (savedMsgPtr != nullptr) {
			msgInfo = savedMsgPtr;
			savedMsgPtr = nullptr;
		}

		if (savedSrcPtr != nullptr) {
			srcInfo = savedSrcPtr;
			savedSrcPtr = nullptr;
		}

		if (readNewTraceMessage != false) {
			rc = sfp->readNextTraceMsg(nm,analytics,haveMsg);

			if (rc != TraceDqr::DQERR_OK) {
				// have an error. either EOF, or error

				status = rc;

				if (status == TraceDqr::DQERR_EOF) {
					state[currentCore] = TRACE_STATE_DONE;
				}
				else {
					printf("Error: Trace file does not contain any trace messages, or is unreadable\n");

					state[currentCore] = TRACE_STATE_ERROR;
				}

				return status;
			}

			if (haveMsg == false) {
				return status;
			}

			readNewTraceMessage = false;
			currentCore = nm.coreId;

			// if set see if HTM trace message, switch to HTM mode

			if (traceType != TraceDqr::TRACETYPE_HTM) {
				switch (nm.tcode) {
				case TraceDqr::TCODE_OWNERSHIP_TRACE:
				case TraceDqr::TCODE_DIRECT_BRANCH:
				case TraceDqr::TCODE_INDIRECT_BRANCH:
				case TraceDqr::TCODE_DATA_ACQUISITION:
				case TraceDqr::TCODE_ERROR:
				case TraceDqr::TCODE_SYNC:
				case TraceDqr::TCODE_DIRECT_BRANCH_WS:
				case TraceDqr::TCODE_INDIRECT_BRANCH_WS:
				case TraceDqr::TCODE_AUXACCESS_WRITE:
				case TraceDqr::TCODE_INCIRCUITTRACE:
				case TraceDqr::TCODE_INCIRCUITTRACE_WS:
					break;
				case TraceDqr::TCODE_CORRELATION:
					if (nm.correlation.cdf == 1) {
						traceType = TraceDqr::TRACETYPE_HTM;
						if (globalDebugFlag) printf("TCODE_CORRELATION, cdf == 1: switching to HTM mode\n");
					}
					break;
				case TraceDqr::TCODE_RESOURCEFULL:
				case TraceDqr::TCODE_INDIRECTBRANCHHISTORY:
				case TraceDqr::TCODE_INDIRECTBRANCHHISTORY_WS:
					traceType = TraceDqr::TRACETYPE_HTM;
					if (globalDebugFlag) printf("History/taken/not taken count TCODE: switching to HTM mode\n");
					break;
				case TraceDqr::TCODE_REPEATBRANCH:
				case TraceDqr::TCODE_REPEATINSTRUCTION:
				case TraceDqr::TCODE_REPEATINSTRUCTION_WS:
				case TraceDqr::TCODE_AUXACCESS_READNEXT:
				case TraceDqr::TCODE_AUXACCESS_WRITENEXT:
				case TraceDqr::TCODE_AUXACCESS_RESPONSE:
				case TraceDqr::TCODE_OUTPUT_PORTREPLACEMENT:
				case TraceDqr::TCODE_INPUT_PORTREPLACEMENT:
				case TraceDqr::TCODE_AUXACCESS_READ:
				case TraceDqr::TCODE_DATA_WRITE_WS:
				case TraceDqr::TCODE_DATA_READ_WS:
				case TraceDqr::TCODE_WATCHPOINT:
				case TraceDqr::TCODE_CORRECTION:
				case TraceDqr::TCODE_DATA_WRITE:
				case TraceDqr::TCODE_DATA_READ:
				case TraceDqr::TCODE_DEBUG_STATUS:
				case TraceDqr::TCODE_DEVICE_ID:
					printf("Error: NextInstruction(): Unsupported tcode type (%d)\n",nm.tcode);
					status = TraceDqr::DQERR_ERR;
					state[currentCore] = TRACE_STATE_ERROR;

					return status;
				case TraceDqr::TCODE_UNDEFINED:
					printf("Error: NextInstruction(): Undefined tcode type (%d)\n",nm.tcode);
					status = TraceDqr::DQERR_ERR;
					state[currentCore] = TRACE_STATE_ERROR;

					return status;
				}
			}

			// Check if this is a ICT Control message and if we are filtering them out

			switch (nm.tcode) {
			case TraceDqr::TCODE_INCIRCUITTRACE:
			case TraceDqr::TCODE_INCIRCUITTRACE_WS:
				if ((nm.getCKSRC() == TraceDqr::ICT_CONTROL) && (eventFilterMask & (1 << CTF::et_controlIndex))) {
					savedInstPtr = instInfo;
					instInfo = nullptr;
					savedMsgPtr = msgInfo;
					msgInfo = nullptr;
					savedSrcPtr = srcInfo;
					srcInfo = nullptr;
				}
				break;
			default:
				break;
			}
		}

		switch (state[currentCore]) {
		case TRACE_STATE_SYNCCATE:	// Looking for a CA trace sync
//			printf("TRACE_STATE_SYNCCATE\n");

			if (caTrace == nullptr) {
				// have an error! Should never have TRACE_STATE_SYNC without a caTrace ptr
				printf("Error: caTrace is null\n");
				status = TraceDqr::DQERR_ERR;
				state[currentCore] = TRACE_STATE_ERROR;
				return status;
			}

			// loop through trace messages until we find a sync of some kind. First sync should do it
			// sync reason must be correct (exit debug or start tracing) or we stay in this state

			TraceDqr::ADDRESS teAddr;

			switch (nm.tcode) {
			case TraceDqr::TCODE_ERROR:
				// reset time. Messages have been missed. Address may not still be 0 if we have seen a sync
                                // message without an exit debug or start trace sync reason, so reset address

				lastTime[currentCore] = 0;
				currentAddress[currentCore] = 0;
                lastFaddr[currentCore] = 0;

				if (msgInfo != nullptr) {
					messageInfo = nm;

					// currentAddresss should be 0 until we get a sync message. TS has been set to 0

					messageInfo.currentAddress = currentAddress[currentCore];
					messageInfo.time = lastTime[currentCore];

					if (messageInfo.processITCPrintData(itcPrint) == false) {
						*msgInfo = &messageInfo;
					}
				}

				readNewTraceMessage = true;

				status = TraceDqr::DQERR_OK;

				return status;
			case TraceDqr::TCODE_OWNERSHIP_TRACE:
			case TraceDqr::TCODE_DIRECT_BRANCH:
			case TraceDqr::TCODE_INDIRECT_BRANCH:
			case TraceDqr::TCODE_DATA_ACQUISITION:
			case TraceDqr::TCODE_AUXACCESS_WRITE:
			case TraceDqr::TCODE_INCIRCUITTRACE:
			case TraceDqr::TCODE_CORRELATION:
			case TraceDqr::TCODE_RESOURCEFULL:
			case TraceDqr::TCODE_INDIRECTBRANCHHISTORY:
			case TraceDqr::TCODE_REPEATBRANCH:
			case TraceDqr::TCODE_REPEATINSTRUCTION:
			case TraceDqr::TCODE_REPEATINSTRUCTION_WS:
			case TraceDqr::TCODE_AUXACCESS_READNEXT:
			case TraceDqr::TCODE_AUXACCESS_WRITENEXT:
			case TraceDqr::TCODE_AUXACCESS_RESPONSE:
			case TraceDqr::TCODE_OUTPUT_PORTREPLACEMENT:
			case TraceDqr::TCODE_INPUT_PORTREPLACEMENT:
			case TraceDqr::TCODE_AUXACCESS_READ:
				// here we return the trace messages before we have actually started tracing
				// this could be at the start of a trace, or after leaving a trace because of
				// a correlation message

		                // we may have a valid address and time already if we saw a sync without an exit debug				        // or start trace sync reason. So call processTraceMessage()

				if (lastFaddr[currentCore] != 0) {
					rc = processTraceMessage(nm,currentAddress[currentCore],lastFaddr[currentCore],lastTime[currentCore]);
					if (rc != TraceDqr::DQERR_OK) {
						printf("Error: NextInstruction(): state TRACE_STATE_SYNCCATE: processTraceMessage()\n");

						status = TraceDqr::DQERR_ERR;
						state[currentCore] = TRACE_STATE_ERROR;

						return status;
					}
                }

				if (msgInfo != nullptr) {
					messageInfo = nm;

					// currentAddresss should be 0 until we get a sync message. TS may
                    // have been set by a ICT control WS message

					messageInfo.currentAddress = currentAddress[currentCore];
					messageInfo.time = lastTime[currentCore];

					if (messageInfo.processITCPrintData(itcPrint) == false) {
						*msgInfo = &messageInfo;
					}
				}

				readNewTraceMessage = true;

				status = TraceDqr::DQERR_OK;

				return status;
			case TraceDqr::TCODE_SYNC:
			case TraceDqr::TCODE_DIRECT_BRANCH_WS:
			case TraceDqr::TCODE_INDIRECT_BRANCH_WS:
			case TraceDqr::TCODE_INDIRECTBRANCHHISTORY_WS:
				// sync reason should be either EXIT_DEBUG or TRACE_ENABLE. Otherwise, keep looking

				TraceDqr::SyncReason sr;

				sr = nm.getSyncReason();
				switch (sr) {
				case TraceDqr::SYNC_EXIT_DEBUG:
				case TraceDqr::SYNC_TRACE_ENABLE:
					// only exit debug or trace enable allow proceeding. All others stay in this state and return

					teAddr = nm.getF_Addr() << 1;
					break;
				case TraceDqr::SYNC_EVTI:
				case TraceDqr::SYNC_EXIT_RESET:
				case TraceDqr::SYNC_T_CNT:
				case TraceDqr::SYNC_I_CNT_OVERFLOW:
				case TraceDqr::SYNC_WATCHPINT:
				case TraceDqr::SYNC_FIFO_OVERRUN:
				case TraceDqr::SYNC_EXIT_POWERDOWN:
				case TraceDqr::SYNC_MESSAGE_CONTENTION:
				case TraceDqr::SYNC_PC_SAMPLE:
					// here we return the trace messages before we have actually started tracing
					// this could be at the start of a trace, or after leaving a trace because of
					// a correlation message
					// probably should never get here when doing a CA trace.

					rc = processTraceMessage(nm,currentAddress[currentCore],lastFaddr[currentCore],lastTime[currentCore]);
					if (rc != TraceDqr::DQERR_OK) {
						printf("Error: NextInstruction(): state TRACE_STATE_SYNCCATE: processTraceMessage()\n");

						status = TraceDqr::DQERR_ERR;
						state[currentCore] = TRACE_STATE_ERROR;

						return status;
					}

					if (msgInfo != nullptr) {
						messageInfo = nm;

						// if doing pc-sampling and msg type is INCIRCUITTRACE_WS, we want to use faddr
						// and not currentAddress

						messageInfo.currentAddress = nm.getF_Addr() << 1;

						if (messageInfo.processITCPrintData(itcPrint) == false) {
							*msgInfo = &messageInfo;
						}
					}

					readNewTraceMessage = true;

					status = TraceDqr::DQERR_OK;

					return status;
				case TraceDqr::SYNC_NONE:
				default:
					printf("Error: invalid sync reason\n");
					status = TraceDqr::DQERR_ERR;
					state[currentCore] = TRACE_STATE_ERROR;

					return status;
				}
				break;
			case TraceDqr::TCODE_INCIRCUITTRACE_WS:
				// INCIRCUTTRACE_WS messages do not have a sync reason, but control(0,1) has
				// the same info!

				TraceDqr::ICTReason itcr;

				itcr = nm.getCKSRC();

				switch (itcr) {
				case TraceDqr::ICT_INFERABLECALL:
				case TraceDqr::ICT_EXT_TRIG:
				case TraceDqr::ICT_EXCEPTION:
				case TraceDqr::ICT_INTERRUPT:
				case TraceDqr::ICT_CONTEXT:
				case TraceDqr::ICT_WATCHPOINT:
				case TraceDqr::ICT_PC_SAMPLE:
					rc = processTraceMessage(nm,currentAddress[currentCore],lastFaddr[currentCore],lastTime[currentCore]);
					if (rc != TraceDqr::DQERR_OK) {
						printf("Error: NextInstruction(): state TRACE_STATE_SYNCCATE: processTraceMessage()\n");

						status = TraceDqr::DQERR_ERR;
						state[currentCore] = TRACE_STATE_ERROR;

						return status;
					}

					if (msgInfo != nullptr) {
						messageInfo = nm;

						// if doing pc-sampling and msg type is INCIRCUITTRACE_WS, we want to use faddr
						// and not currentAddress

						messageInfo.currentAddress = nm.getF_Addr() << 1;

						if (messageInfo.processITCPrintData(itcPrint) == false) {
							*msgInfo = &messageInfo;
						}
					}

					readNewTraceMessage = true;

					status = TraceDqr::DQERR_OK;

					return status;
                case TraceDqr::ICT_CONTROL:
                	bool returnFlag;
                	returnFlag = true;

                	if (nm.ictWS.ckdf == 1) {
                		switch (nm.ictWS.ckdata[1]) {
                		case TraceDqr::ICT_CONTROL_TRACE_ON:
                		case TraceDqr::ICT_CONTROL_EXIT_DEBUG:
                			// only exit debug or trace enable allow proceeding. All others stay in this state and return

                			teAddr = nm.getF_Addr() << 1;
                			returnFlag = false;
                			break;
                		default:
                			break;
                		}
                	}

                	if (returnFlag) {
                		rc = processTraceMessage(nm,currentAddress[currentCore],lastFaddr[currentCore],lastTime[currentCore]);
						if (rc != TraceDqr::DQERR_OK) {
							printf("Error: NextInstruction(): state TRACE_STATE_SYNCCATE: processTraceMessage()\n");

							status = TraceDqr::DQERR_ERR;
							state[currentCore] = TRACE_STATE_ERROR;

							return status;
						}

						if (msgInfo != nullptr) {
							messageInfo = nm;

							// if doing pc-sampling and msg type is INCIRCUITTRACE_WS, we want to use faddr
							// and not currentAddress

							messageInfo.currentAddress = nm.getF_Addr() << 1;

							if (messageInfo.processITCPrintData(itcPrint) == false) {
								*msgInfo = &messageInfo;
							}
						}

						readNewTraceMessage = true;

						status = TraceDqr::DQERR_OK;

						return status;
                	}
                	break;
				case TraceDqr::ICT_NONE:
				default:
					printf("Error: invalid ICT reason\n");
					status = TraceDqr::DQERR_ERR;
					state[currentCore] = TRACE_STATE_ERROR;

					return status;
				}
				break;
			case TraceDqr::TCODE_DEBUG_STATUS:
			case TraceDqr::TCODE_DEVICE_ID:
			case TraceDqr::TCODE_DATA_WRITE:
			case TraceDqr::TCODE_DATA_READ:
			case TraceDqr::TCODE_CORRECTION:
			case TraceDqr::TCODE_DATA_WRITE_WS:
			case TraceDqr::TCODE_DATA_READ_WS:
			case TraceDqr::TCODE_WATCHPOINT:
			case TraceDqr::TCODE_UNDEFINED:
			default:
				printf("Error: nextInstruction(): state TRACE_STATE_SYNCCATE: unsupported or invalid TCODE\n");

				state[currentCore] = TRACE_STATE_ERROR;
				status = TraceDqr::DQERR_ERR;

				return status;
			}

			// run ca code until we get to the te trace address. only do 6 instructions a the most

			caSyncAddr = caTrace->getCATraceStartAddr();

//			printf("caSyncAddr: %08x, teAddr: %08x\n",caSyncAddr,teAddr);

//			caTrace->dumpCurrentCARecord(1);

			TraceDqr::ADDRESS savedAddr;
			savedAddr = -1;

			bool fail;
			fail = false;

			for (int i = 0; (fail == false) && (teAddr != caSyncAddr) && (i < 30); i++) {
				rc = nextCAAddr(caSyncAddr,savedAddr);
				if (rc != TraceDqr::DQERR_OK) {
					fail = true;
				}
				else {
//					printf("caSyncAddr: %08x, teAddr: %08x\n",caSyncAddr,teAddr);

                    rc = caTrace->consume(caFlags,TraceDqr::INST_SCALER,pipeCycles,viStartCycles,viFinishCycles,qDepth,arithInProcess,loadInProcess,storeInProcess);
					if (rc == TraceDqr::DQERR_EOF) {
						state[currentCore] = TRACE_STATE_DONE;

						status = rc;
						return rc;
					}

					if (rc != TraceDqr::DQERR_OK) {
						state[currentCore] = TRACE_STATE_ERROR;

						status = rc;
						return status;
					}
				}
			}

//			if (teAddr == caSyncAddr) {
//				printf("ca sync found at address %08x, cycles: %d\n",caSyncAddr,cycles);
//			}

			if (teAddr != caSyncAddr) {
				// unable to sync by fast-forwarding the CA trace to match the instruction trace
				// so we will try to run the normal trace for a few instructions with the hope it
				// will sync up with the ca trace! We set the max number of instructions to run
				// the normal trace below, and turn tracing loose!

				syncCount = 16;
				caTrace->rewind();
				caSyncAddr = caTrace->getCATraceStartAddr();

//				printf("starting normal trace to sync up; caSyncAddr: %08x\n",caSyncAddr);
			}

			// readnextmessage should be false. So, we want to process the message like a normal message here
			// if the addresses of the trace and the start of the ca trace sync later, it is handled in
			// the other states

			state[currentCore] = TRACE_STATE_GETFIRSTSYNCMSG;
			break;
		case TRACE_STATE_GETFIRSTSYNCMSG:
			// start here for normal traces

//			printf("TRACE_STATE_GETFIRSTSYNCMSG\n");

			// read trace messages until a sync is found. Should be the first message normally
			// unless the wrapped buffer

			// only exit this state when sync type message is found or EOF or error
			// Event messages will cause state to change to TRACE_STATE_EVENT

			switch (nm.tcode) {
			case TraceDqr::TCODE_SYNC:
			case TraceDqr::TCODE_DIRECT_BRANCH_WS:
			case TraceDqr::TCODE_INDIRECT_BRANCH_WS:
			case TraceDqr::TCODE_INDIRECTBRANCHHISTORY_WS:
				rc = processTraceMessage(nm,currentAddress[currentCore],lastFaddr[currentCore],lastTime[currentCore]);
				if (rc != TraceDqr::DQERR_OK) {
					printf("Error: NextInstruction(): state TRACE_STATE_GETFIRSTSYNCMSG: processTraceMessage()\n");

					status = TraceDqr::DQERR_ERR;
					state[currentCore] = TRACE_STATE_ERROR;

					return status;
				}

				if (srcInfo != nullptr) {
					Disassemble(currentAddress[currentCore]);

					sourceInfo.coreId = currentCore;
					*srcInfo = &sourceInfo;
				}

				state[currentCore] = TRACE_STATE_GETMSGWITHCOUNT;
				break;
			case TraceDqr::TCODE_INCIRCUITTRACE_WS:
				// this may set the timestamp, and and may set the address
				// all set the address except control(0,0) which is used just to set the timestamp at most

				rc = processTraceMessage(nm,currentAddress[currentCore],lastFaddr[currentCore],lastTime[currentCore]);
				if (rc != TraceDqr::DQERR_OK) {
					printf("Error: NextInstruction(): state TRACE_STATE_GETFIRSTSYNCMSG: processTraceMessage()\n");

					status = TraceDqr::DQERR_ERR;
					state[currentCore] = TRACE_STATE_ERROR;

					return status;
				}

				if (currentAddress[currentCore] == 0) {
					// for the get first sync state, we want currentAddress to be set
					// most incircuttrace_ws types will set it, but not 8,0; 14,0; 0,0

					currentAddress[currentCore] = lastFaddr[currentCore];
				}

				if ((nm.ictWS.cksrc == TraceDqr::ICT_CONTROL) && (nm.ictWS.ckdf == 0)) {
					// ICT_WS Control(0,0) only updates TS (if present). Does not change state or anything else
					// because it is the only incircuittrace message type with no address
				}
				else {
					if ((nm.getCKSRC() == TraceDqr::ICT_EXT_TRIG) && (nm.getCKDF() == 0)) {
						// no dasm or src for ext trigger in HTM instruction traces
					}
					else if ((nm.getCKSRC() == TraceDqr::ICT_WATCHPOINT) && (nm.getCKDF() == 0)) {
						// no dasm or src for ext trigger in HTM instruction traces
					}
					else if ((instInfo != nullptr) || (srcInfo != nullptr)) {
						Disassemble(currentAddress[currentCore]);

						if (instInfo != nullptr) {
							instructionInfo.qDepth = 0;
							instructionInfo.arithInProcess = 0;
							instructionInfo.loadInProcess = 0;
							instructionInfo.storeInProcess = 0;

							instructionInfo.coreId = currentCore;
							*instInfo = &instructionInfo;
//							(*instInfo)->CRFlag = TraceDqr::isNone;
//							(*instInfo)->brFlags = TraceDqr::BRFLAG_none;
							getCRBRFlags(nm.getCKSRC(),currentAddress[currentCore],(*instInfo)->CRFlag,(*instInfo)->brFlags);

							(*instInfo)->timestamp = lastTime[currentCore];
						}

						if (srcInfo != nullptr) {
							sourceInfo.coreId = currentCore;
							*srcInfo = &sourceInfo;
						}
					}
					state[currentCore] = TRACE_STATE_GETMSGWITHCOUNT;
				}
				break;
			case TraceDqr::TCODE_INCIRCUITTRACE:
			case TraceDqr::TCODE_OWNERSHIP_TRACE:
			case TraceDqr::TCODE_DIRECT_BRANCH:
			case TraceDqr::TCODE_INDIRECT_BRANCH:
			case TraceDqr::TCODE_DATA_ACQUISITION:
			case TraceDqr::TCODE_INDIRECTBRANCHHISTORY:
			case TraceDqr::TCODE_RESOURCEFULL:
			case TraceDqr::TCODE_CORRELATION:
				if (nm.timestamp) {
					lastTime[currentCore] = processTS(TraceDqr::TS_rel,lastTime[currentCore],nm.timestamp);
				}
				break;
			case TraceDqr::TCODE_ERROR:
				// reset time. Messages have been missed.
				lastTime[currentCore] = 0;
				break;
			case TraceDqr::TCODE_DEBUG_STATUS:
			case TraceDqr::TCODE_DEVICE_ID:
			case TraceDqr::TCODE_DATA_WRITE:
			case TraceDqr::TCODE_DATA_READ:
			case TraceDqr::TCODE_CORRECTION:
			case TraceDqr::TCODE_DATA_WRITE_WS:
			case TraceDqr::TCODE_DATA_READ_WS:
			case TraceDqr::TCODE_WATCHPOINT:
			default:
				printf("Error: nextInstructin(): state TRACE_STATE_GETFIRSTSYNCMSG: unsupported or invalid TCODE\n");

				state[currentCore] = TRACE_STATE_ERROR;
				status = TraceDqr::DQERR_ERR;

				return status;
			}

			// INCIRCUITTRACE or INCIRCUITTRACE_WS will have set state to TRACE_STATE_EVENT

			readNewTraceMessage = true;

			// here we return the trace messages before we have actually started tracing
			// this could be at the start of a trace, or after leaving a trace because of
			// a correlation message

			if (msgInfo != nullptr) {
				messageInfo = nm;

				messageInfo.currentAddress = currentAddress[currentCore];

				messageInfo.time = lastTime[currentCore];

				if (messageInfo.processITCPrintData(itcPrint) == false) {
					*msgInfo = &messageInfo;
				}
			}

			status = TraceDqr::DQERR_OK;
			return status;
		case TRACE_STATE_GETMSGWITHCOUNT:

			// think GETMSGWITHCOUNT and GETNEXTMSG state are the same!! If so, combine them!

//			printf("TRACE_STATE_GETMSGWITHCOUNT %08x\n",lastFaddr[currentCore]);
			// only message with i-cnt/hist/taken/notTaken will release from this state

			// return any message without a count (i-cnt or hist, taken/not taken)

			// do not return message with i-cnt/hist/taken/not taken; process them when counts expires

			switch (nm.tcode) {
			case TraceDqr::TCODE_SYNC:
			case TraceDqr::TCODE_DIRECT_BRANCH_WS:
			case TraceDqr::TCODE_INDIRECT_BRANCH_WS:
			case TraceDqr::TCODE_DIRECT_BRANCH:
			case TraceDqr::TCODE_INDIRECT_BRANCH:
			case TraceDqr::TCODE_CORRELATION:
			case TraceDqr::TCODE_INDIRECTBRANCHHISTORY:
			case TraceDqr::TCODE_INDIRECTBRANCHHISTORY_WS:
			case TraceDqr::TCODE_RESOURCEFULL:
				// don't update timestamp until messages are retired!

				// reset all counts before setting them. We have no valid counts before the second message.
				// first message is a sync-type message. Counts are for up to that message, nothing after.

				counts->resetCounts(currentCore);

				rc = counts->setCounts(&nm);
				if (rc != TraceDqr::DQERR_OK) {
					state[currentCore] = TRACE_STATE_ERROR;
					status = rc;

					return status;
				}

				// only these TCODEs have counts and release from this state

				state[currentCore] = TRACE_STATE_GETNEXTINSTRUCTION;
				break;
			case TraceDqr::TCODE_INCIRCUITTRACE:
			case TraceDqr::TCODE_INCIRCUITTRACE_WS:
				// these message have no counts so they will be retired immediately

				rc = processTraceMessage(nm,currentAddress[currentCore],lastFaddr[currentCore],lastTime[currentCore]);
				if (rc != TraceDqr::DQERR_OK) {
					printf("Error: NextInstruction(): state TRACE_STATE_GETMSGWITHCOUNT: processTraceMessage()\n");

					status = TraceDqr::DQERR_ERR;
					state[currentCore] = TRACE_STATE_ERROR;

					return status;
				}

				if ((nm.getCKSRC() == TraceDqr::ICT_CONTROL) && (nm.getCKDF() == 0)) {
					// ICT_WS Control(0,0) only updates TS (if present). Does not change state or anything else
					addr = currentAddress[currentCore];
				}
				else {
					if ((nm.getCKSRC() == TraceDqr::ICT_EXT_TRIG) && (nm.getCKDF() == 0)) {
						// no dasm or src for ext trigger in HTM instruction traces
						addr = lastFaddr[currentCore];
					}
					else if ((nm.getCKSRC() == TraceDqr::ICT_WATCHPOINT) && (nm.getCKDF() == 0)) {
						// no dasm or src for ext trigger in HTM instruction tracaes
						addr = lastFaddr[currentCore];
					}
					else if ((instInfo != nullptr) || (srcInfo != nullptr)) {
						addr = currentAddress[currentCore];

						Disassemble(addr);

						if (instInfo != nullptr) {
							instructionInfo.qDepth = 0;
							instructionInfo.arithInProcess = 0;
							instructionInfo.loadInProcess = 0;
							instructionInfo.storeInProcess = 0;

							instructionInfo.coreId = currentCore;
							*instInfo = &instructionInfo;
//							(*instInfo)->CRFlag = TraceDqr::isNone;
//							(*instInfo)->brFlags = TraceDqr::BRFLAG_none;
							getCRBRFlags(nm.getCKSRC(),currentAddress[currentCore],(*instInfo)->CRFlag,(*instInfo)->brFlags);
							(*instInfo)->timestamp = lastTime[currentCore];
						}

						if (srcInfo != nullptr) {
							sourceInfo.coreId = currentCore;
							*srcInfo = &sourceInfo;
						}
					}
					state[currentCore] = TRACE_STATE_GETMSGWITHCOUNT;
				}

				if (msgInfo != nullptr) {
					messageInfo = nm;
					messageInfo.time = lastTime[currentCore];
					messageInfo.currentAddress = addr;

					if (messageInfo.processITCPrintData(itcPrint) == false) {
						*msgInfo = &messageInfo;
					}
				}

				readNewTraceMessage = true;

				return status;
			case TraceDqr::TCODE_ERROR:
				state[currentCore] = TRACE_STATE_GETFIRSTSYNCMSG;

				// don't update timestamp because we have missed some
				//
				// if (nm.haveTimestamp) {
				//	lastTime[currentCore] = processTS(TraceDqr::TS_rel,lastTime[currentCore],nm.timestamp);
				// }

				nm.timestamp = 0;	// clear time because we have lost time
				lastTime[currentCore] = 0;
				currentAddress[currentCore] = 0;
				lastFaddr[currentCore] = 0;

				if (msgInfo != nullptr) {
					messageInfo = nm;
					messageInfo.time = lastTime[currentCore];
					messageInfo.currentAddress = currentAddress[currentCore];

					if (messageInfo.processITCPrintData(itcPrint) == false) {
						*msgInfo = &messageInfo;
					}
				}

				readNewTraceMessage = true;

				return status;
			case TraceDqr::TCODE_AUXACCESS_WRITE:
			case TraceDqr::TCODE_OWNERSHIP_TRACE:
			case TraceDqr::TCODE_DATA_ACQUISITION:
				// these message have no address or count info, so we still need to get
				// another message.

				// might want to keep track of process, but will add that later

				// for now, return message;

				if (nm.haveTimestamp) {
					lastTime[currentCore] = processTS(TraceDqr::TS_rel,lastTime[currentCore],nm.timestamp);
				}

				if (msgInfo != nullptr) {
					messageInfo = nm;
					messageInfo.time = lastTime[currentCore];
					messageInfo.currentAddress = currentAddress[currentCore];

					if (messageInfo.processITCPrintData(itcPrint) == false) {
						*msgInfo = &messageInfo;
					}
				}

				readNewTraceMessage = true;

				return status;
			default:
				printf("Error: bad tcode type in state TRACE_STATE_GETMSGWITHCOUNT. TCODE (%d)\n",nm.tcode);

				state[currentCore] = TRACE_STATE_ERROR;
				status = TraceDqr::DQERR_ERR;

				return status;
			}
			break;
		case TRACE_STATE_RETIREMESSAGE:
//			printf("TRACE_STATE_RETIREMESSAGE\n");

			// Process message being retired (currently in nm) i_cnt/taken/not taken/history has gone to 0
			// compute next address

//			set lastFaddr,currentAddress,lastTime.
//			readNewTraceMessage = true;
//			state = Trace_State_GetNextMsg;
//			return messageInfo.

			// retire message should be run anytime any count expires - i-cnt, history, taken, not taken

			switch (nm.tcode) {
			// sync type messages say where to set pc to
			case TraceDqr::TCODE_SYNC:
			case TraceDqr::TCODE_DIRECT_BRANCH_WS:
			case TraceDqr::TCODE_INDIRECT_BRANCH_WS:
			case TraceDqr::TCODE_DIRECT_BRANCH:
			case TraceDqr::TCODE_INDIRECT_BRANCH:
			case TraceDqr::TCODE_INDIRECTBRANCHHISTORY:
			case TraceDqr::TCODE_INDIRECTBRANCHHISTORY_WS:
			case TraceDqr::TCODE_RESOURCEFULL:
				rc = processTraceMessage(nm,currentAddress[currentCore],lastFaddr[currentCore],lastTime[currentCore]);
				if (rc != TraceDqr::DQERR_OK) {
					printf("Error: NextInstruction(): state TRACE_STATE_RETIREMESSAGE: processTraceMessage()\n");

					status = TraceDqr::DQERR_ERR;
					state[currentCore] = TRACE_STATE_ERROR;

					return status;
				}

				if (msgInfo != nullptr) {
					messageInfo = nm;
					messageInfo.time = lastTime[currentCore];

					messageInfo.currentAddress = currentAddress[currentCore];

					if (messageInfo.processITCPrintData(itcPrint) == false) {
						*msgInfo = &messageInfo;
					}
				}

				if ((srcInfo != nullptr) && (*srcInfo == nullptr)) {
					Disassemble(currentAddress[currentCore]);

					sourceInfo.coreId = currentCore;
					*srcInfo = &sourceInfo;
				}

				// I don't think the b_type code below actaully does anything??? Remove??

				TraceDqr::BType b_type;
				b_type = TraceDqr::BTYPE_UNDEFINED;

				switch (nm.tcode) {
				case TraceDqr::TCODE_SYNC:
				case TraceDqr::TCODE_DIRECT_BRANCH_WS:
					break;
				case TraceDqr::TCODE_INCIRCUITTRACE_WS:
					if ((nm.ictWS.cksrc == TraceDqr::ICT_EXCEPTION) || (nm.ictWS.cksrc == TraceDqr::ICT_INTERRUPT)) {
						b_type = TraceDqr::BTYPE_EXCEPTION;
					}
					break;
				case TraceDqr::TCODE_INCIRCUITTRACE:
					if ((nm.ict.cksrc == TraceDqr::ICT_EXCEPTION) || (nm.ict.cksrc == TraceDqr::ICT_INTERRUPT)) {
						b_type = TraceDqr::BTYPE_EXCEPTION;
					}
					break;
				case TraceDqr::TCODE_INDIRECT_BRANCH_WS:
					b_type = nm.indirectBranchWS.b_type;
					break;
				case TraceDqr::TCODE_INDIRECT_BRANCH:
					b_type = nm.indirectBranch.b_type;
					break;
				case TraceDqr::TCODE_INDIRECTBRANCHHISTORY:
					b_type = nm.indirectHistory.b_type;
					break;
				case TraceDqr::TCODE_INDIRECTBRANCHHISTORY_WS:
					b_type = nm.indirectHistoryWS.b_type;
					break;
				case TraceDqr::TCODE_DIRECT_BRANCH:
				case TraceDqr::TCODE_RESOURCEFULL:
					// fall through
				default:
					break;
				}

				if (b_type == TraceDqr::BTYPE_EXCEPTION) {
					enterISR[currentCore] = TraceDqr::isInterrupt;
				}

				readNewTraceMessage = true;
				state[currentCore] = TRACE_STATE_GETNEXTMSG;
				break;
			case TraceDqr::TCODE_INCIRCUITTRACE:
			case TraceDqr::TCODE_INCIRCUITTRACE_WS:
				// these messages should have been retired immediately

				printf("Error: unexpected tcode of INCIRCUTTRACE or INCIRCUTTRACE_WS in state TRACE_STATE_RETIREMESSAGE\n");
				state[currentCore] = TRACE_STATE_ERROR;

				status = TraceDqr::DQERR_ERR;
				return status;
			case TraceDqr::TCODE_CORRELATION:
				// correlation has i_cnt, but no address info

				if (nm.haveTimestamp) {
					lastTime[currentCore] = processTS(TraceDqr::TS_rel,lastTime[currentCore],nm.timestamp);
				}

				if (msgInfo != nullptr) {
					messageInfo = nm;
					messageInfo.time = lastTime[currentCore];

					// leaving trace mode - currentAddress should be last faddr + i_cnt *2

					messageInfo.currentAddress = lastFaddr[currentCore] + nm.correlation.i_cnt*2;

					if (messageInfo.processITCPrintData(itcPrint) == false) {
						*msgInfo = &messageInfo;
					}
				}

				readNewTraceMessage = true;

				// leaving trace mode - need to get next sync

				state[currentCore] = TRACE_STATE_GETFIRSTSYNCMSG;
				break;
			case TraceDqr::TCODE_ERROR:
				printf("Error: Unexpected tcode TCODE_ERROR in state TRACE_STATE_RETIREMESSAGE\n");

				state[currentCore] = TRACE_STATE_ERROR;
				status = TraceDqr::DQERR_ERR;

				return status;
			case TraceDqr::TCODE_AUXACCESS_WRITE:
			case TraceDqr::TCODE_OWNERSHIP_TRACE:
				// these messages have no address or i-cnt info and should have been
				// instantly retired when they were read.

				state[currentCore] = TRACE_STATE_ERROR;
				status = TraceDqr::DQERR_ERR;

				return status;
			default:
				printf("Error: bad tcode type in state TRACE_STATE_RETIREMESSAGE\n");

				state[currentCore] = TRACE_STATE_ERROR;
				status = TraceDqr::DQERR_ERR;

				return status;
			}

			status = TraceDqr::DQERR_OK;
			return status;
		case TRACE_STATE_GETNEXTMSG:
//			printf("TRACE_STATE_GETNEXTMSG\n");

			// exit this state when message with i-cnt, history, taken, or not-taken is read

			switch (nm.tcode) {
			case TraceDqr::TCODE_DIRECT_BRANCH:
			case TraceDqr::TCODE_INDIRECT_BRANCH:
			case TraceDqr::TCODE_SYNC:
			case TraceDqr::TCODE_DIRECT_BRANCH_WS:
			case TraceDqr::TCODE_INDIRECT_BRANCH_WS:
			case TraceDqr::TCODE_CORRELATION:
			case TraceDqr::TCODE_INDIRECTBRANCHHISTORY:
			case TraceDqr::TCODE_INDIRECTBRANCHHISTORY_WS:
			case TraceDqr::TCODE_RESOURCEFULL:
				rc = counts->setCounts(&nm);
				if (rc != TraceDqr::DQERR_OK) {
					printf("Error: nextInstruction: state TRACE_STATE_GETNEXTMESSAGE Count::seteCounts()\n");

					state[currentCore] = TRACE_STATE_ERROR;

					status = rc;

					return status;
				}

				state[currentCore] = TRACE_STATE_GETNEXTINSTRUCTION;
				break;
			case TraceDqr::TCODE_INCIRCUITTRACE:
			case TraceDqr::TCODE_INCIRCUITTRACE_WS:
				// these message have no counts so they will be retired immeadiately

				rc = processTraceMessage(nm,currentAddress[currentCore],lastFaddr[currentCore],lastTime[currentCore]);
				if (rc != TraceDqr::DQERR_OK) {
					printf("Error: NextInstruction(): state TRACE_STATE_GETMSGWITHCOUNT: processTraceMessage()\n");

					status = TraceDqr::DQERR_ERR;
					state[currentCore] = TRACE_STATE_ERROR;

					return status;
				}

				if ((nm.getCKSRC() == TraceDqr::ICT_CONTROL) && (nm.getCKDF() == 0)) {
					// ICT_WS Control(0,0) only updates TS (if present). Does not change state or anything else
					addr = currentAddress[currentCore];
				}
				else {
					if ((nm.getCKSRC() == TraceDqr::ICT_EXT_TRIG) && (nm.getCKDF() == 0)) {
						// no dasm or src for ext trigger in HTM instruction traces
						addr = lastFaddr[currentCore];
					}
					else if ((nm.getCKSRC() == TraceDqr::ICT_WATCHPOINT) && (nm.getCKDF() == 0)) {
						// no dasm or src for ext trigger in HTM instruction tracaes
						addr = lastFaddr[currentCore];
					}
					else if ((instInfo != nullptr) || (srcInfo != nullptr)) {
						addr = currentAddress[currentCore];

						Disassemble(addr);

						if (instInfo != nullptr) {
							instructionInfo.qDepth = 0;
							instructionInfo.arithInProcess = 0;
							instructionInfo.loadInProcess = 0;
							instructionInfo.storeInProcess = 0;

							instructionInfo.coreId = currentCore;
							*instInfo = &instructionInfo;
//							(*instInfo)->CRFlag = TraceDqr::isNone;
//							(*instInfo)->brFlags = TraceDqr::BRFLAG_none;
							getCRBRFlags(nm.getCKSRC(),currentAddress[currentCore],(*instInfo)->CRFlag,(*instInfo)->brFlags);

							(*instInfo)->timestamp = lastTime[currentCore];
						}

						if (srcInfo != nullptr) {
							sourceInfo.coreId = currentCore;
							*srcInfo = &sourceInfo;
						}
					}
				}

				if (msgInfo != nullptr) {
					messageInfo = nm;
					messageInfo.time = lastTime[currentCore];
					messageInfo.currentAddress = addr;

					if (messageInfo.processITCPrintData(itcPrint) == false) {
						*msgInfo = &messageInfo;
					}
				}

				readNewTraceMessage = true;

				return status;
			case TraceDqr::TCODE_ERROR:
				state[currentCore] = TRACE_STATE_GETFIRSTSYNCMSG;

				nm.timestamp = 0;	// clear time because we have lost time
				currentAddress[currentCore] = 0;
				lastFaddr[currentCore] = 0;
				lastTime[currentCore] = 0;

				if (msgInfo != nullptr) {
					messageInfo = nm;
					messageInfo.time = lastTime[currentCore];
					messageInfo.currentAddress = currentAddress[currentCore];

					if (messageInfo.processITCPrintData(itcPrint) == false) {
						*msgInfo = &messageInfo;
					}
				}

				readNewTraceMessage = true;

				return status;
			case TraceDqr::TCODE_AUXACCESS_WRITE:
			case TraceDqr::TCODE_DATA_ACQUISITION:
			case TraceDqr::TCODE_OWNERSHIP_TRACE:
				// retire these instantly by returning them through msgInfo

				if (nm.haveTimestamp) {
					lastTime[currentCore] = processTS(TraceDqr::TS_rel,lastTime[currentCore],nm.timestamp);
				}

				if (msgInfo != nullptr) {
					messageInfo = nm;
					messageInfo.time = lastTime[currentCore];
					messageInfo.currentAddress = currentAddress[currentCore];

					if (messageInfo.processITCPrintData(itcPrint) == false) {
						*msgInfo = &messageInfo;
					}
				}

				// leave state along. Need to get another message with an i-cnt!

				readNewTraceMessage = true;

				return status;
			default:
				state[currentCore] = TRACE_STATE_ERROR;
				status = TraceDqr::DQERR_ERR;

				return status;
			}
			break;
		case TRACE_STATE_GETNEXTINSTRUCTION:
			if (counts->getCurrentCountType(currentCore) == TraceDqr::COUNTTYPE_none) {
                                if (globalDebugFlag) {
                                    printf("NextInstruction(): counts are exhausted\n");
                                }

				state[currentCore] = TRACE_STATE_RETIREMESSAGE;
				break;
			}

//			printf("state TRACE_STATE_GETNEXTINSTRUCTION\n");

			// Should first process addr, and then compute next addr!!! If can't compute next addr, it is an error.
			// Should always be able to process instruction at addr and compute next addr when we get here.
			// After processing next addr, if there are no more counts, retire trace message and get another

			addr = currentAddress[currentCore];

			uint32_t inst;
			int inst_size;
			TraceDqr::InstType inst_type;
			int32_t immediate;
			bool isBranch;
			int rc;
			TraceDqr::Reg rs1;
			TraceDqr::Reg rd;

			// getInstrucitonByAddress() should cache last instrucioton/address because I thjink
			// it gets called a couple times for each address/insruction in a row

			status = elfReader->getInstructionByAddress(addr,inst);
			if (status != TraceDqr::DQERR_OK) {
				printf("Error: getInstructionByAddress failed\n");

				state[currentCore] = TRACE_STATE_ERROR;

				return status;
			}

			// figure out how big the instruction is

//			decode instruction/decode instruction size should cache their results (at least last one)
//			because it gets called a few times here!

			rc = decodeInstruction(inst,inst_size,inst_type,rs1,rd,immediate,isBranch);
			if (rc != 0) {
				printf("Error: Cann't decode size of instruction %04x\n",inst);

				state[currentCore] = TRACE_STATE_ERROR;
				status = TraceDqr::DQERR_ERR;

				return status;
			}

			Disassemble(addr);

			// compute next address (retire this instruction)

			// nextAddr() will also update counts
			//
			// nextAddr() computes next address if possible, consumes counts

			// nextAddr can usually compute the next address, but not always. If it can't, it returns
			// -1 as the next address.  This should never happen for conditional branches because we
			// should always have enough informatioon. But it can happen for indirect branches. For indirect
			// branches, retiring the current trace message (should be an indirect branch or indirect
			// brnach with sync) will set the next address correclty.

			status = nextAddr(currentCore,currentAddress[currentCore],addr,nm.tcode,crFlag,brFlags);
			if (status != TraceDqr::DQERR_OK) {
				printf("Error: nextAddr() failed\n");

				state[currentCore] = TRACE_STATE_ERROR;

				return status;
			}

			// if addr == -1 and brFlags == BRFLAG_unknown, we need to read another trace message
			// which hopefully with have history bits. Do not return the instruciton yet - we will
			// retry it after getting another trace message

			// if addr == -1 and brflags != BRFLAG_unknown, and current counts type != none, we have an
			// error.

			// if addr == -1 and brflags != BRFLAG_unkonw and current count type == none, all should
			// be good. We will return the instruction and read another message

			if (addr == (TraceDqr::ADDRESS)-1) {
				if (brFlags == TraceDqr::BRFLAG_unknown) {
					// read another trace message and retry

					state[currentCore] = TRACE_STATE_RETIREMESSAGE;
					break; // this break exits trace_state_getnextinstruction!
				}
				else if (counts->getCurrentCountType(currentCore) != TraceDqr::COUNTTYPE_none) {
					// error
					// must have a JR/JALR or exception/exception return to get here, and the CR stack is empty

					printf("Error: getCurrentCountType(core:%d) still has counts; have countType: %d\n",currentCore,counts->getCurrentCountType(currentCore));
					char d[64];

					instructionInfo.instructionToText(d,sizeof d,2);
					printf("%08llx:    %s\n",currentAddress[currentCore],d);

					state[currentCore] = TRACE_STATE_ERROR;

					status = TraceDqr::DQERR_ERR;

//					nm.dumpRawMessage();
//					nm.dump();
//
//					rc = sfp->readNextTraceMsg(nm,analytics,haveMsg);
//
//					if (rc != TraceDqr::DQERR_OK) {
//						printf("Error: Trace file does not contain any trace messages, or is unreadable\n");
//					} else if (haveMsg != false) {
//						nm.dumpRawMessage();
//						nm.dump();
//					}

					return status;
				}
			}

			currentAddress[currentCore] = addr;

			uint32_t prevCycle;

			if (caTrace != nullptr) {
				if (syncCount > 0) {
					if (caSyncAddr == instructionInfo.address) {
//						printf("ca sync successful at addr %08x\n",caSyncAddr);

						syncCount = 0;
					}
					else {
						syncCount -= 1;
						if (syncCount == 0) {
							printf("Error: unable to sync CA trace and instruction trace\n");
							state[currentCore] = TRACE_STATE_ERROR;
							status = TraceDqr::DQERR_ERR;
							return status;
						}
					}
				}

				if (syncCount == 0) {
					status = caTrace->consume(caFlags,inst_type,pipeCycles,viStartCycles,viFinishCycles,qDepth,arithInProcess,loadInProcess,storeInProcess);
					if (status == TraceDqr::DQERR_EOF) {
						state[currentCore] = TRACE_STATE_DONE;
						return status;
					}

					if (status != TraceDqr::DQERR_OK) {
						state[currentCore] = TRACE_STATE_ERROR;
						return status;
					}

					prevCycle = lastCycle[currentCore];

					eCycleCount[currentCore] = pipeCycles - prevCycle;

					lastCycle[currentCore] = pipeCycles;
				}
			}

			if (instInfo != nullptr) {
				instructionInfo.qDepth = qDepth;
				instructionInfo.arithInProcess = arithInProcess;
				instructionInfo.loadInProcess = loadInProcess;
				instructionInfo.storeInProcess = storeInProcess;

				qDepth = 0;
				arithInProcess = 0;
				loadInProcess = 0;
				storeInProcess = 0;

				instructionInfo.coreId = currentCore;
				*instInfo = &instructionInfo;
				(*instInfo)->CRFlag = (crFlag | enterISR[currentCore]);
				enterISR[currentCore] = TraceDqr::isNone;
				(*instInfo)->brFlags = brFlags;

				if ((caTrace != nullptr) && (syncCount == 0)) {
					// note: start signal is one cycle after execution begins. End signal is two cycles after end

					(*instInfo)->timestamp = pipeCycles;
					(*instInfo)->pipeCycles = eCycleCount[currentCore];

					(*instInfo)->VIStartCycles = viStartCycles - prevCycle;
					(*instInfo)->VIFinishCycles = viFinishCycles - prevCycle - 1;

					(*instInfo)->caFlags = caFlags;
				}
				else {
					(*instInfo)->timestamp = lastTime[currentCore];
				}
			}

//			lastCycle[currentCore] = cycles;

			if (srcInfo != nullptr) {
				sourceInfo.coreId = currentCore;
				*srcInfo = &sourceInfo;
			}

			status = analytics.updateInstructionInfo(currentCore,inst,inst_size,crFlag,brFlags);
			if (status != TraceDqr::DQERR_OK) {
				state[currentCore] = TRACE_STATE_ERROR;

				printf("Error: updateInstructionInfo() failed\n");
				return status;
			}


			if (counts->getCurrentCountType(currentCore) != TraceDqr::COUNTTYPE_none) {
				// still have valid counts. Keep running nextInstruction!

				return status;
			}

			// counts have expired. Retire this message and read next trace message and update. This should cause the
			// current process instruction (above) to be returned along with the retired trace message

			// if the instruction processed above in an indirect branch, counts should be zero and
			// retiring this trace message should set the next address (message should be an indirect
			// brnach type message)

			state[currentCore] = TRACE_STATE_RETIREMESSAGE;
			break;
		case TRACE_STATE_DONE:
			status = TraceDqr::DQERR_DONE;
			return status;
		case TRACE_STATE_ERROR:
			status = TraceDqr::DQERR_ERR;
			return status;
		default:
			printf("Error: Trace::NextInstruction():unknown\n");

			state[currentCore] = TRACE_STATE_ERROR;
			status = TraceDqr::DQERR_ERR;
			return status;
		}
	}

	status = TraceDqr::DQERR_OK;
	return TraceDqr::DQERR_OK;
}
