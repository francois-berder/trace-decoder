/*
 * Copyright 2019 SiFive, Inc.
 *
 * trace.hpp
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

#ifndef TRACE_HPP_
#define TRACE_HPP_

// if config.h is not present, uncomment the lines below

//#define PACKAGE 1
//#define PACKAGE_VERSION 1

// private definitions

#include "config.h"
#include "bfd.h"
#include "dis-asm.h"

#include <iostream>
#include <iomanip>
#include <fstream>
#include <cstring>
#include <cstdint>
#include <cassert>

#ifdef DO_TIMES
class Timer {
public:
	Timer();
	~Timer();

	double start();
	double etime();

private:
	double startTime;
};
#endif // DO_TIMES

class cachedInstInfo {
public:
	cachedInstInfo(const char *file,const char *func,int linenum,const char *lineTxt,const char *instText,TraceDqr::RV_INST inst,int instSize,const char *addresslabel,int addresslabeloffset,bool haveoperandaddress,TraceDqr::ADDRESS operandaddress,const char *operandlabel,int operandlabeloffset);
	~cachedInstInfo();

	void dump();

	const char *filename;
	const char *functionname;
	int linenumber;
	const char *lineptr;

	TraceDqr::RV_INST instruction;
	int               instsize;

	char             *instructionText;

	const char       *addressLabel;
	int               addressLabelOffset;
	bool              haveOperandAddress;
	TraceDqr::ADDRESS operandAddress;
	const char       *operandLabel;
	int               operandLabelOffset;
};

// class section: work with elf file sections using libbfd

class section {
public:
	section();
	~section();

	section *initSection(section **head,asection *newsp,bool enableInstCaching);
	section *getSectionByAddress(TraceDqr::ADDRESS addr);

	cachedInstInfo *setCachedInfo(TraceDqr::ADDRESS addr,const char *file,const char *func,int linenum,const char *lineTxt,const char *instTxt,TraceDqr::RV_INST inst,int instSize,const char *addresslabel,int addresslabeloffset,bool haveoperandaddress,TraceDqr::ADDRESS operandaddress,const char *operandlabel,int operandlabeloffset);
	cachedInstInfo *getCachedInfo(TraceDqr::ADDRESS addr);

	section     *next;
	bfd         *abfd;
	TraceDqr::ADDRESS startAddr;
	TraceDqr::ADDRESS endAddr;
	int          size;
	asection    *asecptr;
	uint16_t    *code;
	cachedInstInfo **cachedInfo;
};

// class fileReader: Helper class to handler list of source code files

class fileReader {
public:
	struct funcList {
		funcList *next;
		char *func;
	};
	struct fileList {
		fileList     *next;
		char         *name;
		funcList     *funcs;
		unsigned int  lineCount;
		char        **lines;
	};

	fileReader(/*paths?*/);
	~fileReader();

	fileList *findFile(const char *file);
private:
	fileList *readFile(const char *file);

	fileList *lastFile;
	fileList *files;
};

// class Symtab: Interface class between bfd symbols and what is needed for dqr

class Symtab {
public:
	             Symtab(bfd *abfd);
	            ~Symtab();
	const char  *getSymbolByAddress(TraceDqr::ADDRESS addr);
	const char  *getNextSymbolByAddress();
//	dqr::ADDRESS getSymbolByName();
	asymbol    **getSymbolTable() { return symbol_table; }
	void         dump();

private:
	bfd      *abfd;
	long      number_of_symbols;
    asymbol **symbol_table;

    TraceDqr::ADDRESS vma;
    int          index;
};

// Class ElfReader: Interface class between dqr and bfd

class ElfReader {
public:
        	   ElfReader(char *elfname);
	          ~ElfReader();
	TraceDqr::DQErr getStatus() { return status; }
	TraceDqr::DQErr getInstructionByAddress(TraceDqr::ADDRESS addr, TraceDqr::RV_INST &inst);
	Symtab    *getSymtab();
	bfd       *get_bfd() {return abfd;}
	int        getArchSize() { return archSize; }
	int        getBitsPerAddress() { return bitsPerAddress; }

private:
	static bool init;
	TraceDqr::DQErr  status;
	bfd        *abfd;
	int         archSize;
	int	        bitsPerWord;
	int         bitsPerAddress;
	section	   *codeSectionLst;
	Symtab     *symtab;
};

class TsList {
public:
	TsList();
	~TsList();

	class TsList *prev;
	class TsList *next;
	bool terminated;
	TraceDqr::TIMESTAMP startTime;
	TraceDqr::TIMESTAMP endTime;
	char *message;
};

class ITCPrint {
public:
	ITCPrint(int numCores,int buffSize,int channel);
	~ITCPrint();
	bool print(uint8_t core, uint32_t address, uint32_t data);
	bool print(uint8_t core, uint32_t address, uint32_t data, TraceDqr::TIMESTAMP tstamp);
	void haveITCPrintData(int numMsgs[DQR_MAXCORES], bool havePrintData[DQR_MAXCORES]);
	bool getITCPrintMsg(uint8_t core, char *dst, int dstLen, TraceDqr::TIMESTAMP &startTime, TraceDqr::TIMESTAMP &endTime);
	bool flushITCPrintMsg(uint8_t core, char *dst, int dstLen, TraceDqr::TIMESTAMP &startTime, TraceDqr::TIMESTAMP &endTime);
	bool getITCPrintStr(uint8_t core, std::string &s, TraceDqr::TIMESTAMP &startTime, TraceDqr::TIMESTAMP &endTime);
	bool flushITCPrintStr(uint8_t core, std::string &s, TraceDqr::TIMESTAMP &starTime, TraceDqr::TIMESTAMP &endTime);

private:
	int  roomInITCPrintQ(uint8_t core);
	TsList *consumeTerminatedTsList(int core);
	TsList *consumeOldestTsList(int core);

	int numCores;
	int buffSize;
	int printChannel;
	char **pbuff;
	int *pbi;
	int *pbo;
	int *numMsgs;
	class TsList **tsList;
	class TsList *freeList;
};

// class SliceFileParser: Class to parse binary or ascii nexus messages into a NexusMessage object
class SliceFileParser {
public:
             SliceFileParser(char *filename,int srcBits);
             ~SliceFileParser();
  TraceDqr::DQErr readNextTraceMsg(NexusMessage &nm,class Analytics &analytics);
  TraceDqr::DQErr getFileOffset(int &size,int &offset);

  TraceDqr::DQErr getErr() { return status; };
  void       dump();

private:
  TraceDqr::DQErr status;

  // add other counts for each message type

  int           srcbits;
  std::ifstream tf;
  int           tfSize;
  int           bitIndex;
  int           msgSlices;
  uint32_t      msgOffset;
  uint8_t       msg[64];
  bool          eom = false;

  TraceDqr::DQErr readBinaryMsg();
  TraceDqr::DQErr readNextByte(uint8_t *byte);
  TraceDqr::DQErr parseVarField(uint64_t *val,int *width);
  TraceDqr::DQErr parseFixedField(int width, uint64_t *val);
  TraceDqr::DQErr parseDirectBranch(NexusMessage &nm,Analytics &analytics);
  TraceDqr::DQErr parseIndirectBranch(NexusMessage &nm,Analytics &analytics);
  TraceDqr::DQErr parseDirectBranchWS(NexusMessage &nm,Analytics &analytics);
  TraceDqr::DQErr parseIndirectBranchWS(NexusMessage &nm,Analytics &analytics);
  TraceDqr::DQErr parseSync(NexusMessage &nm,Analytics &analytics);
  TraceDqr::DQErr parseCorrelation(NexusMessage &nm,Analytics &analytics);
  TraceDqr::DQErr parseAuxAccessWrite(NexusMessage &nm,Analytics &analytics);
  TraceDqr::DQErr parseDataAcquisition(NexusMessage &nm,Analytics &analytics);
  TraceDqr::DQErr parseOwnershipTrace(NexusMessage &nm,Analytics &analytics);
  TraceDqr::DQErr parseError(NexusMessage &nm,Analytics &analytics);
  TraceDqr::DQErr parseIndirectHistory(NexusMessage &nm,Analytics &analytics);
  TraceDqr::DQErr parseIndirectHistoryWS(NexusMessage &nm,Analytics &analytics);
  TraceDqr::DQErr parseResourceFull(NexusMessage &nm,Analytics &analytics);
  TraceDqr::DQErr parseICT(NexusMessage &nm,Analytics &analytics);
  TraceDqr::DQErr parseICTWS(NexusMessage &nm,Analytics &analytics);
};

// class Disassembler: class to help in the dissasemblhy of instrucitons

class Disassembler {
public:
	      Disassembler(bfd *abfd,bool labelsAreFunctionsls);
	      ~Disassembler();
	int   Disassemble(TraceDqr::ADDRESS addr);

	int   getSrcLines(TraceDqr::ADDRESS addr, const char **filename, const char **functionname, unsigned int *linenumber, const char **line);

	static int   decodeInstructionSize(uint32_t inst, int &inst_size);
	static int   decodeInstruction(uint32_t instruction,int archSize,int &inst_size,TraceDqr::InstType &inst_type,TraceDqr::Reg &rs1,TraceDqr::Reg &rd,int32_t &immediate,bool &is_branch);

	void  overridePrintAddress(bfd_vma addr, struct disassemble_info *info); // hmm.. don't need info - part of object!
	void  getAddressSyms(bfd_vma vma);
	void  clearOperandAddress();

	Instruction getInstructionInfo() { return instruction; }
	Source      getSourceInfo() { return source; }

	TraceDqr::DQErr setPathType(TraceDqr::pathType pt);

	TraceDqr::DQErr getStatus() {return status;}

private:
	typedef struct {
		flagword sym_flags;
		bfd_vma  func_vma;
		int      func_size;
	} func_info_t;

	bfd               *abfd;
	disassembler_ftype disassemble_func;
	TraceDqr::DQErr         status;

	int               archSize;

	bfd_vma           start_address;

	long              number_of_syms;
	asymbol         **symbol_table;
	asymbol         **sorted_syms;

	func_info_t      *func_info;
	disassemble_info *info;
	section	         *codeSectionLst;
	int               prev_index;
	int               cached_sym_index;
	bfd_vma           cached_sym_vma;
	int               cached_sym_size;

	Instruction instruction;
	Source      source;

	class fileReader *fileReader;

	TraceDqr::pathType pType;

	void print_address(bfd_vma vma);
	void print_address_and_instruction(bfd_vma vma);
	void setInstructionAddress(bfd_vma vma);

	int lookup_symbol_by_address(bfd_vma,flagword flags,int *index,int *offset);
	int lookupInstructionByAddress(bfd_vma vma,uint32_t *ins,int *ins_size);
//	int get_ins(bfd_vma vma,uint32_t *ins,int *ins_size);

	// need to make all the decode function static. Might need to move them to public?

	static int decodeRV32Q0Instruction(uint32_t instruction,int &inst_size,TraceDqr::InstType &inst_type,TraceDqr::Reg &rs1,TraceDqr::Reg &rd,int32_t &immediate,bool &is_branch);
	static int decodeRV32Q1Instruction(uint32_t instruction,int &inst_size,TraceDqr::InstType &inst_type,TraceDqr::Reg &rs1,TraceDqr::Reg &rd,int32_t &immediate,bool &is_branch);
	static int decodeRV32Q2Instruction(uint32_t instruction,int &inst_size,TraceDqr::InstType &inst_type,TraceDqr::Reg &rs1,TraceDqr::Reg &rd,int32_t &immediate,bool &is_branch);
	static int decodeRV32Instruction(uint32_t instruction,int &inst_size,TraceDqr::InstType &inst_type,TraceDqr::Reg &rs1,TraceDqr::Reg &rd,int32_t &immediate,bool &is_branch);

	static int decodeRV64Q0Instruction(uint32_t instruction,int &inst_size,TraceDqr::InstType &inst_type,TraceDqr::Reg &rs1,TraceDqr::Reg &rd,int32_t &immediate,bool &is_branch);
	static int decodeRV64Q1Instruction(uint32_t instruction,int &inst_size,TraceDqr::InstType &inst_type,TraceDqr::Reg &rs1,TraceDqr::Reg &rd,int32_t &immediate,bool &is_branch);
	static int decodeRV64Q2Instruction(uint32_t instruction,int &inst_size,TraceDqr::InstType &inst_type,TraceDqr::Reg &rs1,TraceDqr::Reg &rd,int32_t &immediate,bool &is_branch);
	static int decodeRV64Instruction(uint32_t instruction,int &inst_size,TraceDqr::InstType &inst_type,TraceDqr::Reg &rs1,TraceDqr::Reg &rd,int32_t &immediate,bool &is_branch);
};

class AddrStack {
public:
	AddrStack(int size = 2048);
	~AddrStack();
	void reset();
	int push(TraceDqr::ADDRESS addr);
	TraceDqr::ADDRESS pop();

private:
	int stackSize;
	int sp;
	TraceDqr::ADDRESS *stack;
};

class NexusMessageSync {
public:
	NexusMessageSync();
	int          firstMsgNum;
	int          lastMsgNum;
	int          index;
	NexusMessage msgs[512];
};

class Count {
public:
	Count();
	~Count();

	void resetCounts(int core);

	TraceDqr::CountType getCurrentCountType(int core);
	TraceDqr::DQErr setICnt(int core,int count);
	TraceDqr::DQErr setHistory(int core,uint64_t hist);
	TraceDqr::DQErr setHistory(int core,uint64_t hist,int count);
	TraceDqr::DQErr setTakenCount(int core,int takenCnt);
	TraceDqr::DQErr setNotTakenCount(int core,int notTakenCnt);
	TraceDqr::DQErr setCounts(NexusMessage *nm);
	int consumeICnt(int core,int numToConsume);
	int consumeHistory(int core,bool &taken);
	int consumeTakenCount(int core);
	int consumeNotTakenCount(int core);

	int getICnt(int core);

	int push(int core,TraceDqr::ADDRESS addr) { return stack[core].push(addr); }
	TraceDqr::ADDRESS pop(int core) { return stack[core].pop(); }
	void resetStack(int core) { stack[core].reset(); }

	void dumpCounts(int core);

//	int getICnt(int core);
//	int adjustICnt(int core,int delta);
//	bool isHistory(int core);
//	bool takenHistory(int core);

private:
	int i_cnt[DQR_MAXCORES];
    uint64_t history[DQR_MAXCORES];
    int histBit[DQR_MAXCORES];
    int takenCount[DQR_MAXCORES];
    int notTakenCount[DQR_MAXCORES];
    AddrStack stack[DQR_MAXCORES];
};

#endif /* TRACE_HPP_ */


// Improvements:
//
// Disassembler class:
//  Should be able to creat disassembler object without elf file
//  Should have a diasassemble method that takes an address and an instruciotn, not just an address
//  Should be able us use a block of memory for the code, not from an elf file
//  Use new methods to cleanup verilator nextInstruction()

// move some stuff in instruction object to a separate object pointed to from instruciton object so that coppies
// of the object don't need to copy it all (regfile is an example). Create accessor method to get. Destructor should
// delete all
