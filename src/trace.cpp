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

// class trace methods

int Trace::decodeInstructionSize(uint32_t inst, int &inst_size)
{
  return disassembler->decodeInstructionSize(inst,inst_size);
}

int Trace::decodeInstruction(uint32_t instruction,int &inst_size,TraceDqr::InstType &inst_type,TraceDqr::Reg &rs1,TraceDqr::Reg &rd,int32_t &immediate,bool &is_branch)
{
	return disassembler->decodeInstruction(instruction,inst_size,inst_type,rs1,rd,immediate,is_branch);
}

Trace::Trace(char *tf_name,bool binaryFlag,char *ef_name,int numAddrBits,uint32_t addrDispFlags,int srcBits,uint32_t freq)
{
  sfp          = nullptr;
  elfReader    = nullptr;
  symtab       = nullptr;
  disassembler = nullptr;

  assert(tf_name != nullptr);

  itcPrint = nullptr;

  srcbits = srcBits;

  analytics.setSrcBits(srcBits);

  sfp = new (std::nothrow) SliceFileParser(tf_name,binaryFlag,srcbits);

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

	disassembler = new (std::nothrow) Disassembler(abfd);

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

  startMessageNum  = 0;
  endMessageNum    = 0;

  for (int i = 0; (size_t)i < (sizeof messageSync / sizeof messageSync[0]); i++) {
	  messageSync[i] = nullptr;
  }

  instructionInfo.address = 0;
  instructionInfo.instruction = 0;
  instructionInfo.instSize = 0;

  if (numAddrBits != 0 ) {
	  instructionInfo.addrSize = numAddrBits;
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

  sourceInfo.sourceFile = nullptr;
  sourceInfo.sourceFunction = nullptr;
  sourceInfo.sourceLineNum = 0;
  sourceInfo.sourceLine = nullptr;

  NexusMessage::targetFrequency = freq;

  status = TraceDqr::DQERR_OK;
}

Trace::~Trace()
{
	if (sfp != nullptr) {
		delete sfp;
		sfp = nullptr;
	}

	if (elfReader != nullptr) {
		delete elfReader;
		elfReader = nullptr;
	}

	if (itcPrint  != nullptr) {
		delete itcPrint;
		itcPrint = nullptr;
	}

	if (counts != nullptr) {
		delete [] counts;
		counts = nullptr;
	}
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

	for (int i = 0; (size_t)i < sizeof state / sizeof state[0]; i++) {
		state[i] = TRACE_STATE_GETSTARTTRACEMSG;
	}

	return TraceDqr::DQERR_OK;
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

TraceDqr::DQErr Trace::nextAddr(int core,TraceDqr::ADDRESS addr,TraceDqr::ADDRESS &pc)
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

	printf("Trace::nextAddr(core=%d,addr=%08x,pc=%08x)\n",core,addr,pc);

	status = elfReader->getInstructionByAddress(addr,inst);
	if (status != TraceDqr::DQERR_OK) {
		printf("Error: nextAddr(): getInstructionByAddress() failed\n");

		printf("Addr: %08x\n",addr);

		return status;
	}

	// figure out how big the instruction is
	// Note: immediate will already be adjusted - don't need to mult by 2 before adding to address

	rc = decodeInstruction(inst,inst_size,inst_type,rs1,rd,immediate,isBranch);
	if (rc != 0) {
		printf("Error: nextAddr(): Cannot decode instruction %04x\n",inst);

		status = TraceDqr::DQERR_ERR;

		return status;
	}

	printf("nextAddr(): addr:%08x inst:%08x size:%d type:%d\n",addr,inst,inst_size,inst_type);

	switch (inst_type) {
	case TraceDqr::INST_UNKNOWN:
		printf("nextAddr(): INST_UNKNOWN\n");

		pc = addr + inst_size/8;
		break;
	case TraceDqr::INST_JAL:
		printf("nextAddr(): INST_JAL\n");

		// rd = pc+4 (rd can be r0)
		// pc = pc + (sign extended immediate offset)
		// plan unconditional jumps use rd -> r0
		// inferrable unconditional

		if ((rd == TraceDqr::REG_1) || (rd == TraceDqr::REG_5)) {
			counts->push(core,addr + inst_size/8);
		}

		pc = addr + immediate;
		break;
	case TraceDqr::INST_JALR:
		printf("nextAddr(): INST_JALR\n");

		// rd = pc+4 (rd can be r0)
		// pc = pc + ((sign extended immediate offset) + rs) & 0xffe
		// plain unconditional jumps use rd -> r0
		// not inferrable unconditional

		if ((rd == TraceDqr::REG_1) || (rd == TraceDqr::REG_5)) {
			if ((rs1 != TraceDqr::REG_1) && (rs1 != TraceDqr::REG_5)) {
				counts->push(core,addr+inst_size/8);
				pc = -1;
			}
			else if (rd != rs1) {
				pc = counts->pop(core);
				counts->push(core,addr+inst_size/8);
			}
			else {
				counts->push(core,addr+inst_size/8);
				pc = -1;
			}
		}
		else if ((rs1 == TraceDqr::REG_1) || (rs1 == TraceDqr::REG_5)) {
			pc = counts->pop(core);
		}
		else {
			pc = -1;
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
		printf("nextAddr(): INST_BRANCH or INST_C_BRANCH\n");

		// pc = pc + (sign extend immediate offset) (BLTU and BGEU are not sign extended)
		// inferrable conditional

		ct = counts->getCurrentCountType(core);
		switch (ct) {
		case TraceDqr::COUNTTYPE_none:
			printf("Error: nextAddr(): instruction counts consumed\n");

			return TraceDqr::DQERR_ERR;
		case TraceDqr::COUNTTYPE_i_cnt:
			// don't know if the branch is taken or not, so we don't know the next addr
			pc = -1;
			break;
		case TraceDqr::COUNTTYPE_history:
			//consume history bit here and set pc accordingly

			rc = counts->consumeHistory(core,isTaken);
			if ( rc != 0) {
				printf("Error: nextAddr(): consumeHistory() failed\n");

				status = TraceDqr::DQERR_ERR;

				return status;
			}

			if (isTaken) {
				pc = addr + immediate;
			}
			else {
				pc = addr + inst_size / 8;
			}
			break;
		case TraceDqr::COUNTTYPE_taken:
			rc = counts->consumeTakenCount(core);
			if ( rc != 0) {
				printf("Error: nextAddr(): consumeTakenCount() failed\n");

				status = TraceDqr::DQERR_ERR;

				return status;
			}

			pc = addr + immediate;
			break;
		case TraceDqr::COUNTTYPE_notTaken:
			rc = counts->consumeNotTakenCount(core);
			if ( rc != 0) {
				printf("Error: nextAddr(): consumeTakenCount() failed\n");

				status = TraceDqr::DQERR_ERR;

				return status;
			}

			pc = addr + inst_size / 8;
			break;
		}
		break;
	case TraceDqr::INST_C_J:
		printf("nextAddr(): INST_BRANCH or INST_C_J\n");

		// pc = pc + (signed extended immediate offset)
		// inferrable unconditional

		pc = addr + immediate;
		break;
	case TraceDqr::INST_C_JAL:
		printf("nextAddr(): INST_BRANCH or INST_C_JAL\n");

		// x1 = pc + 2
		// pc = pc + (signed extended immediate offset)
		// inferrable unconditional

		counts->push(core,addr + inst_size/8);
		pc = addr + immediate;
		break;
	case TraceDqr::INST_C_JR:
		printf("nextAddr(): INST_BRANCH or INST_C_JR\n");

		// pc = pc + rs1
		// not inferrable unconditional

		if ((rs1 == TraceDqr::REG_1) || (rs1 == TraceDqr::REG_5)) {
			pc = counts->pop(core);
		}
		else {
			pc = -1;
		}
		break;
	case TraceDqr::INST_C_JALR:
		printf("nextAddr(): INST_BRANCH or INST_C_JALR\n");

		// x1 = pc + 2
		// pc = pc + rs1
		// not inferrble unconditional

		if (rs1 == TraceDqr::REG_5) {
			pc = counts->pop(core);
			counts->push(core,addr+inst_size/8);
		}
		else {
			counts->push(core,addr+inst_size/8);
			pc = -1;
		}
		break;
	default:
		pc = addr + inst_size / 8;
		break;
	}

	// Always consume i-cnt

	rc = counts->consumeICnt(core,inst_size/16);
	if ( rc != 0) {
		printf("Error: nextAddr(): consumeICnt() failed\n");

		status = TraceDqr::DQERR_ERR;

		return status;
	}

	printf("nextAddr() -> %08x\n",pc);

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
			ts = ts ^ nm.timestamp;
		}
		break;
	case TraceDqr::TCODE_INDIRECT_BRANCH:
		if (nm.haveTimestamp) {
			ts = ts ^ nm.timestamp;
		}
		faddr = faddr ^ (nm.indirectBranch.u_addr << 1);
		pc = faddr;
		break;
	case TraceDqr::TCODE_SYNC:
		if (nm.haveTimestamp) {
			ts = nm.timestamp;
		}
		faddr = nm.sync.f_addr << 1;
		pc = faddr;
		break;
	case TraceDqr::TCODE_DIRECT_BRANCH_WS:
		if (nm.haveTimestamp) {
			ts = nm.timestamp;
		}
		faddr = nm.directBranchWS.f_addr << 1;
		pc = faddr;
		break;
	case TraceDqr::TCODE_INDIRECT_BRANCH_WS:
		if (nm.haveTimestamp) {
			ts = nm.timestamp;
		}
		faddr = nm.indirectBranchWS.f_addr << 1;
		pc = faddr;
		break;
	case TraceDqr::TCODE_INDIRECTBRANCHHISTORY:
		if (nm.haveTimestamp) {
			ts = ts ^ nm.timestamp;
		}
		faddr = faddr ^ (nm.indirectHistory.u_addr << 1);
		pc = faddr;
		break;
	case TraceDqr::TCODE_INDIRECTBRANCHHISTORY_WS:
		if (nm.haveTimestamp) {
			ts = nm.timestamp;
		}
		faddr = nm.indirectHistoryWS.f_addr << 1;
		pc = faddr;
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
	case TraceDqr::TCODE_INCIRCUITTRACE:
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
	TraceDqr::ADDRESS addr;

//	printf("Instinfo: %08llx, MsgInfo: %08x, srcInfo: %08x\n",instInfo,msgInfo,srcInfo);

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

		if (readNewTraceMessage != false) {
//			rc = linkedNexusMessage::nextTraceMessage(nm);
			rc = sfp->readNextTraceMsg(nm,analytics);

			if (rc != TraceDqr::DQERR_OK) {
				// have an error. either eof, or error

				status = sfp->getErr();

				if (status != TraceDqr::DQERR_EOF) {
					if ( messageSync[currentCore] != nullptr) {
						printf("Error: Trace file does not contain %d trace messages. %d message found\n",startMessageNum,messageSync[currentCore]->lastMsgNum);
					}
					else {
						printf("Error: Trace file does not contain any trace messages, or is unreadable\n");
					}
				}

				return status;
			}

			readNewTraceMessage = false;
			currentCore = nm.coreId;
		}

		switch (state[currentCore]) {
		case TRACE_STATE_GETSTARTTRACEMSG:
			printf("state TRACE_STATE_GETSTARTTRACEMSG\n");

			// home new nexus message to process

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
				if (messageSync[currentCore]->index == 0) {
					if (nm.msgNum >= startMessageNum) {
						// can't start at this trace message because we have not seen a sync yet
						// so we cannot compute the address

						state[currentCore] = TRACE_STATE_ERROR;

						printf("Error: cannot start at trace message %d because no preceeding sync\n",startMessageNum);

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
			printf("state TRACE_STATE_COMPUTESTARTINGADDRESS\n");

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
				lastTime[currentCore] = messageSync[currentCore]->msgs[0].timestamp;
			}

			// thow away all counts from the first trace message!! then use them starting with the second

			// start at one because we have already consumed 0 (its faddr and timestamp are all we
			// need).

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
					if (addr == (TraceDqr::ADDRESS)-1) {
						printf("Error: NextInstruction(): state TRACE_STATE_COMPUTESTARTINGADDRESS: can't compute address\n");

						status = TraceDqr::DQERR_ERR;

						state[currentCore] = TRACE_STATE_ERROR;

						return status;
					}

					rc = nextAddr(currentCore,currentPc,newPc);
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
			printf("state TRACE_STATE_GETFIRSTSYNCMSG\n");

			// read trace messages until a sync is found. Should be the first message normally

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
				messageInfo.currentAddress = currentAddress[currentCore];
				messageInfo.time = lastTime[currentCore];

				if (messageInfo.processITCPrintData(itcPrint) == false) {
					*msgInfo = &messageInfo;
				}
			}

			status = TraceDqr::DQERR_OK;
			return status;
		case TRACE_STATE_GETSECONDMSG:
			printf("state TRACE_STATE_GETSECONDMSG\n");

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

				rc = counts->setCounts(&nm);
				if (rc != TraceDqr::DQERR_OK) {
					state[currentCore] = TRACE_STATE_ERROR;
					status = rc;

					return status;
				}

				state[currentCore] = TRACE_STATE_GETNEXTINSTRUCTION;
				break;
			case TraceDqr::TCODE_AUXACCESS_WRITE:
			case TraceDqr::TCODE_OWNERSHIP_TRACE:
			case TraceDqr::TCODE_ERROR:
				// these message have no address or count info, so we still need to get
				// another message.

				// might want to keep track of process, but will add that later

				// for now, return message;

				if (nm.haveTimestamp) {
					lastTime[currentCore] = lastTime[currentCore] ^ nm.timestamp;
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
				printf("Error: bad tcode type in sate TRACE_STATE_GETNEXTMSG.TCODE_DIRECT_BRANCH\n");

				state[currentCore] = TRACE_STATE_ERROR;
				status = TraceDqr::DQERR_ERR;
				return status;
			}
			break;
		case TRACE_STATE_RETIREMESSAGE:
			printf("state TRACE_STATE_RETIREMESSAGE\n");

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

				readNewTraceMessage = true;
				state[currentCore] = TRACE_STATE_GETNEXTMSG;
				break;
			case TraceDqr::TCODE_CORRELATION:
				// correlation has i_cnt, but no address info

				if (nm.haveTimestamp) {
					lastTime[currentCore] = lastTime[currentCore] ^ nm.timestamp;
				}

				if (msgInfo != nullptr) {
					messageInfo = nm;
					messageInfo.time = lastTime[currentCore];

					// leaving trace mode - addr should be last faddr + i_cnt *2

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
				printf("Error: bad tcode type in sate TRACE_STATE_GETNEXTMSG\n");

				state[currentCore] = TRACE_STATE_ERROR;
				status = TraceDqr::DQERR_ERR;
				return status;
			}

			status = TraceDqr::DQERR_OK;
			return status;
		case TRACE_STATE_GETNEXTMSG:
			printf("state TRACE_STATE_GETNEXTMSG\n");

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
					lastTime[currentCore] = lastTime[currentCore] ^ nm.timestamp;
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
			printf("Trace::NextInstruction():TRACE_STATE_GETNEXTINSTRUCTION\n");

			// NOte! should first process addr, and then compute next addr!!! If can't compute next addr,
			// retire the message!!

			addr = currentAddress[currentCore];

			uint32_t inst;
			int inst_size;

			status = elfReader->getInstructionByAddress(addr,inst);
			if (status != TraceDqr::DQERR_OK) {
				printf("Error: getInstructionByAddress failed\n");

				printf("Addr2: %08x\n",addr);

				state[currentCore] = TRACE_STATE_ERROR;

				return status;
			}

			// figure out how big the instruction is

			int rc;

			// what does this state do?
			//	- adjust currentAddr to next instruction, taking jumps, etc
			//	- update analytics
			//	- set next state if counts expire
			//	- return instInfo, srcInfo

			rc = decodeInstructionSize(inst,inst_size);
			if (rc != 0) {
				printf("Error: Cann't decode size of instruction %04x\n",inst);

				state[currentCore] = TRACE_STATE_ERROR;
				status = TraceDqr::DQERR_ERR;
				return status;
			}

			status = analytics.updateInstructionInfo(currentCore,inst,inst_size);
			if (status != TraceDqr::DQERR_OK) {
				state[currentCore] = TRACE_STATE_ERROR;
				return status;
			}

			Disassemble(addr);

			if (instInfo != nullptr) {
				instructionInfo.coreId = currentCore;
				*instInfo = &instructionInfo;
			}

			if (srcInfo != nullptr) {
				sourceInfo.coreId = currentCore;
				*srcInfo = &sourceInfo;
			}

			// compute next address (retire this instruction)

			rc = nextAddr(currentCore,currentAddress[currentCore],addr);
			if (addr == (TraceDqr::ADDRESS)-1) {
				if (counts->getCurrentCountType(currentCore) == TraceDqr::COUNTTYPE_none) {
					// counts have expired. Retire this message and read next trace message and update

					state[currentCore] = TRACE_STATE_RETIREMESSAGE;
				}
				else {
					// something went wrong! This should never happen!

					printf("Error: nextInstructcion(): nextAddr() returned error\n");

					state[currentCore] = TRACE_STATE_ERROR;
					status = TraceDqr::DQERR_ERR;
					return status;
				}
			}
			else if (counts->getCurrentCountType(currentCore) == TraceDqr::COUNTTYPE_none) {
				// counts have expired. Retire this message and read next trace message and update

				state[currentCore] = TRACE_STATE_RETIREMESSAGE;
			}
			else {
				currentAddress[currentCore] = addr;
			}


			status = TraceDqr::DQERR_OK;
			return status;
		case TRACE_STATE_DONE:
//			printf("Trace::NextInstruction():TRACE_STATE_DONE\n");
			status = TraceDqr::DQERR_DONE;
			return status;
		case TRACE_STATE_ERROR:
//			printf("Trace::NextInstruction():TRACE_STATE_ERROR\n");

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
