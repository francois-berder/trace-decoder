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
#include <cassert>

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

			QStart += 1;
			if (QStart >= traceQSize) {
				QStart = 0;
			}

			return TraceDqr::DQERR_OK;
		}

		if ((caTraceQ[QStart].record & TraceDqr::CAVFLAG_V1) != 0) {
			pipe = TraceDqr::CAFLAG_PIPE1;
			cycles = caTraceQ[QStart].cycle;
			caTraceQ[QStart].record &= ~TraceDqr::CAVFLAG_V1;

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

				QStart += 1;
				if (QStart >= traceQSize) {
					QStart = 0;
				}

				return TraceDqr::DQERR_OK;
			}

			if ((caTraceQ[QStart].record & TraceDqr::CAVFLAG_V1) != 0) {
				pipe = TraceDqr::CAFLAG_PIPE1;
				cycles = caTraceQ[QStart].cycle;
				caTraceQ[QStart].record &= ~TraceDqr::CAVFLAG_V1;

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

TraceDqr::DQErr CATrace::consumeCAVector(int &QStart,TraceDqr::CAVectorTraceFlags type,uint32_t &cycles,uint8_t &qInfo,uint8_t &arithInfo,uint8_t &loadInfo, uint8_t &storeInfo)
{
	if (caTraceQ == nullptr) {
		return TraceDqr::DQERR_ERR;
	}

	// first look for pipe info in Q

	// look in Q and see if record with matching type is found

	TraceDqr::DQErr rc;

	if (QStart == traceQIn) {
		// get next record

		rc = parseNextVectorRecord(QStart);	// reads a record and adds it to the Q (adds five entries to the Q. Packs the Q if needed
		if (rc != TraceDqr::DQERR_OK) {
			status = rc;

			return rc;
		}
	}

	uint8_t tQInfo = caTraceQ[QStart].qDepth;
	uint8_t tArithInfo = caTraceQ[QStart].arithInProcess;
	uint8_t tLoadInfo = caTraceQ[QStart].loadInProcess;
	uint8_t tStoreInfo = caTraceQ[QStart].storeInProcess;

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

// class trace methods

int Trace::decodeInstructionSize(uint32_t inst, int &inst_size)
{
  return disassembler->decodeInstructionSize(inst,inst_size);
}

int Trace::decodeInstruction(uint32_t instruction,int &inst_size,TraceDqr::InstType &inst_type,TraceDqr::Reg &rs1,TraceDqr::Reg &rd,int32_t &immediate,bool &is_branch)
{
	return disassembler->decodeInstruction(instruction,getArchSize(),inst_size,inst_type,rs1,rd,immediate,is_branch);
}

Trace::Trace(char *tf_name,char *ef_name,int numAddrBits,uint32_t addrDispFlags,int srcBits,uint32_t freq)
{
  sfp          = nullptr;
  elfReader    = nullptr;
  symtab       = nullptr;
  disassembler = nullptr;
  caTrace      = nullptr;
  counts       = nullptr;//delete this line if compile error

  syncCount = 0;
  caSyncAddr = (TraceDqr::ADDRESS)-1;

  assert(tf_name != nullptr);

  traceType = TraceDqr::TRACETYPE_BTM;	// assume BTM trace until HTM message is seen

  itcPrint = nullptr;

  srcbits = srcBits;

  analytics.setSrcBits(srcBits);

  sfp = new (std::nothrow) SliceFileParser(tf_name,srcbits);

  assert(sfp != nullptr);

  if (sfp->getErr() != TraceDqr::DQERR_OK) {
	printf("Error: cannot open trace file '%s' for input\n",tf_name);
	delete sfp;
	sfp = nullptr;

	status = TraceDqr::DQERR_ERR;

	return;
  }

  if (ef_name != nullptr ) {
	// create elf object

//    printf("ef_name:%s\n",ef_name);

    elfReader = new (std::nothrow) ElfReader(ef_name);

    assert(elfReader != nullptr);

    if (elfReader->getStatus() != TraceDqr::DQERR_OK) {
    	if (sfp != nullptr) {
    		delete sfp;
    		sfp = nullptr;
    	}

    	delete elfReader;
    	elfReader = nullptr;

    	status = TraceDqr::DQERR_ERR;

    	return;
    }

    // create disassembler object

    bfd *abfd;
    abfd = elfReader->get_bfd();

	disassembler = new (std::nothrow) Disassembler(abfd,true);

	assert(disassembler != nullptr);

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

		return;
	}

    // get symbol table

    symtab = elfReader->getSymtab();
    if (symtab == nullptr) {
    	delete elfReader;
    	elfReader = nullptr;

    	delete sfp;
    	sfp = nullptr;

    	status = TraceDqr::DQERR_ERR;

    	return;
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

  counts = new Count [DQR_MAXCORES];

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

  startMessageNum  = 0;
  endMessageNum    = 0;

  for (int i = 0; (size_t)i < (sizeof messageSync / sizeof messageSync[0]); i++) {
	  messageSync[i] = nullptr;
  }

  instructionInfo.CRFlag = TraceDqr::isNone;
  instructionInfo.brFlags = TraceDqr::BRFLAG_none;

  instructionInfo.address = 0;
  instructionInfo.instruction = 0;
  instructionInfo.instSize = 0;

  if (numAddrBits != 0 ) {
	  instructionInfo.addrSize = numAddrBits;
  }
  else if (elfReader == nullptr) {
	  instructionInfo.addrSize = 0;
  }
  else {
	  instructionInfo.addrSize = elfReader->getBitsPerAddress();
  }

  instructionInfo.addrDispFlags = addrDispFlags;

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

  NexusMessage::targetFrequency = freq;

  tsSize = 40;
  tsBase = 0;

  for (int i = 0; (size_t)i < sizeof enterISR / sizeof enterISR[0]; i++) {
	  enterISR[i] = TraceDqr::isNone;
  }

  status = TraceDqr::DQERR_OK;
}

Trace::~Trace()
{
	cleanUp();
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

	if (counts != nullptr) {
		delete [] counts;
		counts = nullptr;
	}

	if (disassembler != nullptr) {
		delete disassembler;
		disassembler = nullptr;
	}

	for (int i = 0; (size_t)i < (sizeof messageSync / sizeof messageSync[0]); i++) {
		if (messageSync[i] != nullptr) {
			delete messageSync[i];
			messageSync[i] = nullptr;
		}
	}

	if (caTrace != nullptr) {
		delete caTrace;
		caTrace = nullptr;
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

TraceDqr::DQErr Trace::setTraceRange(int start_msg_num,int stop_msg_num)
{
	if (start_msg_num < 0) {
		status = TraceDqr::DQERR_ERR;
		return TraceDqr::DQERR_ERR;
	}

	if (stop_msg_num < 0) {
		status = TraceDqr::DQERR_ERR;
		return TraceDqr::DQERR_ERR;
	}

	if ((stop_msg_num != 0) && (start_msg_num > stop_msg_num)) {
		status = TraceDqr::DQERR_ERR;
		return TraceDqr::DQERR_ERR;
	}

	startMessageNum = start_msg_num;
	endMessageNum = stop_msg_num;

	if ((startMessageNum != 0) || (endMessageNum != 0)) {
		for (int i = 0; (size_t)i < sizeof state / sizeof state[0]; i++) {
			state[i] = TRACE_STATE_GETSTARTTRACEMSG;
		}
	}

	return TraceDqr::DQERR_OK;
}

TraceDqr::DQErr Trace::setPathType(TraceDqr::pathType pt)
{
	if (disassembler != nullptr) {
		disassembler->setPathType(pt);

		return TraceDqr::DQERR_OK;
	}

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

	assert(disassembler != nullptr);

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

	status = TraceDqr::DQERR_OK;

	return status;
}

TraceDqr::TIMESTAMP Trace::adjustTsForWrap(TraceDqr::tsType tstype, TraceDqr::TIMESTAMP lastTs, TraceDqr::TIMESTAMP newTs)
{
	TraceDqr::TIMESTAMP ts;

	if (tstype == TraceDqr::TS_full) {
		ts = lastTs + tsBase;
	}
	else {
		ts = (lastTs ^ newTs) + tsBase;
	}

	if (ts < lastTs) {
		ts += (1 << tsSize);
		tsBase += (1 << tsSize);
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
	assert(disassembler != nullptr);

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

TraceDqr::DQErr Trace::setITCPrintOptions(int buffSize,int channel)
{
	if (itcPrint != nullptr) {
		delete itcPrint;
		itcPrint = nullptr;
	}

	itcPrint = new ITCPrint(1 << srcbits,buffSize,channel);

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
			if (globalDebugFlag) printf("Debug: call: core %d, new address %08llx (don't know dst yet), %d item now on stack\n",core,pc,counts->getNumOnStack(core));
			pc = -1;
			crFlag |= TraceDqr::isCall;
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
	case TraceDqr::TCODE_OWNERSHIP_TRACE:
	case TraceDqr::TCODE_DIRECT_BRANCH:
	case TraceDqr::TCODE_DATA_ACQUISITION:
	case TraceDqr::TCODE_ERROR:
	case TraceDqr::TCODE_AUXACCESS_WRITE:
	case TraceDqr::TCODE_RESOURCEFULL:
	case TraceDqr::TCODE_CORRELATION:
		if (nm.haveTimestamp) {
			ts = adjustTsForWrap(TraceDqr::TS_rel,ts,nm.timestamp);
		}
		break;
	case TraceDqr::TCODE_INDIRECT_BRANCH:
		if (nm.haveTimestamp) {
			ts = adjustTsForWrap(TraceDqr::TS_rel,ts,nm.timestamp);
		}
		faddr = faddr ^ (nm.indirectBranch.u_addr << 1);
		pc = faddr;
		break;
	case TraceDqr::TCODE_SYNC:
		if (nm.haveTimestamp) {
			ts = adjustTsForWrap(TraceDqr::TS_full,ts,nm.timestamp);
		}
		faddr = nm.sync.f_addr << 1;
		pc = faddr;
		counts->resetStack(nm.coreId);
		counts->resetCounts(nm.coreId);
		break;
	case TraceDqr::TCODE_DIRECT_BRANCH_WS:
		if (nm.haveTimestamp) {
			ts = adjustTsForWrap(TraceDqr::TS_full,ts,nm.timestamp);
		}
		faddr = nm.directBranchWS.f_addr << 1;
		pc = faddr;
		counts->resetStack(nm.coreId);
		counts->resetCounts(nm.coreId);
		break;
	case TraceDqr::TCODE_INDIRECT_BRANCH_WS:
		if (nm.haveTimestamp) {
			ts = adjustTsForWrap(TraceDqr::TS_full,ts,nm.timestamp);
		}
		faddr = nm.indirectBranchWS.f_addr << 1;
		pc = faddr;
		counts->resetStack(nm.coreId);
		counts->resetCounts(nm.coreId);
		break;
	case TraceDqr::TCODE_INDIRECTBRANCHHISTORY:
		if (nm.haveTimestamp) {
			ts = adjustTsForWrap(TraceDqr::TS_rel,ts,nm.timestamp);
		}
		faddr = faddr ^ (nm.indirectHistory.u_addr << 1);
		pc = faddr;
		break;
	case TraceDqr::TCODE_INDIRECTBRANCHHISTORY_WS:
		if (nm.haveTimestamp) {
			ts = adjustTsForWrap(TraceDqr::TS_full,ts,nm.timestamp);
		}
		faddr = nm.indirectHistoryWS.f_addr << 1;
		pc = faddr;
		counts->resetStack(nm.coreId);
		counts->resetCounts(nm.coreId);
		break;
	case TraceDqr::TCODE_INCIRCUITTRACE:
		if (nm.haveTimestamp) {
			ts = adjustTsForWrap(TraceDqr::TS_rel,ts,nm.timestamp);
		}

		// update faddr but not pc

		faddr = faddr ^ (nm.ict.ckdata << 1);
//		pc = faddr;
		break;
	case TraceDqr::TCODE_INCIRCUITTRACE_WS:
		if (nm.haveTimestamp) {
			ts = adjustTsForWrap(TraceDqr::TS_full,ts,nm.timestamp);
		}

		// update faddr but not pc

		faddr = nm.ictWS.ckdata << 1;
//		pc = faddr;
		// don't reset call/return stack for this message - not a normal sync!
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
	assert(sfp != nullptr);

	TraceDqr::DQErr rc;
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

		if (readNewTraceMessage != false) {
			rc = sfp->readNextTraceMsg(nm,analytics,haveMsg);

			if (rc != TraceDqr::DQERR_OK) {
				// have an error. either eof, or error

				status = rc;

				if (status == TraceDqr::DQERR_EOF) {
					state[currentCore] = TRACE_STATE_DONE;
				}
				else {
					if ( messageSync[currentCore] != nullptr) {
						printf("Error: Trace file does not contain %d trace messages. %d message found\n",startMessageNum,messageSync[currentCore]->lastMsgNum);
					}
					else {
						printf("Error: Trace file does not contain any trace messages, or is unreadable\n");
					}

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
					}
					break;
				case TraceDqr::TCODE_RESOURCEFULL:
				case TraceDqr::TCODE_INDIRECTBRANCHHISTORY:
				case TraceDqr::TCODE_INDIRECTBRANCHHISTORY_WS:
					traceType = TraceDqr::TRACETYPE_HTM;
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
		}

		switch (state[currentCore]) {
		case TRACE_STATE_SYNCCATE:
//			printf("TRACE_STATE_SYNCCATE\n");

			if (caTrace == nullptr) {
				// have an error! Should never have TRACE_STATE_SYNC whthout a caTrace ptr
				printf("Error: caTrace is null\n");
				status = TraceDqr::DQERR_ERR;
				state[currentCore] = TRACE_STATE_ERROR;
				return status;
			}

			// loop through trace messages until we find a sync of some kind. First sync should do it
			// sync reason must be correct

			TraceDqr::ADDRESS teAddr;

			switch (nm.tcode) {
			case TraceDqr::TCODE_OWNERSHIP_TRACE:
			case TraceDqr::TCODE_DIRECT_BRANCH:
			case TraceDqr::TCODE_INDIRECT_BRANCH:
			case TraceDqr::TCODE_DATA_ACQUISITION:
			case TraceDqr::TCODE_ERROR:
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
			case TraceDqr::TCODE_DATA_WRITE_WS:
			case TraceDqr::TCODE_DATA_READ_WS:
			case TraceDqr::TCODE_WATCHPOINT:
			case TraceDqr::TCODE_CORRECTION:
			case TraceDqr::TCODE_DATA_WRITE:
			case TraceDqr::TCODE_DATA_READ:
			case TraceDqr::TCODE_DEBUG_STATUS:
			case TraceDqr::TCODE_DEVICE_ID:
				readNewTraceMessage = true;

				// here we return the trace messages before we have actually started tracing
				// this could be at the start of a trace, or after leaving a trace because of
				// a correlation message

				if (msgInfo != nullptr) {
					messageInfo = nm;

					// if doing pc-sampling and msg type is INCIRCUITTRACE_WS, we want to use faddr
					// and not currentAddress

					if (nm.tcode == TraceDqr::TCODE_INCIRCUITTRACE_WS) {
						messageInfo.currentAddress = lastFaddr[currentCore];
					}
					else {
						messageInfo.currentAddress = currentAddress[currentCore];
					}

					messageInfo.time = lastTime[currentCore];

					if (messageInfo.processITCPrintData(itcPrint) == false) {
						*msgInfo = &messageInfo;
					}
				}

				status = TraceDqr::DQERR_OK;

				return status;
			case TraceDqr::TCODE_SYNC:
			case TraceDqr::TCODE_DIRECT_BRANCH_WS:
			case TraceDqr::TCODE_INDIRECT_BRANCH_WS:
			case TraceDqr::TCODE_INCIRCUITTRACE_WS: // this may be problem. Can't start on this
			case TraceDqr::TCODE_INDIRECTBRANCHHISTORY_WS:
				// sync reason should be either EXIT_DEBUG or TRACE_ENABLE. Otherwise, keep looking
				TraceDqr::SyncReason sr;

				sr = nm.getSyncReason();
				switch (sr) {
				case TraceDqr::SYNC_EXIT_DEBUG:
				case TraceDqr::SYNC_TRACE_ENABLE:
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
					readNewTraceMessage = true;

					// here we return the trace messages before we have actually started tracing
					// this could be at the start of a trace, or after leaving a trace because of
					// a correlation message

					if (msgInfo != nullptr) {
						messageInfo = nm;

						// if doing pc-sampling and msg type is INCIRCUITTRACE_WS, we want to use faddr
						// and not currentAddress

						if (nm.tcode == TraceDqr::TCODE_INCIRCUITTRACE_WS) {
							messageInfo.currentAddress = lastFaddr[currentCore];
						}
						else {
							messageInfo.currentAddress = currentAddress[currentCore];
						}

						messageInfo.time = lastTime[currentCore];

						if (messageInfo.processITCPrintData(itcPrint) == false) {
							*msgInfo = &messageInfo;
						}
					}

					status = TraceDqr::DQERR_OK;

					return status;
					break;
				case TraceDqr::SYNC_NONE:
				default:
					printf("Error: invalid sync reason\n");
					status = TraceDqr::DQERR_ERR;
					state[currentCore] = TRACE_STATE_ERROR;

					return status;
				}
				break;
			case TraceDqr::TCODE_UNDEFINED:
			default:
				printf("Error: NextInstruction(): Undefined tcode type (%d)\n",nm.tcode);
				status = TraceDqr::DQERR_ERR;
				state[currentCore] = TRACE_STATE_ERROR;

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

					// We just use an itype of maybe we need to get the instruciton ad addr and get its type??

					rc = caTrace->consume(caFlags,TraceDqr::INST_SCALER,pipeCycles,viStartCycles,viFinishCycles,qDepth,arithInProcess,loadInProcess,storeInProcess);

					if (rc == TraceDqr::DQERR_EOF) {
						state[currentCore = TRACE_STATE_DONE];

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
//				printf("ca sync found at address %08x, cycles: %d\n",caSyncAddr,pipeCycles);
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

			state[currentCore] = TRACE_STATE_GETFIRSTSYNCMSG;
			break;
		case TRACE_STATE_GETSTARTTRACEMSG:
			if (startMessageNum <= 1) {
				// if starting at beginning, just switch to normal state for starting

				state[currentCore] = TRACE_STATE_GETFIRSTSYNCMSG;
				break;
			}

			if (messageSync[currentCore] == nullptr) {
				messageSync[currentCore] = new NexusMessageSync;
			}

			switch (nm.tcode) {
			case TraceDqr::TCODE_SYNC:
			case TraceDqr::TCODE_DIRECT_BRANCH_WS:
			case TraceDqr::TCODE_INDIRECT_BRANCH_WS:
			case TraceDqr::TCODE_INDIRECTBRANCHHISTORY_WS:
				messageSync[currentCore]->msgs[0] = nm;
				messageSync[currentCore]->index = 1;

				messageSync[currentCore]->firstMsgNum = nm.msgNum;
				messageSync[currentCore]->lastMsgNum = nm.msgNum;

				if (nm.msgNum >= startMessageNum) {
					// do not need to set read next message - compute starting address handles everything!

					state[currentCore] = TRACE_STATE_COMPUTESTARTINGADDRESS;
				}
				else {
					readNewTraceMessage = true;
				}
				break;
			case TraceDqr::TCODE_DIRECT_BRANCH:
			case TraceDqr::TCODE_INDIRECT_BRANCH:
			case TraceDqr::TCODE_INDIRECTBRANCHHISTORY:
			case TraceDqr::TCODE_RESOURCEFULL:
			case TraceDqr::TCODE_INCIRCUITTRACE:
			case TraceDqr::TCODE_INCIRCUITTRACE_WS:	// can't be used as a sync message to start
				if (messageSync[currentCore]->index == 0) {
					if (nm.msgNum >= startMessageNum) {
						// can't start at this trace message because we have not seen a sync yet
						// so we cannot compute the address

						state[currentCore] = TRACE_STATE_ERROR;

						printf("Error: cannot start at trace message %d because no preceding sync\n",startMessageNum);

						status = TraceDqr::DQERR_ERR;

						return status;
					}

					// first message. Not a sync, so ignore, but read another message
					readNewTraceMessage = true;
				}
				else {
					// stuff it in the list

					messageSync[currentCore]->msgs[messageSync[currentCore]->index] = nm;
					messageSync[currentCore]->index += 1;

					// don't forget to check for messageSync->msgs[] overrun!!

					if (messageSync[currentCore]->index >= (int)(sizeof messageSync[currentCore]->msgs / sizeof messageSync[currentCore]->msgs[0])) {
						status = TraceDqr::DQERR_ERR;
						state[currentCore] = TRACE_STATE_ERROR;

						return status;
					}

					messageSync[currentCore]->lastMsgNum = nm.msgNum;

					if (nm.msgNum >= startMessageNum) {
						state[currentCore] = TRACE_STATE_COMPUTESTARTINGADDRESS;
					}
					else {
						readNewTraceMessage = true;
					}
				}
				break;
			case TraceDqr::TCODE_CORRELATION:
				// we are leaving trace mode, so we no longer know address we are at until
				// we see a sync message, so set index to 0 to start over

				messageSync[currentCore]->index = 0;
				readNewTraceMessage = true;
				break;
			case TraceDqr::TCODE_AUXACCESS_WRITE:
			case TraceDqr::TCODE_OWNERSHIP_TRACE:
			case TraceDqr::TCODE_ERROR:
				// these message types we just stuff in the list in case we are interested in the
				// information later

				messageSync[currentCore]->msgs[messageSync[currentCore]->index] = nm;
				messageSync[currentCore]->index += 1;

				// don't forget to check for messageSync->msgs[] overrun!!

				if (messageSync[currentCore]->index >= (int)(sizeof messageSync[currentCore]->msgs / sizeof messageSync[currentCore]->msgs[0])) {
					status = TraceDqr::DQERR_ERR;
					state[currentCore] = TRACE_STATE_ERROR;

					return status;
				}

				messageSync[currentCore]->lastMsgNum = nm.msgNum;

				if (nm.msgNum >= startMessageNum) {
					state[currentCore] = TRACE_STATE_COMPUTESTARTINGADDRESS;
				}
				else {
					readNewTraceMessage = true;
				}
				break;
			default:
				state[currentCore] = TRACE_STATE_ERROR;

				status = TraceDqr::DQERR_ERR;

				return status;
			}
			break;
		case TRACE_STATE_COMPUTESTARTINGADDRESS:
			// compute address from trace message queued up in messageSync->msgs

			// first message should be a sync type. If not, error!

			if (messageSync[currentCore]->index <= 0) {
				printf("Error: nextInstruction(): state TRACE_STATE_COMPUTESTARTGINGADDRESS has no trace messages\n");

				state[currentCore] = TRACE_STATE_ERROR;

				status = TraceDqr::DQERR_ERR;

				return status;
			}

			// first message should be some kind of sync message

			lastFaddr[currentCore] = messageSync[currentCore]->msgs[0].getF_Addr() << 1;
			if (lastFaddr[currentCore] == (TraceDqr::ADDRESS)(-1 << 1)) {
				printf("Error: nextInstruction: state TRACE_STATE_COMPUTESTARTINGADDRESS: no starting F-ADDR for first trace message\n");

				state[currentCore] = TRACE_STATE_ERROR;

				status = TraceDqr::DQERR_ERR;

				return status;
			}

			currentAddress[currentCore] = lastFaddr[currentCore];

			if (messageSync[currentCore]->msgs[0].haveTimestamp) {
				// don't need to adjust ts for wrap, because this is the first sync and it will not wrap
				lastTime[currentCore] = messageSync[currentCore]->msgs[0].timestamp;
			}

			// thow away all counts from the first trace message!! then use them starting with the second

			// start at one because we have already consumed 0 (its faddr and timestamp are all we
			// need).

			counts->resetCounts(currentCore);
			counts->resetStack(currentCore);

			for (int i = 1; i < messageSync[currentCore]->index; i++) {
				rc = counts->setCounts(&messageSync[currentCore]->msgs[i]);
				if (rc != TraceDqr::DQERR_OK) {
					printf("Error: nextInstruction: state TRACE_STATE_COMPUTESTARTINGADDRESS: Count::seteCounts()\n");

					state[currentCore] = TRACE_STATE_ERROR;

					status = rc;

					return status;
				}

				TraceDqr::ADDRESS currentPc = currentAddress[currentCore];
				TraceDqr::ADDRESS newPc;

				// now run through the code updating the pc until we consume all the counts for the current message

				while (counts->getCurrentCountType(currentCore) != TraceDqr::COUNTTYPE_none) {
					if (currentPc == (TraceDqr::ADDRESS)-1) {
						printf("Error: NextInstruction(): state TRACE_STATE_COMPUTESTARTINGADDRESS: can't compute address\n");

						status = TraceDqr::DQERR_ERR;

						state[currentCore] = TRACE_STATE_ERROR;

						return status;
					}

//					think I need to redo this section to match nextInstruction logic!

					rc = nextAddr(currentCore,currentPc,newPc,nm.tcode,crFlag,brFlags);
					if (newPc == (TraceDqr::ADDRESS)-1) {
						if (counts->getCurrentCountType(currentCore != TraceDqr::COUNTTYPE_none)) {
							printf("Error: NextInstruction(): state TRACE_STATE_COMPUTESTARTINGADDRESS: counts not consumed\n");

							status = TraceDqr::DQERR_ERR;
							state[currentCore] = TRACE_STATE_ERROR;

							return status;
						}
					}
					else {
						currentPc = newPc;
					}
				}

				// If needed, adjust pc based on u-addr/f-addr info for current trace message

				rc = processTraceMessage(messageSync[currentCore]->msgs[i],currentPc,lastFaddr[currentCore],lastTime[currentCore]);
				if (rc != TraceDqr::DQERR_OK) {
					printf("Error: NextInstruction(): state TRACE_STATE_COMPUTESTARTINGADDRESS: counts not consumed\n");

					status = TraceDqr::DQERR_ERR;
					state[currentCore] = TRACE_STATE_ERROR;

					return status;
				}

				currentAddress[currentCore] = currentPc;
			}

			readNewTraceMessage = true;
			state[currentCore] = TRACE_STATE_GETNEXTMSG;

			if ((msgInfo != nullptr) && (messageSync[currentCore]->index > 0)) {
				messageInfo = messageSync[currentCore]->msgs[messageSync[currentCore]->index-1];
				messageInfo.currentAddress = currentAddress[currentCore];
				messageInfo.time = lastTime[currentCore];

				if (messageInfo.processITCPrintData(itcPrint) == false) {
					*msgInfo = &messageInfo;

					status = TraceDqr::DQERR_OK;

					return status;
				}
			}
			break;
		case TRACE_STATE_GETFIRSTSYNCMSG:
			//printf("TRACE_STATE_GETFIRSTSYNCMSG\n");

			// read trace messages until a sync is found. Should be the first message normally, unless wrapped buffer

			// only exit this state when sync type message is found

			if ((endMessageNum != 0) && (nm.msgNum > endMessageNum)) {
				printf("stopping at trace message %d\n",endMessageNum);

				state[currentCore] = TRACE_STATE_DONE;
				status = TraceDqr::DQERR_DONE;

				return status;
			}

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

				state[currentCore] = TRACE_STATE_GETSECONDMSG;
				break;
			case TraceDqr::TCODE_INCIRCUITTRACE_WS: // If we are looking for a starting sync and see this, all is cool!!
				rc = processTraceMessage(nm,currentAddress[currentCore],lastFaddr[currentCore],lastTime[currentCore]);
				if (rc != TraceDqr::DQERR_OK) {
					printf("Error: NextInstruction(): state TRACE_STATE_GETFIRSTSYNCMSG: processTraceMessage()\n");

					status = TraceDqr::DQERR_ERR;
					state[currentCore] = TRACE_STATE_ERROR;

					return status;
				}

				if ((instInfo != nullptr) || (srcInfo != nullptr)) {
					Disassemble(lastFaddr[currentCore]);

					if (instInfo != nullptr) {
						instructionInfo.qDepth = 0;
						instructionInfo.arithInProcess = 0;
						instructionInfo.loadInProcess = 0;
						instructionInfo.storeInProcess = 0;

						instructionInfo.coreId = currentCore;
						*instInfo = &instructionInfo;
						(*instInfo)->CRFlag = (crFlag | enterISR[currentCore]);
						enterISR[currentCore] = TraceDqr::isNone;
						(*instInfo)->brFlags = brFlags;

						(*instInfo)->timestamp = lastTime[currentCore];
					}

					if (srcInfo != nullptr) {
						sourceInfo.coreId = currentCore;
						*srcInfo = &sourceInfo;
					}
				}

				state[currentCore] = TRACE_STATE_GETSECONDMSG;
				break;
			case TraceDqr::TCODE_OWNERSHIP_TRACE:
			case TraceDqr::TCODE_DIRECT_BRANCH:
			case TraceDqr::TCODE_INDIRECT_BRANCH:
			case TraceDqr::TCODE_DATA_ACQUISITION:
			case TraceDqr::TCODE_ERROR:
			case TraceDqr::TCODE_CORRELATION:
			case TraceDqr::TCODE_INDIRECTBRANCHHISTORY:
			case TraceDqr::TCODE_RESOURCEFULL:
			case TraceDqr::TCODE_INCIRCUITTRACE:
				nm.timestamp = 0;	// don't start clock until we have a sync and a full time
				lastTime[currentCore] = 0;
				currentAddress[currentCore] = 0;
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

			readNewTraceMessage = true;

			// here we return the trace messages before we have actually started tracing
			// this could be at the start of a trace, or after leaving a trace because of
			// a correlation message

			if (msgInfo != nullptr) {
				messageInfo = nm;

				// if doing pc-sampling and msg type is INCIRCUITTRACE_WS, we want to use faddr
				// and not currentAddress

				if (nm.tcode == TraceDqr::TCODE_INCIRCUITTRACE_WS) {
					messageInfo.currentAddress = lastFaddr[currentCore];
				}
				else {
					messageInfo.currentAddress = currentAddress[currentCore];
				}

				messageInfo.time = lastTime[currentCore];

				if (messageInfo.processITCPrintData(itcPrint) == false) {
					*msgInfo = &messageInfo;
				}
			}

			status = TraceDqr::DQERR_OK;
			return status;
		case TRACE_STATE_GETSECONDMSG:
//			printf("TRACE_STATE_GETSECONDMSG %08x\n",lastFaddr[currentCore]);
			// only message with i-cnt/hist/taken/notTaken will release from this state

			// return any message without an i-cnt (or hist, taken/not taken)

			// do not return message with i-cnt/hist/taken/not taken; process them when counts expires

			if ((endMessageNum != 0) && (nm.msgNum > endMessageNum)) {
				printf("stopping at trace message %d\n",endMessageNum);

				state[currentCore] = TRACE_STATE_DONE;
				status = TraceDqr::DQERR_DONE;

				return status;
			}

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

				state[currentCore] = TRACE_STATE_GETNEXTINSTRUCTION;
				break;
			case TraceDqr::TCODE_INCIRCUITTRACE:
			case TraceDqr::TCODE_INCIRCUITTRACE_WS:
				// this message has no count info, but does have a u-addr
				// another message. Doesn't count as a second message, so just return it. In fact,
				// ICT PC Sampling traces should never exit this state

				rc = processTraceMessage(nm,currentAddress[currentCore],lastFaddr[currentCore],lastTime[currentCore]);
				if (rc != TraceDqr::DQERR_OK) {
					printf("Error: NextInstruction(): state TRACE_STATE_GETSECONDMSG: processTraceMessage()\n");

					status = TraceDqr::DQERR_ERR;
					state[currentCore] = TRACE_STATE_ERROR;

					return status;
				}

				// only fill out instinfo/srcinfo if we have the correct reason field (wachpoint or pc_sample)

				TraceDqr::ICTReason isr;

				isr = nm.getICTReason();
				if ((isr == TraceDqr::ICT_WATCHPOINT) || (isr == TraceDqr::ICT_PC_SAMPLE)) {
					if ((instInfo != nullptr) || (srcInfo != nullptr)) {
						Disassemble(lastFaddr[currentCore]);

						if (instInfo != nullptr) {

							instructionInfo.qDepth = 0;
							instructionInfo.arithInProcess = 0;
							instructionInfo.loadInProcess = 0;
							instructionInfo.storeInProcess = 0;

							instructionInfo.coreId = currentCore;
							*instInfo = &instructionInfo;
							(*instInfo)->CRFlag = (crFlag | enterISR[currentCore]);
							enterISR[currentCore] = TraceDqr::isNone;
							(*instInfo)->brFlags = brFlags;

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

					// use lastFaddr[] and not currentAddress[] for currentAddress

					messageInfo.currentAddress = lastFaddr[currentCore];

					if (messageInfo.processITCPrintData(itcPrint) == false) {
						*msgInfo = &messageInfo;
					}
				}

				readNewTraceMessage = true;

				return status;
			case TraceDqr::TCODE_AUXACCESS_WRITE:
			case TraceDqr::TCODE_OWNERSHIP_TRACE:
			case TraceDqr::TCODE_ERROR:
			case TraceDqr::TCODE_DATA_ACQUISITION:
				// these message have no address or count info, so we still need to get
				// another message.

				// might want to keep track of process, but will add that later

				// for now, return message;

				if (nm.haveTimestamp) {
					lastTime[currentCore] = adjustTsForWrap(TraceDqr::TS_rel,lastTime[currentCore],nm.timestamp);
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
				printf("Error: bad tcode type in state TRACE_STATE_GETSECONDMSG. TCODE (%d)\n",nm.tcode);

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
			case TraceDqr::TCODE_INCIRCUITTRACE:
			case TraceDqr::TCODE_INCIRCUITTRACE_WS:
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

					if ((nm.tcode == TraceDqr::TCODE_INCIRCUITTRACE) || (nm.tcode == TraceDqr::TCODE_INCIRCUITTRACE_WS)) {
						messageInfo.currentAddress = lastFaddr[currentCore];
					}
					else {
						messageInfo.currentAddress = currentAddress[currentCore];
					}

					if (messageInfo.processITCPrintData(itcPrint) == false) {
						*msgInfo = &messageInfo;
					}
				}

				if ((srcInfo != nullptr) && (*srcInfo == nullptr)) {
					if ((nm.tcode == TraceDqr::TCODE_INCIRCUITTRACE) || (nm.tcode == TraceDqr::TCODE_INCIRCUITTRACE_WS)) {
						Disassemble(lastFaddr[currentCore]);
					}
					else {
						Disassemble(currentAddress[currentCore]);
					}

					sourceInfo.coreId = currentCore;
					*srcInfo = &sourceInfo;
				}

				TraceDqr::BType b_type;
				b_type = TraceDqr::BTYPE_UNDEFINED;

				switch (nm.tcode) {
				case TraceDqr::TCODE_SYNC:
				case TraceDqr::TCODE_DIRECT_BRANCH_WS:
				case TraceDqr::TCODE_INCIRCUITTRACE_WS:
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
				case TraceDqr::TCODE_INCIRCUITTRACE:
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
			case TraceDqr::TCODE_CORRELATION:
				// correlation has i_cnt, but no address info

				if (nm.haveTimestamp) {
					lastTime[currentCore] = adjustTsForWrap(TraceDqr::TS_rel,lastTime[currentCore],nm.timestamp);
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
			case TraceDqr::TCODE_AUXACCESS_WRITE:
			case TraceDqr::TCODE_OWNERSHIP_TRACE:
			case TraceDqr::TCODE_ERROR:
				// these messages have no address or i-cnt info and should have been
				// instantly retired when they were read.


				state[currentCore] = TRACE_STATE_ERROR;
				status = TraceDqr::DQERR_ERR;

				return status;
			default:
				printf("Error: bad tcode type in sate TRACE_STATE_RETIREMESSAGE\n");

				state[currentCore] = TRACE_STATE_ERROR;
				status = TraceDqr::DQERR_ERR;

				return status;
			}

			status = TraceDqr::DQERR_OK;
			return status;
		case TRACE_STATE_GETNEXTMSG:
//			printf("TRACE_STATE_GETNEXTMSG\n");

			// exit this state when message with i-cnt, history, taken, or not-taken is read

			if ((endMessageNum != 0) && (nm.msgNum > endMessageNum)) {
				printf("stopping at trace message %d\n",endMessageNum);

				state[currentCore] = TRACE_STATE_DONE;
				status = TraceDqr::DQERR_DONE;

				return status;
			}

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
			case TraceDqr::TCODE_INCIRCUITTRACE:
			case TraceDqr::TCODE_INCIRCUITTRACE_WS:
				rc = counts->setCounts(&nm);
				if (rc != TraceDqr::DQERR_OK) {
					printf("Error: nextInstruction: state TRACE_STATE_GETNEXTMESSAGE Count::seteCounts()\n");

					state[currentCore] = TRACE_STATE_ERROR;

					status = rc;

					return status;
				}

				state[currentCore] = TRACE_STATE_GETNEXTINSTRUCTION;
				break;
			case TraceDqr::TCODE_AUXACCESS_WRITE:
			case TraceDqr::TCODE_DATA_ACQUISITION:
			case TraceDqr::TCODE_OWNERSHIP_TRACE:
			case TraceDqr::TCODE_ERROR:
				// retire these instantly by returning them through msgInfo

				if (nm.haveTimestamp) {
					lastTime[currentCore] = adjustTsForWrap(TraceDqr::TS_rel,lastTime[currentCore],nm.timestamp);
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
					printf("NextInstruction: counts are exhausted\n");
				}

				state[currentCore] = TRACE_STATE_RETIREMESSAGE;
				break;
			}

//			printf("state TRACE_STATE_GETNEXTINSTRUCTION\n");

			// Should first process addr, and then compute next addr!!! If can't compute next addr, it is an error.
			// Should always be able to process instruction at addr and compute next addr when we get here.
			// After processing next addr, if there are no more counts, retire trace message and get another

			TraceDqr::ADDRESS addr;

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
					break;
				}
				else if (counts->getCurrentCountType(currentCore) != TraceDqr::COUNTTYPE_none) {
					// error
					// must have a JR/JALR or exception/exception return to get here, and the CR stack is empty

					printf("Error: getCurrentCountType(core:%d) still has counts; call/return stack is empty; have countType: %d\n",currentCore,counts->getCurrentCountType(currentCore));

					char d[64];

					instructionInfo.instructionToText(d,sizeof d,2);
					printf("%08llx:    %s\n",currentAddress[currentCore],d);

					state[currentCore] = TRACE_STATE_ERROR;

					status = TraceDqr::DQERR_ERR;

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
					(*instInfo)->timestamp = pipeCycles;
					(*instInfo)->pipeCycles = eCycleCount[currentCore];
					(*instInfo)->VIStartCycles = viStartCycles - prevCycle;
					(*instInfo)->VIFinishCycles = viFinishCycles - prevCycle;

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
