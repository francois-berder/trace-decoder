/*
 * Copyright 2019 Sifive, Inc.
 *
 * dqr.cpp
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

//#define DQR_MAXCORES	8

const char * const DQR_VERSION = "0.9.4";

// static C type helper functions

static int atoh(char a)
{
	if (a >= '0' && a <= '9') {
		return a-'0';
	}

	if (a >= 'a' && a <= 'f') {
		return a-'a'+10;
	}

	if (a >= 'A' && a <= 'F') {
		return a-'A'+10;
	}

	return -1;
}

static void override_print_address(bfd_vma addr, struct disassemble_info *info)
{
//	lookup symbol at addr.

  Disassembler *dp;

  // use field in info to point to disassembler object so we can call member funcs

//  printf("addr: %08x\n",addr);

  dp = (Disassembler *)info->application_data;

  assert(dp != nullptr);

  dp->overridePrintAddress(addr,info);
}

static int asymbol_compare_func(const void *arg1,const void *arg2)
{
	assert(arg1 != nullptr);
	assert(arg2 != nullptr);

	asymbol *first;
	asymbol *second;

	first = *(asymbol **)arg1;
	second = *(asymbol **)arg2;

	// note: use bfd_asymbol_value() because first and second symbols may not be in same section

	return bfd_asymbol_value((asymbol*)first) - bfd_asymbol_value((asymbol*)second);
}

// should probably delete this function

__attribute__((unused)) static void dump_syms(asymbol **syms, int num_syms) // @suppress("Unused static function")
{
	assert(syms != nullptr);

    for (int i = 0; i < num_syms; i++) {
    	printf("%d: 0x%llx '%s'",i,bfd_asymbol_value(syms[i]),syms[i]->name);

    	if (syms[i]->flags == 0) {
    		printf(" NOTYPE");
    	}

        if (syms[i]->flags & BSF_LOCAL) {
          printf(" LOCAL");
        }

        if (syms[i]->flags & BSF_GLOBAL) {
          printf(" GLOBAL");
        }

        if (syms[i]->flags & BSF_EXPORT) {
          printf(" EXPORT");
        }

        if (syms[i]->flags & BSF_DEBUGGING) {
          printf(" DEBUGGING");
        }

        if (syms[i]->flags & BSF_FUNCTION) {
          printf(" FUNCTION");
        }

        if (syms[i]->flags & BSF_KEEP) {
          printf(" KEEP");
        }

        if (syms[i]->flags & BSF_ELF_COMMON) {
          printf(" ELF_COMMON");
        }

        if (syms[i]->flags & BSF_WEAK) {
          printf(" WEAK");
        }

        if (syms[i]->flags & BSF_SECTION_SYM) {
          printf(" SECTION_SYM");
        }

        if (syms[i]->flags & BSF_OLD_COMMON) {
          printf(" OLD_COMMON");
        }

        if (syms[i]->flags & BSF_NOT_AT_END) {
          printf(" NOT_AT_END");
        }

        if (syms[i]->flags & BSF_CONSTRUCTOR) {
          printf(" CONSTRUCTOR");
        }

        if (syms[i]->flags & BSF_WARNING) {
          printf(" WARNING");
        }

        if (syms[i]->flags & BSF_INDIRECT) {
          printf(" INDIRECT");
        }

        if (syms[i]->flags & BSF_FILE) {
          printf(" FILE");
        }

        if (syms[i]->flags & BSF_DYNAMIC) {
          printf(" DYNAMIC");
        }

        if (syms[i]->flags & BSF_OBJECT) {
          printf(" OBJECT");
        }

        if (syms[i]->flags & BSF_DEBUGGING_RELOC) {
          printf(" DEBUGGING_RELOC");
        }

        if (syms[i]->flags & BSF_THREAD_LOCAL) {
          printf(" THREAD_LOCAL");
        }

        if (syms[i]->flags & BSF_RELC) {
          printf(" RELC");
        }

        if (syms[i]->flags & BSF_SRELC) {
          printf(" SRELC");
        }

        if (syms[i]->flags & BSF_SYNTHETIC) {
          printf(" SYNTHETIC");
        }

        if (syms[i]->flags & BSF_GNU_INDIRECT_FUNCTION) {
          printf(" GNU_INDIRECT_FUNCTION");
        }

        printf("\n");
    }
}

static char *dis_output;

static int stringify_callback(FILE *stream, const char *format, ...)
{
	char buffer[128];
	va_list vl;
	int rc;

	// need a smarter struct for dis_output that contains a pointer for where to put the data and a lenght of thedata already in the string
	// then don't need to strcat, but just print into the buffer indexed by length

	if (dis_output == nullptr) {
		return 0;
	}

	va_start(vl, format);
	rc = vsprintf(buffer, format, vl);
	va_end(vl);

	strcat(dis_output,buffer);

	return rc;
}

// Section Class Methods
cachedInstInfo::cachedInstInfo(const char *file,const char *func,int linenum,const char *lineTxt,const char *instText,TraceDqr::RV_INST inst,int instSize,const char *addresslabel,int addresslabeloffset,bool haveoperandaddress,TraceDqr::ADDRESS operandaddress,const char *operandlabel,int operandlabeloffset)
{
	// Don't need to allocate and copy file and function. They will remain until trace object is deleted

	filename = file;
	functionname = func;
	linenumber = linenum;
	lineptr = lineTxt;

	instruction = inst;
	instsize = instSize;

	addressLabel = addresslabel;
	addressLabelOffset = addresslabeloffset;

	haveOperandAddress = haveoperandaddress;
	operandAddress = operandaddress;
	operandLabel = operandlabel;
	operandLabelOffset = operandlabeloffset;

	// Need to allocate and copy instruction. src changes every time an instruction is disassembled, so we need to save it

	if (instText != nullptr) {
		int	s = strlen(instText)+1;
		instructionText = new char [s];
		strcpy(instructionText,instText);
	}
	else {
		instructionText = nullptr;
	}
}

cachedInstInfo::~cachedInstInfo()
{
	filename = nullptr;
	functionname = nullptr;
	lineptr = nullptr;
	addressLabel = nullptr;
	operandLabel = nullptr;

	if (instructionText != nullptr) {
		delete [] instructionText;
		instructionText = nullptr;
	}
}

void cachedInstInfo::dump()
{
	printf("cachedInstInfo()\n");
	printf("filename: '%s'\n",filename);
	printf("fucntion: '%s'\n",functionname);
	printf("linenumber: %d\n",linenumber);
	printf("lineptr: '%s'\n",lineptr);
	printf("instructin: 0x%08x\n",instruction);
	printf("instruction size: %d\n",instsize);
	printf("instruction text: '%s'\n",instructionText);
	printf("addressLabel: '%s'\n",addressLabel);
	printf("addressLabelOffset: %d\n",addressLabelOffset);
	printf("haveOperandAddress: %d\n",haveOperandAddress);
	printf("operandAddress: 0x%08x\n",operandAddress);
	printf("operandLabel:%p\n",operandLabel);
	printf("operandLabel: '%s'\n",operandLabel);
	printf("operandLabelOffset: %d\n",operandLabelOffset);
}

// work with elf file sections using libbfd

section::section()
{
	next      = nullptr;
	abfd      = nullptr;
	asecptr   = nullptr;
	size      = 0;
	startAddr = (TraceDqr::ADDRESS)0;
	endAddr   = (TraceDqr::ADDRESS)0;
	code      = nullptr;
	cachedInfo = nullptr;
}

section::~section()
{
	if (code != nullptr) {
		delete [] code;
		code = nullptr;
	}

	if (cachedInfo != nullptr) {
		for (int i = 0; i < size/2; i++) {
			if (cachedInfo[i] != nullptr) {
				delete cachedInfo[i];
				cachedInfo[i] = nullptr;
			}
		}

		delete [] cachedInfo;
		cachedInfo = nullptr;
	}
}

section *section::initSection(section **head, asection *newsp,bool enableInstCaching)
{
	next = *head;
	*head = this;

	abfd = newsp->owner;
	asecptr = newsp;
	size = newsp->size;
	startAddr = (TraceDqr::ADDRESS)newsp->vma;
	endAddr = (TraceDqr::ADDRESS)(newsp->vma + size - 1);

	int words = (size+1)/2;

	code = nullptr;

//	could make code only read when it is needed (lazzy eval), so if nver used, no memory is
//	ever allocated and no time to read

	code = new (std::nothrow) uint16_t[words];

	assert(code != nullptr);

    bfd_boolean rc;
    rc = bfd_get_section_contents(abfd,newsp,(void*)code,0,size);
    if (rc != TRUE) {
      printf("Error: bfd_get_section_contents() failed\n");
      return nullptr;
    }

    if (enableInstCaching) {
    	cachedInfo = new cachedInstInfo*[size/2];	// divide by 2 because we want entries for addr/2

    	for (int i = 0; i < size/2; i++) {
    		cachedInfo[i] = nullptr;
    	}
    }
    else {
    	cachedInfo = nullptr;
    }

    return this;
}

section *section::getSectionByAddress(TraceDqr::ADDRESS addr)
{
	section *sp = this;

	while (sp != nullptr) {
		if ((addr >= sp->startAddr) && (addr <= sp->endAddr)) {
			return sp;
		}

		sp = sp->next;
	}

	return nullptr;
}

cachedInstInfo *section::setCachedInfo(TraceDqr::ADDRESS addr,const char *file,const char *func,int linenum,const char *lineTxt,const char *instTxt,TraceDqr::RV_INST inst,int instSize,const char *addresslabel,int addresslabeloffset,bool haveoperandaddress,TraceDqr::ADDRESS operandaddress,const char *operandlabel,int operandlabeloffset)
{
	if ((addr >= startAddr) && (addr <= endAddr)) {
		if (cachedInfo != nullptr) {

			int index = (addr - startAddr) >> 1;

			assert(cachedInfo[index] == nullptr);

			cachedInstInfo *cci;
			cci = new cachedInstInfo(file,func,linenum,lineTxt,instTxt,inst,instSize,addresslabel,addresslabeloffset,haveoperandaddress,operandaddress,operandlabel,operandlabeloffset);

			cachedInfo[index] = cci;

			return cci;
		}
	}

	return nullptr;
}

cachedInstInfo *section::getCachedInfo(TraceDqr::ADDRESS addr)
{
	if ((addr >= startAddr) && (addr <= endAddr)) {
		if (cachedInfo != nullptr) {
			return cachedInfo[(addr - startAddr) >> 1];
		}
	}

	return nullptr;
}

int      Instruction::addrSize;
uint32_t Instruction::addrDispFlags;
int      Instruction::addrPrintWidth;

std::string Instruction::addressToString(int labelLevel)
{
	char dst[128];

	addressToText(dst,sizeof dst,labelLevel);

	return std::string(dst);
}

void Instruction::addressToText(char *dst,size_t len,int labelLevel)
{
	assert(dst != nullptr);

	dst[0] = 0;

	if (addrDispFlags & TraceDqr::ADDRDISP_WIDTHAUTO) {
		while (address > (0xffffffffffffffffllu >> (64 - addrPrintWidth*4))) {
			addrPrintWidth += 1;
		}
	}

    int n;

	if ((addrPrintWidth > 8) && (addrDispFlags & TraceDqr::ADDRDISP_SEP)) {
		n = snprintf(dst,len,"%0*x.%08x",addrPrintWidth-8,(uint32_t)(address >> 32),(uint32_t)address);
	}
	else {
		n = snprintf(dst,len,"%0*llx",addrPrintWidth,address);
	}

    if ((labelLevel >= 1) && (addressLabel != nullptr)) {
    	if (addressLabelOffset != 0) {
		    snprintf(dst+n,len-n," <%s+%x>",addressLabel,addressLabelOffset);
		}
		else {
		    snprintf(dst+n,len-n," <%s>",addressLabel);
		}
	}
}

std::string Instruction::instructionToString(int labelLevel)
{
	char dst[128];

	instructionToText(dst,sizeof dst,labelLevel);

	return std::string(dst);
}

void Instruction::instructionToText(char *dst,size_t len,int labelLevel)
{
	assert(dst != nullptr);

	int n;

	dst[0] = 0;

//	should cache this (as part of other instruction stuff cached)!!

	if (instSize == 32) {
		n = snprintf(dst,len,"%08x           %s",instruction,instructionText);
	}
	else {
		n = snprintf(dst,len,"%04x               %s",instruction,instructionText);
	}

	if (haveOperandAddress) {
		n += snprintf(dst+n,len-n,"%llx",operandAddress);

		if (labelLevel >= 1) {
			if (operandLabel != nullptr) {
				if (operandLabelOffset != 0) {
					snprintf(dst+n,len-n," <%s+%x>",operandLabel,operandLabelOffset);
				}
				else {
					snprintf(dst+n,len-n," <%s>",operandLabel);
				}
			}
		}
	}
}

std::string Instruction::addressLabelToString()
{

	if (addressLabel != nullptr) {
		return std::string (addressLabel);
	}

	return std::string("");
}

std::string Instruction::operandLabelToString()
{
	if (operandLabel != nullptr) {
		return std::string(operandLabel);
	}

	return std::string("");
}

const char *Source::stripPath(const char *path)
{
	if (path == nullptr) {
		return sourceFile;
	}

	const char *s = sourceFile;

	if (s == nullptr) {
		return nullptr;
	}

	for (;;) {
		if (*path == 0) {
			return s;
		}

		if (tolower(*path) == tolower(*s)) {
			path += 1;
			s += 1;
		}
		else if (*path == '/') {
			if (*s != '\\') {
				return sourceFile;
			}
			path += 1;
			s += 1;
		}
		else if (*path == '\\') {
			if (*s != '/') {
				return sourceFile;
			}
			path += 1;
			s += 1;
		}
		else {
			return sourceFile;
		}
	}

	return nullptr;
}

std::string Source::sourceFileToString(std::string path)
{
	if (sourceFile != nullptr) {

		const char *sf = stripPath(path.c_str());

		return std::string(sf);
	}

	return std::string("");
}

std::string Source::sourceFileToString()
{
	if (sourceFile != nullptr) {
		return std::string(sourceFile);
	}

	return std::string("");
}

std::string Source::sourceLineToString()
{
	if (sourceLine != nullptr) {
		return std::string(sourceLine);
	}

	return std::string("");
}

std::string Source::sourceFunctionToString()
{
	if (sourceFunction != nullptr) {
		return std::string(sourceFunction);
	}

	return std::string("");
}

fileReader::fileReader(/*paths*/)
{
	lastFile = nullptr;
	files = nullptr;
}

fileReader::~fileReader()
{
	for (fileList *fl = files; fl != nullptr;) {
		fileList *nextFl = fl->next;

		for (funcList *func = fl->funcs; func != nullptr;) {
			funcList *nextFunc = func->next;
			if (func->func != nullptr) {
				delete [] func->func;
				func->func = nullptr;
			}
			delete func;
			func = nextFunc;
		}

		if (fl->name != nullptr) {
			delete [] fl->name;
			fl->name = nullptr;
		}

		if (fl->lines != nullptr) {
			if (fl->lines[0] != nullptr) {
				delete [] fl->lines[0];
			}
			delete fl->lines;
			fl->lines = nullptr;
		}

		fl = nextFl;
	}

	files = nullptr;
}

fileReader::fileList *fileReader::readFile(const char *file)
{
	assert(file != nullptr);

	const char *original_file_name = file;

	std::ifstream  f(file, std::ifstream::binary);

	if (!f) {
//		printf("Error: readFile(): could not open file %s for input\n",file);

		// try again after stripping off path

		int l = -1;

		for (int i = 0; file[i] != 0; i++) {
			if ((file[i] == '/') || (file[i] == '\\')) {
				l = i;
			}
		}

		if (l != -1) {
			file = &file[l+1];
			f.open(file, std::ifstream::binary);
		}
	}

	// could put another try here is !f is true, such as searching a provided path?

	// if (!f) {
	//    char fn[256];
	//
	//    strcpy(fn,path);
	//    if ((path[strlen(path)] != '/') && (path[strlen(path)] != '\\')) {
	//        strcat(fn,'/');
	//    }
	//    strcat(fn,file);	// file is potentially adjusted above]
	//
	//    f.open(fn, ifsteam::binary);
	//  }

	fileList *fl = new fileList;

	fl->next = files;
	fl->funcs = nullptr;
	files = fl;

	int len = strlen(original_file_name)+1;
	char *name = new char[len];
	strcpy(name,original_file_name);

	fl->name = name;

	if (!f) {
		// always return a file list pointer, even if a file isn't found. If file not
		// found, lines will be null

		fl->lineCount = 0;
		fl->lines = nullptr;

		return fl;
	}

	// get length of file:

	f.seekg (0, f.end);
	int length = f.tellg();
	f.seekg (0, f.beg);

	// allocate memory:

	char *buffer = new char [length];

	// read file into buffer

	f.read(buffer,length);

	f.close();

	// count lines

	int lc = 1;

	for (int i = 0; i < length; i++) {
		if (buffer[i] == '\n') {
			lc += 1;
		}
	}

	// create array of line pointers

	char **lines = new char *[lc];

	// initialize arry of ptrs

	int l;
	int s;

	l = 0;
	s = 1;

	for (int i = 0; i < length;i++) {
		if (s != 0) {
			lines[l] = &buffer[i];
			l += 1;
			s = 0;
		}

		// strip out CRs and LFs

		if (buffer[i] == '\r') {
			buffer[i] = 0;
		}
		else if (buffer[i] == '\n') {
			buffer[i] = 0;
			s = 1;
		}
	}

	if (l >= lc) {
		delete [] lines;
		delete [] buffer;

		fl->lineCount = 0;
		fl->lines = nullptr;

		printf("Error: readFile(): Error computing line count for file\n");

		return nullptr;
	}

	lines[l] = nullptr;

	fl->lineCount = l;
	fl->lines = lines;

	return fl;
}

fileReader::fileList *fileReader::findFile(const char *file)
{
	assert(file != nullptr);

	// first check if file is same as last one uesed

	if (lastFile != nullptr) {
		if (strcmp(lastFile->name,file) == 0) {
			// found file!
			return lastFile;
		}
	}

	struct fileList *fp;

	for (fp = files; fp != nullptr; fp = fp->next) {
		if (strcmp(fp->name,file) == 0) {
			lastFile = fp;
			return fp;
		}
	}

	// didn't find file. See if we can read it in

	fp = readFile(file);

	return fp;
}


Symtab::Symtab(bfd *abfd)
{
	assert(abfd != nullptr);

	this->abfd = abfd;

	if ((bfd_get_file_flags (abfd) & HAS_SYMS)) {
		long storage_needed;

		storage_needed = bfd_get_symtab_upper_bound(abfd);

		if (storage_needed > 0 ) {
			symbol_table = (asymbol**)new (std::nothrow) char[storage_needed];

			assert(symbol_table != nullptr);

			number_of_symbols = bfd_canonicalize_symtab(abfd,symbol_table);

//			printf("symtable 2: storage needed: %d, number of symbols: %d, symbol_table: %08x\n",storage_needed, number_of_symbols, symbol_table);
		}
		else {
			symbol_table = nullptr;
		}
	}
	else {
		symbol_table = nullptr;
	}

    vma = 0;
    index = 0;
}

Symtab::~Symtab()
{
	if (symbol_table != nullptr) {
		delete[] symbol_table;
		symbol_table = nullptr;
	}
}

const char *Symtab::getSymbolByAddress(TraceDqr::ADDRESS addr)
{
	if (addr == 0) {
		return nullptr;
	}

	vma = addr;

	struct bfd_section *section;
	bfd_vma section_base_vma;

	index = 0;

	for (int i = 0; i < number_of_symbols; i++) {
		section = symbol_table[i]->section;
	    section_base_vma = section->vma;

	    if ((section_base_vma + symbol_table[i]->value == vma) && (symbol_table[i]->flags & BSF_FUNCTION)) {
	    	index = i;

	    	return symbol_table[i]->name;
	    }
	}

	return nullptr;
}

// probably can delete the getNextSymboByAddress() funcs

const char *Symtab::getNextSymbolByAddress()
{
	struct bfd_section *section;
	bfd_vma section_base_vma;

	for (int i = index+1; i < number_of_symbols; i++) {
		section = symbol_table[i]->section;
	    section_base_vma = section->vma;

	    if ((section_base_vma + symbol_table[i]->value == vma) && (symbol_table[i]->flags & BSF_FUNCTION)) {
	    	index = i;

	    	return symbol_table[i]->name;
	    }
	}

	return nullptr;
}

void Symtab::dump()
{
    printf("number_of_symbols: %ld\n",number_of_symbols);

    for (int i = 0; i < number_of_symbols; i++) {
      printf("symname: %s: \t",symbol_table[i]->name);
      bfd_print_symbol_vandf(abfd,stdout,symbol_table[i]);
      printf("\n");
    }
}

bool ElfReader::init = false;

ElfReader::ElfReader(char *elfname)
{
  assert(elfname != nullptr);

  if (init == false) {
	  // only call bfd_init once - not once per object

	  bfd_init();
	  init = true;
  }

  abfd = bfd_openr(elfname,NULL);
  if (abfd == nullptr) {
    status = TraceDqr::TraceDqr::DQERR_ERR;

    bfd_error_type bfd_error = bfd_get_error();
    printf("Error: bfd_openr() returned null. Error: %d\n",bfd_error);
    printf("Error: Can't open file %s\n",elfname);

    return;
  }

  if(!bfd_check_format(abfd,bfd_object)) {
    printf("Error: ElfReader(): %s not object file: %d\n",elfname,bfd_get_error());

	status = TraceDqr::TraceDqr::DQERR_ERR;

	return;
  }

  const bfd_arch_info_type *aitp;

  aitp = bfd_get_arch_info(abfd);
  if (aitp == nullptr) {
	  printf("Error: ElfReader(): Cannot get arch info for file %s\n",elfname);

	  status = TraceDqr::TraceDqr::DQERR_ERR;
	  return;
  }

// lines below are commented out because the bfd is reaturning the wrong arch for
// riscv arch (returning 67 and not 74)
//
//  printf("arch: %d, bfd_arch_riscv: %d, mach: %d\n",aitp->arch,bfd_arch_riscv,aitp->mach);
//
//  if (aitp->arch != bfd_arch_riscv) {
//    printf("Error: ElfReader(): elf file is not for risc-v architecture\n");
//
//	  status = dqr::dqr::DQERR_ERR;
//	  return;
//  }

  switch (aitp->mach) {
  case bfd_mach_riscv32:
	  archSize = 32;
	  bitsPerWord = aitp->bits_per_word;
	  bitsPerAddress = aitp->bits_per_address;
	  break;
  case bfd_mach_riscv64:
	  archSize = 64;
	  bitsPerWord = aitp->bits_per_word;
	  bitsPerAddress = aitp->bits_per_address;
	  break;
  default:
	  printf("Error: ElfReader(): elf file is for unknown machine type\n");

	  status = TraceDqr::TraceDqr::DQERR_ERR;
	  return;
  }

  symtab = nullptr;
  codeSectionLst = nullptr;

  for (asection *p = abfd->sections; p != NULL; p = p->next) {
	  if (p->flags & SEC_CODE) {
          // found a code section, add to list

		  section *sp = new section;
		  sp->initSection(&codeSectionLst,p,false);
	  }
  }

  status = TraceDqr::DQERR_OK;
}

ElfReader::~ElfReader()
{
	if (symtab != nullptr) {
		delete symtab;
		symtab = nullptr;
	}

	while (codeSectionLst != nullptr) {
		section *nextSection = codeSectionLst->next;
		delete codeSectionLst;
		codeSectionLst = nextSection;
	}

	if (abfd != nullptr) {
		bfd_close(abfd);

		abfd = nullptr;
	}
}

TraceDqr::DQErr ElfReader::getInstructionByAddress(TraceDqr::ADDRESS addr,TraceDqr::RV_INST &inst)
{
	// get instruction at addr

	// Address for code[0] is text->vma

	//don't forget base!!'

	// hmmm.. probably should cache section pointer, and not address/instruction! Or maybe not cache anything?

	section *sp;
	if (codeSectionLst == nullptr) {
		status = TraceDqr::DQERR_ERR;
		return status;
	}

	sp = codeSectionLst->getSectionByAddress(addr);
	if (sp == nullptr) {
		status = TraceDqr::DQERR_ERR;
		return status;
	}

	if ((addr < sp->startAddr) || (addr > sp->endAddr)) {
		status = TraceDqr::DQERR_ERR;
		return status;
	}

//	if ((addr < text->vma) || (addr >= text->vma + text->size)) {
////			don't know instruction - not part of text segment. need to handle for os
////			if we can't get the instruciton, get the next trace message and try again. or do
////			we need the next sync message and the next trace message and try again?
//
//		// for now, just return an error
//
//		status = dqr::DQERR_ERR;
//		return status;
//	}

	int index;

	index = (addr - sp->startAddr) / 2;

	inst = sp->code[index];

	// if 32 bit instruction, get the second half

	switch (inst & 0x0003) {
	case 0x0000:	// quadrant 0, compressed
	case 0x0001:	// quadrant 1, compressed
	case 0x0002:	// quadrant 2, compressed
		status = TraceDqr::DQERR_OK;
		break;
	case 0x0003:	// not compressed. Assume RV32 for now
		if ((inst & 0x1f) == 0x1f) {
			fprintf(stderr,"Error: getInstructionByAddress(): cann't decode instructions longer than 32 bits\n");
			status = TraceDqr::DQERR_ERR;
			break;
		}

		inst = inst | (((uint32_t)sp->code[index+1]) << 16);

		status = TraceDqr::DQERR_OK;
		break;
	}

	return status;
}

Symtab *ElfReader::getSymtab()
{
	if (symtab == nullptr) {
		symtab = new (std::nothrow) Symtab(abfd);

		assert(symtab != nullptr);
	}

	return symtab;
}

TsList::TsList()
{
	prev = nullptr;
	next = nullptr;
	message = nullptr;
	terminated = false;

	startTime = (TraceDqr::TIMESTAMP)0;
	endTime = (TraceDqr::TIMESTAMP)0;
}

TsList::~TsList()
{
}

ITCPrint::ITCPrint(int numCores, int buffSize,int channel)
{
	assert((numCores > 0) && (buffSize > 0));

	this->numCores = numCores;
	this->buffSize = buffSize;
	this->printChannel = channel;

	pbuff = new char*[numCores];

	for (int i = 0; i < numCores; i++) {
		pbuff[i] = new char[buffSize];
	}

	numMsgs = new int[numCores];

	for (int i=0; i< numCores; i++) {
		numMsgs[i] = 0;
	}

	pbi = new int[numCores];

	for (int i=0; i< numCores; i++) {
		pbi[i] = 0;
	}

	pbo = new int[numCores];

	for (int i=0; i< numCores; i++) {
		pbo[i] = 0;
	}

	freeList = nullptr;

	tsList = new TsList *[numCores];

	for (int i = 0; i < numCores; i++) {
		tsList[i] = nullptr;
	}
}

ITCPrint::~ITCPrint()
{
	if (pbuff != nullptr) {
		for (int i = 0; i < numCores; i++) {
			if (pbuff[i] != nullptr) {
				delete [] pbuff[i];
				pbuff[i] = nullptr;
			}
		}

		delete [] pbuff;
		pbuff = nullptr;
	}

	if (numMsgs != nullptr) {
		delete [] numMsgs;
		numMsgs = nullptr;
	}

	if (pbi != nullptr) {
		delete [] pbi;
		pbi = nullptr;
	}

	if (pbo != nullptr) {
		delete [] pbo;
		pbo = nullptr;
	}

	if (freeList != nullptr) {
		TsList *tl = freeList;
		while (tl != nullptr) {
			TsList *tln = tl->next;
			delete tl;
			tl = tln;
		}
		freeList = nullptr;
	}

	if (tsList != nullptr) {
		for (int i = 0; i < numCores; i++) {
			TsList *tl = tsList[i];
			while (tl != nullptr) {
				TsList *tln = tl->next;
				delete tl;
				tl = tln;
			}
		}
		tsList = nullptr;
	}
}

int ITCPrint::roomInITCPrintQ(uint8_t core)
{
	if (core >= numCores) {
		return 0;
	}

	if (pbi[core] > pbo[core]) {
		return buffSize - pbi[core] + pbo[core] - 1;
	}

	if (pbi[core] < pbo[core]) {
		return pbo[core] - pbi[core] - 1;
	}

	return buffSize-1;
}

bool ITCPrint::print(uint8_t core, uint32_t addr, uint32_t data,TraceDqr::TIMESTAMP tstamp)
{
	if (core >= numCores) {
		return false;
	}

	if ((addr < (uint32_t)printChannel*4) || (addr >= (((uint32_t)printChannel+1)*4))) {
		// not writing to this itc channel

		return false;
	}

	TsList *tlp;
	tlp = tsList[core];
	TsList *workingtlp;

    if ((tlp == nullptr) || tlp->terminated == true) {
		// see if there is one on the free list before making a new one

		if(freeList != nullptr) {
			workingtlp = freeList;
			freeList = workingtlp->next;

			workingtlp->terminated = false;
			workingtlp->message = nullptr;
		}
		else {
			workingtlp = new TsList();
		}

		if (tlp == nullptr) {
			workingtlp->next = workingtlp;
			workingtlp->prev = workingtlp;
		}
		else {
			workingtlp->next = tlp;
			workingtlp->prev = tlp->prev;

			tlp->prev = workingtlp;
			workingtlp->prev->next = workingtlp;
		}

		workingtlp->startTime = tstamp;
		workingtlp->message = &pbuff[core][pbi[core]];

		tsList[core] = workingtlp;
	}
    else {
    	workingtlp = tlp;
    }

	// workingtlp now points to unterminated TSList object (in progress)

    workingtlp->endTime = tstamp;

	char *p = (char *)&data;
	int room = roomInITCPrintQ(core);

	for (int i = 0; ((size_t)i < ((sizeof data) - (addr & 0x03))); i++ ) {
		if (room >= 2) {
			pbuff[core][pbi[core]] = p[i];
			pbi[core] += 1;
			if (pbi[core] >= buffSize) {
				pbi[core] = 0;
			}
			room -= 1;

			switch (p[i]) {
			case 0:
				numMsgs[core] += 1;

				workingtlp->terminated = true;
				break;
			case '\n':
			case '\r':
				pbuff[core][pbi[core]] = 0;	// add a null termination after the eol
				pbi[core] += 1;
				if (pbi[core] >= buffSize) {
					pbi[core] = 0;
				}
				room -= 1;
				numMsgs[core] += 1;

				workingtlp->terminated = true;
				break;
			default:
				break;
			}
		}
	}

	pbuff[core][pbi[core]] = 0; // make sure always null terminated. This may be a temporary null termination

	return true;
}

void ITCPrint::haveITCPrintData(int numMsgs[], bool havePrintData[])
{
	if (numMsgs != nullptr) {
		for (int i = 0; i < numCores; i++) {
			numMsgs[i] = this->numMsgs[i];
		}
	}

	if (havePrintData != nullptr) {
		for (int i = 0; i < numCores; i++) {
			havePrintData[i] = pbi[i] != pbo[i];
		}
	}
}

TsList *ITCPrint::consumeTerminatedTsList(int core)
{
	TsList *rv = nullptr;

	if (numMsgs[core] > 0) { // have stuff in the Q
		TsList *tsl = tsList[core];

		// tsl now points to the newest tslist object. We want the oldest, which will be tsl->prev

		if (tsl != nullptr) {
			tsl = tsl->prev;

			if (tsl->terminated == true) {
				rv = tsl;

				// unlink tsl (consume it)

				if (tsl->next == tsl) {
					// was the only one in list

					tsList[core] = nullptr;
				}
				else {
					tsList[core]->prev = tsl->prev;

					if (tsList[core]->next == tsl) {
						tsList[core]->next = tsl->next;
					}
				}

				tsl->next = freeList;
				freeList = tsl;
			}
		}
	}

	return rv;
}

TsList *ITCPrint::consumeOldestTsList(int core)
{
	TsList *rv;

	rv = consumeTerminatedTsList(core);
	if (rv == nullptr) {
		// no terminated itc prints. Look for one in progress

		TsList *tsl = tsList[core];

		if (tsl != nullptr) {
			// this must be an unterminated tsl. unlink tsl (consume it)

			rv = tsl;

			if (tsl->next == tsl) {
				// was the only one in the list!

				tsList[core] = nullptr;
			}
			else {
				tsList[core]->prev = tsl->prev;

				if (tsList[core]->next == tsl) {
					tsList[core]->next = tsl->next;
				}
			}

			tsl->next = freeList;
			freeList = tsl;
		}
	}

	return rv;
}

bool ITCPrint::getITCPrintMsg(uint8_t core,char* dst,int dstLen, TraceDqr::TIMESTAMP &startTime, TraceDqr::TIMESTAMP &endTime)
{
	bool rc = false;

	if (core >= numCores) {
		return false;
	}

	assert((dst != nullptr) && (dstLen > 0));

	if (numMsgs[core] > 0) {
		TsList *tsl = consumeTerminatedTsList(core);

		if (tsl != nullptr) {
			startTime = tsl->startTime;
			endTime = tsl->endTime;
		}
		else {
			assert (tsl != nullptr);
		}

		rc = true;
		numMsgs[core] -= 1;

		while (pbuff[core][pbo[core]] && (dstLen > 1)) {
			*dst++ = pbuff[core][pbo[core]];
			dstLen -= 1;

			pbo[core] += 1;
			if (pbo[core] >= buffSize) {
				pbo[core] = 0;
			}
		}

		*dst = 0;

		// skip past null terminted end of message

		pbo[core] += 1;
		if (pbo[core] >= buffSize) {
			pbo[core] = 0;
		}
	}

	return rc;
}

bool ITCPrint::flushITCPrintMsg(uint8_t core, char *dst, int dstLen, TraceDqr::TIMESTAMP &startTime, TraceDqr::TIMESTAMP &endTime)
{
	if (core >= numCores) {
		return false;
	}

	assert((dst != nullptr) && (dstLen > 0));

	if (numMsgs[core] > 0) {
		return getITCPrintMsg(core,dst,dstLen,startTime,endTime);
	}

	if (pbo[core] != pbi[core]) {
		TsList *tsl = consumeOldestTsList(core);

		assert (tsl != nullptr);
		assert (tsl->terminated == false);

		startTime = tsl->startTime;
		endTime = tsl->endTime;

		while (pbuff[core][pbo[core]] && (dstLen > 1)) {
			*dst++ = pbuff[core][pbo[core]];
			dstLen -= 1;

			pbo[core] += 1;
			if (pbo[core] >= buffSize) {
				pbo[core] = 0;
			}
		}
		return true;
	}

	return false;
}

bool ITCPrint::getITCPrintStr(uint8_t core, std::string &s,TraceDqr::TIMESTAMP &startTime, TraceDqr::TIMESTAMP &endTime)
{
	if (core >= numCores) {
		return false;
	}

	bool rc = false;

	if (numMsgs[core] > 0) { // stuff in the Q
		rc = true;

		TsList *tsl = consumeTerminatedTsList(core);

		assert (tsl != nullptr);

		startTime = tsl->startTime;
		endTime = tsl->endTime;

		numMsgs[core] -= 1;

		while (pbuff[core][pbo[core]]) {
			s += pbuff[core][pbo[core]];

			pbo[core] += 1;
			if (pbo[core] >= buffSize) {
				pbo[core] = 0;
			}
		}

		pbo[core] += 1;
		if (pbo[core] >= buffSize) {
			pbo[core] = 0;
		}
	}

	return rc;
}

bool ITCPrint::flushITCPrintStr(uint8_t core, std::string &s, TraceDqr::TIMESTAMP &startTime, TraceDqr::TIMESTAMP &endTime)
{
	if (core >= numCores) {
		return false;
	}

	if (numMsgs[core] > 0) {
		return getITCPrintStr(core,s,startTime,endTime);
	}

	if (pbo[core] != pbi[core]) {
		TsList *tsl = consumeOldestTsList(core);

		assert (tsl != nullptr);
		assert (tsl->terminated == false);

		startTime = tsl->startTime;
		endTime = tsl->endTime;

		s = "";
		while (pbuff[core][pbo[core]]) {
			s += pbuff[core][pbo[core]];

			pbo[core] += 1;
			if (pbo[core] >= buffSize) {
				pbo[core] = 0;
			}
		}
		return true;
	}

	return false;
}

Analytics::Analytics()
{
	cores = 0;
	num_trace_msgs_all_cores = 0;
	num_trace_bits_all_cores = 0;
	num_trace_bits_all_cores_max = 0;
	num_trace_bits_all_cores_min = 0;
	num_trace_mseo_bits_all_cores = 0;

	num_inst_all_cores = 0;
	num_inst16_all_cores = 0;
	num_inst32_all_cores = 0;

	num_branches_all_cores = 0;

	for (int i = 0; i < DQR_MAXCORES; i++) {
		core[i].num_inst16 = 0;
		core[i].num_inst32 = 0;
		core[i].num_inst = 0;

		core[i].num_trace_msgs = 0;
		core[i].num_trace_syncs = 0;
		core[i].num_trace_dbranch = 0;
		core[i].num_trace_ibranch = 0;
		core[i].num_trace_ihistory = 0;
		core[i].num_trace_ihistoryws = 0;
		core[i].num_trace_resourcefull = 0;
		core[i].num_trace_incircuittraceWS = 0;
		core[i].num_trace_incircuittrace = 0;

		core[i].num_trace_dataacq = 0;
		core[i].num_trace_dbranchws = 0;
		core[i].num_trace_ibranchws = 0;
		core[i].num_trace_correlation = 0;
		core[i].num_trace_auxaccesswrite = 0;
		core[i].num_trace_ownership = 0;
		core[i].num_trace_error = 0;

		core[i].num_trace_ts = 0;
		core[i].num_trace_uaddr = 0;
		core[i].num_trace_faddr = 0;
		core[i].num_trace_ihistory_taken_branches = 0;
		core[i].num_trace_ihistory_nottaken_branches = 0;
		core[i].num_trace_resourcefull_i_cnt = 0;
		core[i].num_trace_resourcefull_hist = 0;
		core[i].num_trace_resourcefull_takenCount = 0;
		core[i].num_trace_resourcefull_notTakenCount = 0;
		core[i].num_trace_resourcefull_taken_branches = 0;
		core[i].num_trace_resourcefull_nottaken_branches = 0;

		core[i].trace_bits = 0;
		core[i].trace_bits_max = 0;
		core[i].trace_bits_min = 0;
		core[i].trace_bits_mseo = 0;

		core[i].max_hist_bits = 0;
		core[i].min_hist_bits = 0;
		core[i].max_notTakenCount = 0;
		core[i].min_notTakenCount = 0;
		core[i].max_takenCount = 0;
		core[i].min_takenCount = 0;

		core[i].trace_bits_sync = 0;
		core[i].trace_bits_dbranch = 0;
		core[i].trace_bits_ibranch = 0;
		core[i].trace_bits_dataacq = 0;
		core[i].trace_bits_dbranchws = 0;
		core[i].trace_bits_ibranchws = 0;
		core[i].trace_bits_ihistory = 0;
		core[i].trace_bits_ihistoryws = 0;
		core[i].trace_bits_resourcefull = 0;
		core[i].trace_bits_correlation = 0;
		core[i].trace_bits_auxaccesswrite = 0;
		core[i].trace_bits_ownership = 0;
		core[i].trace_bits_error = 0;
		core[i].trace_bits_incircuittrace = 0;
		core[i].trace_bits_incircuittraceWS = 0;

		core[i].trace_bits_ts = 0;
		core[i].trace_bits_ts_max = 0;
		core[i].trace_bits_ts_min = 0;

		core[i].trace_bits_uaddr = 0;
		core[i].trace_bits_uaddr_max = 0;
		core[i].trace_bits_uaddr_min = 0;

		core[i].trace_bits_faddr = 0;
		core[i].trace_bits_faddr_max = 0;
		core[i].trace_bits_faddr_min = 0;

		core[i].trace_bits_hist = 0;

		core[i].num_taken_branches = 0;
		core[i].num_notTaken_branches = 0;
		core[i].num_calls = 0;
		core[i].num_returns = 0;
		core[i].num_swaps = 0;
		core[i].num_exceptions = 0;
		core[i].num_exception_returns = 0;
		core[i].num_interrupts = 0;
	}

#ifdef DO_TIMES
	etimer = new Timer();
#endif // DO_TIMES

	status = TraceDqr::DQERR_OK;
}

Analytics::~Analytics()
{
#ifdef DO_TIMES
	if (etimer != nullptr) {
		delete etimer;
		etimer = nullptr;
	}
#endif // DO_TIMES
}

TraceDqr::DQErr Analytics::updateTraceInfo(NexusMessage &nm,uint32_t bits,uint32_t mseo_bits,uint32_t ts_bits,uint32_t addr_bits)
{
	bool have_uaddr = false;
	bool have_faddr = false;

	num_trace_msgs_all_cores += 1;
	num_trace_bits_all_cores += bits;
	num_trace_mseo_bits_all_cores += mseo_bits;

	core[nm.coreId].num_trace_msgs += 1;

	if (bits > num_trace_bits_all_cores_max) {
		num_trace_bits_all_cores_max = bits;
	}

	if ((num_trace_bits_all_cores_min == 0) || (bits < num_trace_bits_all_cores_min)) {
		num_trace_bits_all_cores_min = bits;
	}

	core[nm.coreId].trace_bits_mseo += mseo_bits;
	core[nm.coreId].trace_bits += bits;

	if (bits > core[nm.coreId].trace_bits_max) {
		core[nm.coreId].trace_bits_max = bits;
	}

	if ((core[nm.coreId].trace_bits_min == 0) || (bits < core[nm.coreId].trace_bits_min)) {
		core[nm.coreId].trace_bits_min = bits;
	}

	cores |= (1 << nm.coreId);

	if (ts_bits > 0) {
		core[nm.coreId].num_trace_ts += 1;
		core[nm.coreId].trace_bits_ts += ts_bits;

		if (ts_bits > core[nm.coreId].trace_bits_ts_max) {
			core[nm.coreId].trace_bits_ts_max = ts_bits;
		}

		if ((core[nm.coreId].trace_bits_ts_min == 0) || (ts_bits < core[nm.coreId].trace_bits_ts_min)) {
			core[nm.coreId].trace_bits_ts_min = ts_bits;
		}
	}

	int msb;
	uint64_t mask;
	int taken;
	int nottaken;

	switch (nm.tcode) {
	case TraceDqr::TCODE_OWNERSHIP_TRACE:
		core[nm.coreId].num_trace_ownership += 1;
		core[nm.coreId].trace_bits_ownership += bits;
		break;
	case TraceDqr::TCODE_DIRECT_BRANCH:
		core[nm.coreId].num_trace_dbranch += 1;
		core[nm.coreId].trace_bits_dbranch += bits;
		num_branches_all_cores += 1;
		break;
	case TraceDqr::TCODE_INDIRECT_BRANCH:
		core[nm.coreId].num_trace_ibranch += 1;
		core[nm.coreId].trace_bits_ibranch += bits;
		num_branches_all_cores += 1;

		have_uaddr = true;
		break;
	case TraceDqr::TCODE_DATA_ACQUISITION:
		core[nm.coreId].num_trace_dataacq += 1;
		core[nm.coreId].trace_bits_dataacq += bits;
		break;
	case TraceDqr::TCODE_ERROR:
		core[nm.coreId].num_trace_error += 1;
		core[nm.coreId].trace_bits_error += bits;
		break;
	case TraceDqr::TCODE_SYNC:
		core[nm.coreId].num_trace_syncs += 1;
		core[nm.coreId].trace_bits_sync += bits;

		have_faddr = true;
		break;
	case TraceDqr::TCODE_DIRECT_BRANCH_WS:
		core[nm.coreId].num_trace_dbranchws += 1;
		core[nm.coreId].trace_bits_dbranchws += bits;
		num_branches_all_cores += 1;

		have_faddr = true;
		break;
	case TraceDqr::TCODE_INDIRECT_BRANCH_WS:
		core[nm.coreId].num_trace_ibranchws += 1;
		core[nm.coreId].trace_bits_ibranchws += bits;
		num_branches_all_cores += 1;

		have_faddr = true;
		break;
	case TraceDqr::TCODE_AUXACCESS_WRITE:
		core[nm.coreId].num_trace_auxaccesswrite += 1;
		core[nm.coreId].trace_bits_auxaccesswrite += bits;
		break;
	case TraceDqr::TCODE_CORRELATION:
		core[nm.coreId].num_trace_correlation += 1;
		core[nm.coreId].trace_bits_ibranchws += bits;
		break;
	case TraceDqr::TCODE_INDIRECTBRANCHHISTORY:
		core[nm.coreId].num_trace_ihistory += 1;
		core[nm.coreId].trace_bits_ihistory += bits;

		// need to find msb = 1

		msb = -1;
		mask = nm.indirectHistory.history;
		taken = -1;	// start at -1 to account for stop bit, which isn't a branch
		nottaken = 0;

		while (mask > 1) { // use > 1 because the most significant 1 is a stop bit which we don't want to count!
			msb += 1;
			if (mask & 1) {
				taken += 1;
			}
			else {
				nottaken += 1;
			}
			mask >>= 1;
		}

		core[nm.coreId].num_trace_ihistory_taken_branches += taken;
		core[nm.coreId].num_trace_ihistory_nottaken_branches += nottaken;

		if (msb >= 0) {
			core[nm.coreId].trace_bits_hist += msb+1;

			if (msb >= (int32_t)core[nm.coreId].max_hist_bits) {
				core[nm.coreId].max_hist_bits = msb+1;
			}
		}

		if ((msb+1) < (int32_t)core[nm.coreId].min_hist_bits) {
			core[nm.coreId].min_hist_bits = msb+1;
		}

		num_branches_all_cores += 1 + taken + nottaken;

		have_uaddr = true;
		break;
	case TraceDqr::TCODE_INDIRECTBRANCHHISTORY_WS:
		core[nm.coreId].num_trace_ihistoryws += 1;
		core[nm.coreId].trace_bits_ihistoryws += bits;

		// need to find msb = 1

		msb = -1;
		mask = nm.indirectHistoryWS.history;
		taken = -1;	// start at -1 to account for stop bit, which isn't a branch
		nottaken = 0;

		while (mask > 1) {
			msb += 1;
			if (mask & 1) {
				taken += 1;
			}
			else {
				nottaken += 1;
			}
			mask >>= 1;
		}

		core[nm.coreId].num_trace_ihistory_taken_branches += taken;
		core[nm.coreId].num_trace_ihistory_nottaken_branches += nottaken;

		if (msb >= 0) {
			core[nm.coreId].trace_bits_hist += msb+1;

			if (msb >= (int32_t)core[nm.coreId].max_hist_bits) {
				core[nm.coreId].max_hist_bits = msb+1;
			}
		}

		if ((msb+1) < (int32_t)core[nm.coreId].min_hist_bits) {
			core[nm.coreId].min_hist_bits = msb+1;
		}

		num_branches_all_cores += 1 + taken + nottaken;

		have_faddr = true;
		break;
	case TraceDqr::TCODE_RESOURCEFULL:
		core[nm.coreId].num_trace_resourcefull += 1;
		core[nm.coreId].trace_bits_resourcefull += bits;

		switch (nm.resourceFull.rCode) {
		case 0:
			core[nm.coreId].num_trace_resourcefull_i_cnt += 1;
			break;
		case 1:
			core[nm.coreId].num_trace_resourcefull_hist += 1;

			// need to find msb = 1

			msb = -1;
			mask = nm.resourceFull.history;
			taken = -1;	// start at -1 to account for stop bit, which isn't a branch
			nottaken = 0;

			while (mask > 1) {
				msb += 1;
				if (mask & 1) {
					taken += 1;
				}
				else {
					nottaken += 1;
				}
				mask >>= 1;
			}

			core[nm.coreId].num_trace_ihistory_taken_branches += taken;
			core[nm.coreId].num_trace_ihistory_nottaken_branches += nottaken;

			if (msb >= 0) {
				core[nm.coreId].trace_bits_hist += msb+1;

				if (msb >= (int32_t)core[nm.coreId].max_hist_bits) {
					core[nm.coreId].max_hist_bits = msb+1;
				}
			}

			if ((msb+1) < (int32_t)core[nm.coreId].min_hist_bits) {
				core[nm.coreId].min_hist_bits = msb+1;
			}

			num_branches_all_cores += taken + nottaken;
			break;
		case 8:
			core[nm.coreId].num_trace_resourcefull_notTakenCount += 1;
			core[nm.coreId].num_trace_resourcefull_nottaken_branches += nm.resourceFull.notTakenCount;

			// compute avg/max/min not taken count

			if (nm.resourceFull.notTakenCount > (int32_t)core[nm.coreId].max_notTakenCount) {
				core[nm.coreId].max_notTakenCount = nm.resourceFull.notTakenCount;
			}

			if ((core[nm.coreId].min_notTakenCount == 0) || (nm.resourceFull.notTakenCount < (int32_t)core[nm.coreId].min_notTakenCount)) {
				core[nm.coreId].min_notTakenCount = nm.resourceFull.notTakenCount;
			}
			break;
		case 9:
			core[nm.coreId].num_trace_resourcefull_takenCount += 1;
			core[nm.coreId].num_trace_resourcefull_taken_branches += nm.resourceFull.takenCount;

			// compute avg/max/min taken count

			if (nm.resourceFull.takenCount > (int32_t)core[nm.coreId].max_takenCount) {
				core[nm.coreId].max_takenCount = nm.resourceFull.takenCount;
			}

			if ((core[nm.coreId].min_takenCount == 0) || (nm.resourceFull.takenCount < (int32_t)core[nm.coreId].min_takenCount)) {
				core[nm.coreId].min_takenCount = nm.resourceFull.takenCount;
			}
			break;
		default:
			printf("Error: Analytics::updateTraceInfo(): ResoureFull: unknown RDode: %d\n",nm.resourceFull.rCode);
			status = TraceDqr::DQERR_ERR;
			return status;
		}
		break;
	case TraceDqr::TCODE_INCIRCUITTRACE:
		core[nm.coreId].num_trace_incircuittrace += 1;
		core[nm.coreId].trace_bits_incircuittrace += bits;
		have_uaddr = true;
		break;
	case TraceDqr::TCODE_INCIRCUITTRACE_WS:
		core[nm.coreId].num_trace_incircuittraceWS += 1;
		core[nm.coreId].trace_bits_incircuittraceWS += bits;
		have_faddr = true;
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
	default:
		status = TraceDqr::DQERR_ERR;
		return status;
	}

	if (have_uaddr) {
		core[nm.coreId].num_trace_uaddr += 1;
		core[nm.coreId].trace_bits_uaddr += addr_bits;

		if (addr_bits > core[nm.coreId].trace_bits_uaddr_max) {
			core[nm.coreId].trace_bits_uaddr_max = addr_bits;
		}

		if ((core[nm.coreId].trace_bits_uaddr_min == 0) || (addr_bits < core[nm.coreId].trace_bits_uaddr_min)) {
			core[nm.coreId].trace_bits_uaddr_min = addr_bits;
		}
	}
	else if (have_faddr) {
		core[nm.coreId].num_trace_faddr += 1;
		core[nm.coreId].trace_bits_faddr += addr_bits;

		if (addr_bits > core[nm.coreId].trace_bits_faddr_max) {
			core[nm.coreId].trace_bits_faddr_max = addr_bits;
		}

		if ((core[nm.coreId].trace_bits_faddr_min == 0) || (addr_bits < core[nm.coreId].trace_bits_faddr_min)) {
			core[nm.coreId].trace_bits_faddr_min = addr_bits;
		}
	}

	return status;
}

TraceDqr::DQErr Analytics::updateInstructionInfo(uint32_t core_id,uint32_t inst,int instSize,int crFlags,TraceDqr::BranchFlags brFlags)
{
	num_inst_all_cores += 1;
	core[core_id].num_inst += 1;

	switch (instSize) {
	case 16:
		num_inst16_all_cores += 1;
		core[core_id].num_inst16 += 1;
		break;
	case 32:
		num_inst32_all_cores += 1;
		core[core_id].num_inst32 += 1;
		break;
	default:
		status = TraceDqr::DQERR_ERR;
	}

	switch (brFlags) {
	case TraceDqr::BRFLAG_none:
	case TraceDqr::BRFLAG_unknown:
		break;
	case TraceDqr::BRFLAG_taken:
		core[core_id].num_taken_branches += 1;
		break;
	case TraceDqr::BRFLAG_notTaken:
		core[core_id].num_notTaken_branches += 1;
		break;
	}

	if (crFlags & TraceDqr::isCall) {
		core[core_id].num_calls += 1;
	}
	if (crFlags & TraceDqr::isReturn) {
		core[core_id].num_returns += 1;
	}
	if (crFlags & TraceDqr::isSwap) {
		core[core_id].num_swaps += 1;
	}
	if (crFlags & TraceDqr::isInterrupt) {
		core[core_id].num_interrupts += 1;
	}
	if (crFlags & TraceDqr::isException) {
		core[core_id].num_exceptions += 1;
	}
	if (crFlags & TraceDqr::isExceptionReturn) {
		core[core_id].num_exception_returns += 1;
	}

	return status;
}

static void updateDst(int n, char *&dst,int &dst_len)
{
	if (n >= dst_len) {
		dst += dst_len;
		dst_len = 0;
	}
	else {
		dst += n;
		dst_len -= n;
	}
}

void Analytics::toText(char *dst,int dst_len,int detailLevel)
{
	char tmp_dst[512];
	int n;
#ifdef DO_TIMES
	double etime = etimer->etime();
#endif // DO_TIMES

	assert(dst != nullptr);

	dst[0] = 0;

	if (detailLevel <= 0) {
		return;
	}

	uint32_t have_ts = 0;

	for (int i = 0; i < DQR_MAXCORES; i++) {
		if (cores & (1<<i)) {
			if (core[i].num_trace_ts > 0) {
				have_ts |= (1<<i);
			}
		}
	}

	if (srcBits == 0) {
		n = snprintf(dst,dst_len,"Trace Analytics: Single core");
		updateDst(n,dst,dst_len);
	}
	else {
		n = snprintf(dst,dst_len,"Trace Analytics: Multi core (src field %d bits)",srcBits);
		updateDst(n,dst,dst_len);
	}

	if (have_ts == 0) {
		n = snprintf(dst,dst_len,"; Trace messages do not have timestamps\n");
		updateDst(n,dst,dst_len);
	}
	else if (have_ts == cores) {
		n = snprintf(dst,dst_len,"; Trace messages have timestamps\n");
		updateDst(n,dst,dst_len);
	}
	else {
		n = snprintf(dst,dst_len,"; Some trace messages have timestamps\n");
		updateDst(n,dst,dst_len);
	}

	if (detailLevel == 1) {
		n = snprintf(dst,dst_len,"\n");
		updateDst(n,dst,dst_len);

		n = snprintf(dst,dst_len,"Instructions             Compressed                   RV32\n");
		updateDst(n,dst,dst_len);

		n = snprintf(dst,dst_len,"  %10u    %10u (%0.2f%%)    %10u (%0.2f%%)\n",num_inst_all_cores,num_inst16_all_cores,((float)num_inst16_all_cores)/num_inst_all_cores*100.0,num_inst32_all_cores,((float)num_inst32_all_cores)/num_inst_all_cores*100.0);
		updateDst(n,dst,dst_len);

		n = snprintf(dst,dst_len,"\n");
		updateDst(n,dst,dst_len);

		n = snprintf(dst,dst_len,"Number of Trace Msgs      Avg Length    Min Length    Max Length    Total Length\n");
		updateDst(n,dst,dst_len);

		n = snprintf(dst,dst_len,"          %10u          %6.2f    %10u    %10u      %10u\n",num_trace_msgs_all_cores,((float)num_trace_bits_all_cores)/num_trace_msgs_all_cores,num_trace_bits_all_cores_min,num_trace_bits_all_cores_max,num_trace_bits_all_cores);
		updateDst(n,dst,dst_len);

		n = snprintf(dst,dst_len,"\n");
		updateDst(n,dst,dst_len);

		n = snprintf(dst,dst_len,"Trace bits per instruction:     %5.2f\n",((float)num_trace_bits_all_cores)/num_inst_all_cores);
		updateDst(n,dst,dst_len);

		n = snprintf(dst,dst_len,"Instructions per trace message: %5.2f\n",((float)num_inst_all_cores)/num_trace_msgs_all_cores);
		updateDst(n,dst,dst_len);

		n = snprintf(dst,dst_len,"Instructions per taken branch:  %5.2f\n",((float)num_inst_all_cores)/num_branches_all_cores);
		updateDst(n,dst,dst_len);

		if (srcBits > 0) {
			n = snprintf(dst,dst_len,"Src bits %% of message:          %5.2f%%\n",((float)srcBits*num_trace_msgs_all_cores)/num_trace_bits_all_cores*100.0);
			updateDst(n,dst,dst_len);
		}

		if (have_ts != 0 || 1) {
			int bits_ts = 0;

			for (int i = 0; i < DQR_MAXCORES; i++) {
				if (cores & (1<<i)) {
					bits_ts += core[i].trace_bits_ts;
				}
			}
			n = snprintf(dst,dst_len,"Timestamp bits %% of message:    %5.2f%%\n",((float)bits_ts)/num_trace_bits_all_cores*100.0);
			updateDst(n,dst,dst_len);
		}
	}
	else if (detailLevel > 1) {
		int position;
		int tabs[] = {19+21*0,19+21*1,19+21*2,19+21*3,19+21*4,19+21*5,19+21*6,19+21*7,19+21*8};
		uint32_t t1, t2;
		int ts;

		n = sprintf(tmp_dst,"\n");
		n += sprintf(tmp_dst+n,"                 ");

		for (int i = 0; i < DQR_MAXCORES; i++) {
			if (cores & (1<<i)) {
				n += sprintf(tmp_dst+n,"          Core %d",i);
			}
		}

		if (srcBits > 0) {
			n += sprintf(tmp_dst+n,"               Total");
		}

		n += sprintf(tmp_dst+n,"\n");

		n = snprintf(dst,dst_len,"%s",tmp_dst);
		updateDst(n,dst,dst_len);

		position = sprintf(tmp_dst,"Instructions");

		t1 = 0;
		ts = 0;

		for (int i = 0; i < DQR_MAXCORES; i++) {
			if (cores & (1<<i)) {
				while (position < tabs[ts]) { position += sprintf(tmp_dst+position," "); }
				position += sprintf(tmp_dst+position,"%10u",core[i].num_inst);
				t1 += core[i].num_inst;
				ts += 1;
			}
		}

		if (srcBits > 0) {
			while (position < tabs[ts]) { position += sprintf(tmp_dst+position," "); }
			position += sprintf(tmp_dst+position,"%10u",t1);
		}

		n = snprintf(dst,dst_len,"%s\n",tmp_dst);
		updateDst(n,dst,dst_len);

		position = sprintf(tmp_dst,"  Compressed");

		t2 = 0;
		ts = 0;

		for (int i = 0; i < DQR_MAXCORES; i++) {
			if (cores & (1<<i)) {
				while (position < tabs[ts]) { position += sprintf(tmp_dst+position," "); }
				position += sprintf(tmp_dst+position,"%10u (%0.2f%%)",core[i].num_inst16,((float)core[i].num_inst16)/core[i].num_inst*100.0);
				t2 += core[i].num_inst16;
				ts += 1;
			}
		}

		if (srcBits > 0) {
			while (position < tabs[ts]) { position += sprintf(tmp_dst+position," "); }
			position += sprintf(tmp_dst+position,"%10u (%0.2f%%)",t2,((float)t2)/t1*100.0);
		}

		n = snprintf(dst,dst_len,"%s\n",tmp_dst);
		updateDst(n,dst,dst_len);

		position = sprintf(tmp_dst,"  RV32");

		t2 = 0;
		ts = 0;

		for (int i = 0; i < DQR_MAXCORES; i++) {
			if (cores & (1<<i)) {
				while (position < tabs[ts]) { position += sprintf(tmp_dst+position," "); }
				position += sprintf(tmp_dst+position,"%10u (%0.2f%%)",core[i].num_inst32,((float)core[i].num_inst32)/core[i].num_inst*100.0);
				t2 += core[i].num_inst32;
				ts += 1;
			}
		}

		if (srcBits > 0) {
			while (position < tabs[ts]) { position += sprintf(tmp_dst+position," "); }
			position += sprintf(tmp_dst+position,"%10u (%0.2f%%)",t2,((float)t2)/t1*100.0);
		}

		n = snprintf(dst,dst_len,"%s\n",tmp_dst);
		updateDst(n,dst,dst_len);

		position = sprintf(tmp_dst,"Trace Msgs");

		t1 = 0;
		ts = 0;

		for (int i = 0; i < DQR_MAXCORES; i++) {
			if (cores & (1<<i)) {
				while (position < tabs[ts]) { position += sprintf(tmp_dst+position," "); }
				position += sprintf(tmp_dst+position,"%10u",core[i].num_trace_msgs);
				t1 += core[i].num_trace_msgs;
				ts += 1;
			}
		}

		if (srcBits > 0) {
			while (position < tabs[ts]) { position += printf(" "); }
			position += sprintf(tmp_dst+position,"%10u",t1);
		}

		n = snprintf(dst,dst_len,"%s\n",tmp_dst);
		updateDst(n,dst,dst_len);

		position = sprintf(tmp_dst,"  Sync");

		t2 = 0;
		ts = 0;

		for (int i = 0; i < DQR_MAXCORES; i++) {
			if (cores & (1<<i)) {
				while (position < tabs[ts]) { position += sprintf(tmp_dst+position," "); }
				position += sprintf(tmp_dst+position,"%10u (%0.2f%%)",core[i].num_trace_syncs,((float)core[i].num_trace_syncs)/core[i].num_trace_msgs*100.0);
				t2 += core[i].num_trace_syncs;
				ts += 1;
			}
		}

		if (srcBits > 0) {
			while (position < tabs[ts]) { position += sprintf(tmp_dst+position," "); }
			position += sprintf(tmp_dst+position,"%10u (%0.2f%%)",t2,((float)t2)/t1*100.0);
		}

		n = snprintf(dst,dst_len,"%s\n",tmp_dst);
		updateDst(n,dst,dst_len);

		position = sprintf(tmp_dst,"  DBranch");

		t2 = 0;
		ts = 0;

		for (int i = 0; i < DQR_MAXCORES; i++) {
			if (cores & (1<<i)) {
				while (position < tabs[ts]) { position += sprintf(tmp_dst+position," "); }
				position += sprintf(tmp_dst+position,"%10u (%0.2f%%)",core[i].num_trace_dbranch,((float)core[i].num_trace_dbranch)/core[i].num_trace_msgs*100.0);
				t2 += core[i].num_trace_dbranch;
				ts += 1;
			}
		}

		if (srcBits > 0) {
			while (position < tabs[ts]) { position += sprintf(tmp_dst+position," "); }
			position += sprintf(tmp_dst+position,"%10u (%0.2f%%)",t2,((float)t2)/t1*100.0);
		}

		n = snprintf(dst,dst_len,"%s\n",tmp_dst);
		updateDst(n,dst,dst_len);

		position = sprintf(tmp_dst,"  IBranch");

		ts = 0;
		t2 = 0;

		for (int i = 0; i < DQR_MAXCORES; i++) {
			if (cores & (1<<i)) {
				while (position < tabs[ts]) { position += sprintf(tmp_dst+position," "); }
				position += sprintf(tmp_dst+position,"%10u (%0.2f%%)",core[i].num_trace_ibranch,((float)core[i].num_trace_ibranch)/core[i].num_trace_msgs*100.0);
				t2 += core[i].num_trace_ibranch;
				ts += 1;
			}
		}

		if (srcBits > 0) {
			while (position < tabs[ts]) { position += sprintf(tmp_dst+position," "); }
			position += sprintf(tmp_dst+position,"%10u (%0.2f%%)",t2,((float)t2)/t1*100.0);
		}

		n = snprintf(dst,dst_len,"%s\n",tmp_dst);
		updateDst(n,dst,dst_len);

		position = sprintf(tmp_dst,"  DBranch WS");

		ts = 0;
		t2 = 0;

		for (int i = 0; i < DQR_MAXCORES; i++) {
			if (cores & (1<<i)) {
				while (position < tabs[ts]) { position += sprintf(tmp_dst+position," "); }
				position += sprintf(tmp_dst+position,"%10u (%0.2f%%)",core[i].num_trace_dbranchws,((float)core[i].num_trace_dbranchws)/core[i].num_trace_msgs*100.0);
				t2 += core[i].num_trace_dbranchws;
				ts += 1;
			}
		}

		if (srcBits > 0) {
			while (position < tabs[ts]) { position += sprintf(tmp_dst+position," "); }
			position += sprintf(tmp_dst+position,"%10u (%0.2f%%)",t2,((float)t2)/t1*100.0);
		}

		n = snprintf(dst,dst_len,"%s\n",tmp_dst);
		updateDst(n,dst,dst_len);

		position = sprintf(tmp_dst,"  IBranch WS");

		ts = 0;
		t2 = 0;

		for (int i = 0; i < DQR_MAXCORES; i++) {
			if (cores & (1<<i)) {
				while (position < tabs[ts]) { position += sprintf(tmp_dst+position," "); }
				position += sprintf(tmp_dst+position,"%10u (%0.2f%%)",core[i].num_trace_ibranchws,((float)core[i].num_trace_ibranchws)/core[i].num_trace_msgs*100.0);
				t2 += core[i].num_trace_ibranchws;
				ts += 1;
			}
		}

		if (srcBits > 0) {
			while (position < tabs[ts]) { position += sprintf(tmp_dst+position," "); }
			position += sprintf(tmp_dst+position,"%10u (%0.2f%%)",t2,((float)t2)/t1*100.0);
		}

		n = snprintf(dst,dst_len,"%s\n",tmp_dst);
		updateDst(n,dst,dst_len);

		position = sprintf(tmp_dst,"  Data Acq");

		ts = 0;
		t2 = 0;

		for (int i = 0; i < DQR_MAXCORES; i++) {
			if (cores & (1<<i)) {
				while (position < tabs[ts]) { position += sprintf(tmp_dst+position," "); }
				position += sprintf(tmp_dst+position,"%10u (%0.2f%%)",core[i].num_trace_dataacq,((float)core[i].num_trace_dataacq)/core[i].num_trace_msgs*100.0);
				t2 += core[i].num_trace_dataacq;
				ts += 1;
			}
		}

		if (srcBits > 0) {
			while (position < tabs[ts]) { position += sprintf(tmp_dst+position," "); }
			position += sprintf(tmp_dst+position,"%10u (%0.2f%%)",t2,((float)t2)/t1*100.0);
		}

		n = snprintf(dst,dst_len,"%s\n",tmp_dst);
		updateDst(n,dst,dst_len);

		position = sprintf(tmp_dst,"  Correlation");

		ts = 0;
		t2 = 0;

		for (int i = 0; i < DQR_MAXCORES; i++) {
			if (cores & (1<<i)) {
				while (position < tabs[ts]) { position += sprintf(tmp_dst+position," "); }
				position += sprintf(tmp_dst+position,"%10u (%0.2f%%)",core[i].num_trace_correlation,((float)core[i].num_trace_correlation)/core[i].num_trace_msgs*100.0);
				t2 += core[i].num_trace_correlation;
				ts += 1;
			}
		}

		if (srcBits > 0) {
			while (position < tabs[ts]) { position += sprintf(tmp_dst+position," "); }
			position += sprintf(tmp_dst+position,"%10u (%0.2f%%)",t2,((float)t2)/t1*100.0);
		}

		n = snprintf(dst,dst_len,"%s\n",tmp_dst);
		updateDst(n,dst,dst_len);

		position = sprintf(tmp_dst,"  Aux Acc Write");

		ts = 0;
		t2 = 0;

		for (int i = 0; i < DQR_MAXCORES; i++) {
			if (cores & (1<<i)) {
				while (position < tabs[ts]) { position += sprintf(tmp_dst+position," "); }
				position += sprintf(tmp_dst+position,"%10u (%0.2f%%)",core[i].num_trace_auxaccesswrite,((float)core[i].num_trace_auxaccesswrite)/core[i].num_trace_msgs*100.0);
				t2 += core[i].num_trace_auxaccesswrite;
				ts += 1;
			}
		}

		if (srcBits > 0) {
			while (position < tabs[ts]) { position += sprintf(tmp_dst+position," "); }
			position += sprintf(tmp_dst+position,"%10u (%0.2f%%)",t2,((float)t2)/t1*100.0);
		}

		n = snprintf(dst,dst_len,"%s\n",tmp_dst);
		updateDst(n,dst,dst_len);

		position = sprintf(tmp_dst,"  Ownership");

		ts = 0;
		t2 = 0;

		for (int i = 0; i < DQR_MAXCORES; i++) {
			if (cores & (1<<i)) {
				while (position < tabs[ts]) { position += sprintf(tmp_dst+position," "); }
				position += sprintf(tmp_dst+position,"%10u (%0.2f%%)",core[i].num_trace_ownership,((float)core[i].num_trace_ownership)/core[i].num_trace_msgs*100.0);
				t2 += core[i].num_trace_ownership;
				ts += 1;
			}
		}

		if (srcBits > 0) {
			while (position < tabs[ts]) { position += sprintf(tmp_dst+position," "); }
			position += sprintf(tmp_dst+position,"%10u (%0.2f%%)",t2,((float)t2)/t1*100.0);
		}

		n = snprintf(dst,dst_len,"%s\n",tmp_dst);
		updateDst(n,dst,dst_len);

		position = sprintf(tmp_dst,"  Error");

		ts = 0;
		t2 = 0;

		for (int i = 0; i < DQR_MAXCORES; i++) {
			if (cores & (1<<i)) {
				while (position < tabs[ts]) { position += sprintf(tmp_dst+position," "); }
				position += sprintf(tmp_dst+position,"%10u (%0.2f%%)",core[i].num_trace_error,((float)core[i].num_trace_error)/core[i].num_trace_msgs*100.0);
				t2 += core[i].num_trace_error;
				ts += 1;
			}
		}

		if (srcBits > 0) {
			while (position < tabs[ts]) { position += sprintf(tmp_dst+position," "); }
			position += sprintf(tmp_dst+position,"%10u (%0.2f%%)",t2,((float)t2)/t1*100.0);
		}

		n = snprintf(dst,dst_len,"%s\n",tmp_dst);
		updateDst(n,dst,dst_len);

		position = sprintf(tmp_dst,"  IHistory");

		ts = 0;
		t2 = 0;

		for (int i = 0; i < DQR_MAXCORES; i++) {
			if (cores & (1<<i)) {
				while (position < tabs[ts]) { position += sprintf(tmp_dst+position," "); }
				position += sprintf(tmp_dst+position,"%10u (%0.2f%%)",core[i].num_trace_ihistory,((float)core[i].num_trace_ihistory)/core[i].num_trace_msgs*100.0);
				t2 += core[i].num_trace_ihistory;
				ts += 1;
			}
		}

		if (srcBits > 0) {
			while (position < tabs[ts]) { position += sprintf(tmp_dst+position," "); }
			position += sprintf(tmp_dst+position,"%10u (%0.2f%%)",t2,((float)t2)/t1*100.0);
		}

		n = snprintf(dst,dst_len,"%s\n",tmp_dst);
		updateDst(n,dst,dst_len);

		position = sprintf(tmp_dst,"  IHistory WS");

		ts = 0;
		t2 = 0;

		for (int i = 0; i < DQR_MAXCORES; i++) {
			if (cores & (1<<i)) {
				while (position < tabs[ts]) { position += sprintf(tmp_dst+position," "); }
				position += sprintf(tmp_dst+position,"%10u (%0.2f%%)",core[i].num_trace_ihistoryws,((float)core[i].num_trace_ihistoryws)/core[i].num_trace_msgs*100.0);
				t2 += core[i].num_trace_ihistoryws;
				ts += 1;
			}
		}

		if (srcBits > 0) {
			while (position < tabs[ts]) { position += sprintf(tmp_dst+position," "); }
			position += sprintf(tmp_dst+position,"%10u (%0.2f%%)",t2,((float)t2)/t1*100.0);
		}

		n = snprintf(dst,dst_len,"%s\n",tmp_dst);
		updateDst(n,dst,dst_len);

		position = sprintf(tmp_dst,"  RFull ICNT");

		ts = 0;
		t2 = 0;

		for (int i = 0; i < DQR_MAXCORES; i++) {
			if (cores & (1<<i)) {
				while (position < tabs[ts]) { position += sprintf(tmp_dst+position," "); }
				position += sprintf(tmp_dst+position,"%10u (%0.2f%%)",core[i].num_trace_resourcefull_i_cnt,((float)core[i].num_trace_resourcefull_i_cnt)/core[i].num_trace_msgs*100.0);
				t2 += core[i].num_trace_resourcefull_i_cnt;
				ts += 1;
			}
		}

		if (srcBits > 0) {
			while (position < tabs[ts]) { position += sprintf(tmp_dst+position," "); }
			position += sprintf(tmp_dst+position,"%10u (%0.2f%%)",t2,((float)t2)/t1*100.0);
		}

		n = snprintf(dst,dst_len,"%s\n",tmp_dst);
		updateDst(n,dst,dst_len);

		position = sprintf(tmp_dst,"  RFull HIST");

		ts = 0;
		t2 = 0;

		for (int i = 0; i < DQR_MAXCORES; i++) {
			if (cores & (1<<i)) {
				while (position < tabs[ts]) { position += sprintf(tmp_dst+position," "); }
				position += sprintf(tmp_dst+position,"%10u (%0.2f%%)",core[i].num_trace_resourcefull_hist,((float)core[i].num_trace_resourcefull_hist)/core[i].num_trace_msgs*100.0);
				t2 += core[i].num_trace_resourcefull_hist;
				ts += 1;
			}
		}

		if (srcBits > 0) {
			while (position < tabs[ts]) { position += sprintf(tmp_dst+position," "); }
			position += sprintf(tmp_dst+position,"%10u (%0.2f%%)",t2,((float)t2)/t1*100.0);
		}

		n = snprintf(dst,dst_len,"%s\n",tmp_dst);
		updateDst(n,dst,dst_len);

		position = sprintf(tmp_dst,"  RFull Taken");

		ts = 0;
		t2 = 0;

		for (int i = 0; i < DQR_MAXCORES; i++) {
			if (cores & (1<<i)) {
				while (position < tabs[ts]) { position += sprintf(tmp_dst+position," "); }
				position += sprintf(tmp_dst+position,"%10u (%0.2f%%)",core[i].num_trace_resourcefull_takenCount,((float)core[i].num_trace_resourcefull_takenCount)/core[i].num_trace_msgs*100.0);
				t2 += core[i].num_trace_resourcefull_takenCount;
				ts += 1;
			}
		}

		if (srcBits > 0) {
			while (position < tabs[ts]) { position += sprintf(tmp_dst+position," "); }
			position += sprintf(tmp_dst+position,"%10u (%0.2f%%)",t2,((float)t2)/t1*100.0);
		}

		n = snprintf(dst,dst_len,"%s\n",tmp_dst);
		updateDst(n,dst,dst_len);

		position = sprintf(tmp_dst,"  RFull NTaken");

		ts = 0;
		t2 = 0;

		for (int i = 0; i < DQR_MAXCORES; i++) {
			if (cores & (1<<i)) {
				while (position < tabs[ts]) { position += sprintf(tmp_dst+position," "); }
				position += sprintf(tmp_dst+position,"%10u (%0.2f%%)",core[i].num_trace_resourcefull_notTakenCount,((float)core[i].num_trace_resourcefull_notTakenCount)/core[i].num_trace_msgs*100.0);
				t2 += core[i].num_trace_resourcefull_notTakenCount;
				ts += 1;
			}
		}

		if (srcBits > 0) {
			while (position < tabs[ts]) { position += sprintf(tmp_dst+position," "); }
			position += sprintf(tmp_dst+position,"%10u (%0.2f%%)",t2,((float)t2)/t1*100.0);
		}

		n = snprintf(dst,dst_len,"%s\n",tmp_dst);
		updateDst(n,dst,dst_len);

		position = sprintf(tmp_dst,"  ICT WS");

		ts = 0;
		t2 = 0;

		for (int i = 0; i < DQR_MAXCORES; i++) {
			if (cores & (1<<i)) {
				while (position < tabs[ts]) { position += sprintf(tmp_dst+position," "); }
				position += sprintf(tmp_dst+position,"%10u (%0.2f%%)",core[i].num_trace_incircuittraceWS,((float)core[i].num_trace_incircuittraceWS)/core[i].num_trace_msgs*100.0);
				t2 += core[i].num_trace_ihistoryws;
				ts += 1;
			}
		}

		if (srcBits > 0) {
			while (position < tabs[ts]) { position += sprintf(tmp_dst+position," "); }
			position += sprintf(tmp_dst+position,"%10u (%0.2f%%)",t2,((float)t2)/t1*100.0);
		}

		n = snprintf(dst,dst_len,"%s\n",tmp_dst);
		updateDst(n,dst,dst_len);

		position = sprintf(tmp_dst,"  ICT");

		ts = 0;
		t2 = 0;

		for (int i = 0; i < DQR_MAXCORES; i++) {
			if (cores & (1<<i)) {
				while (position < tabs[ts]) { position += sprintf(tmp_dst+position," "); }
				position += sprintf(tmp_dst+position,"%10u (%0.2f%%)",core[i].num_trace_incircuittrace,((float)core[i].num_trace_incircuittrace)/core[i].num_trace_msgs*100.0);
				t2 += core[i].num_trace_ihistoryws;
				ts += 1;
			}
		}

		if (srcBits > 0) {
			while (position < tabs[ts]) { position += sprintf(tmp_dst+position," "); }
			position += sprintf(tmp_dst+position,"%10u (%0.2f%%)",t2,((float)t2)/t1*100.0);
		}

		n = snprintf(dst,dst_len,"%s\n",tmp_dst);
		updateDst(n,dst,dst_len);

		position = sprintf(tmp_dst,"Trace Bits Total");

		ts = 0;
		t1 = 0;

		for (int i = 0; i < DQR_MAXCORES; i++) {
			if (cores & (1<<i)) {
				while (position < tabs[ts]) { position += sprintf(tmp_dst+position," "); }
				position += sprintf(tmp_dst+position,"%10u",core[i].trace_bits);
				t1 += core[i].trace_bits;
				ts += 1;
			}
		}

		if (srcBits > 0) {
			while (position < tabs[ts]) { position += sprintf(tmp_dst+position," "); }
			position += sprintf(tmp_dst+position,"%10u",t1);
		}

//		printf("\n");
//		position = printf("Trace Bits MSEO");
//
//		ts = 0;
//		t1 = 0;
//
//		for (int i = 0; i < DQR_MAXCORES; i++) {
//			if (cores & (1<<i)) {
//				while (position < tabs[ts]) { position += printf(" "); }
//				position += printf("%10u",core[i].trace_bits_mseo);
//				t1 += core[i].trace_bits_mseo;
//				ts += 1;
//			}
//		}
//
//		if (srcBits > 0) {
//			while (position < tabs[ts]) { position += printf(" "); }
//			printf("%10u",t1);
//		}

		n = snprintf(dst,dst_len,"%s\n",tmp_dst);
		updateDst(n,dst,dst_len);

		position = sprintf(tmp_dst,"Trace Bits/Inst");

		ts = 0;
		t1 = 0;
		t2 = 0;

		for (int i = 0; i < DQR_MAXCORES; i++) {
			if (cores & (1<<i)) {
				while (position < tabs[ts]) { position += sprintf(tmp_dst+position," "); }
				if (core[i].num_inst != 0) {
					position += sprintf(tmp_dst+position,"%13.2f",((float)core[i].trace_bits)/core[i].num_inst);
				}
				else {
					position += sprintf(tmp_dst+position,"          -");
				}
				t1 += core[i].trace_bits;
				t2 += core[i].num_inst;
				ts += 1;
			}
		}

		if (srcBits > 0) {
			while (position < tabs[ts]) { position += sprintf(tmp_dst+position," "); }
			if (t2 != 0) {
				position += sprintf(tmp_dst+position,"%13.2f",((float)t1)/t2);
			}
			else {
				position += sprintf(tmp_dst+position,"          -");
			}
		}

		n = snprintf(dst,dst_len,"%s\n",tmp_dst);
		updateDst(n,dst,dst_len);

		position = sprintf(tmp_dst,"Inst/Trace Msg");

		ts = 0;
		t1 = 0;

		for (int i = 0; i < DQR_MAXCORES; i++) {
			if (cores & (1<<i)) {
				while (position < tabs[ts]) { position += sprintf(tmp_dst+position," "); }
				position += sprintf(tmp_dst+position,"%13.2f",((float)core[i].num_inst)/core[i].num_trace_msgs);
				t1 += core[i].num_trace_msgs;
				ts += 1;
			}
		}

		if (srcBits > 0) {
			while (position < tabs[ts]) { position += sprintf(tmp_dst+position," "); }
			position += sprintf(tmp_dst+position,"%13.2f",((float)t2)/t1);
		}

		n = snprintf(dst,dst_len,"%s\n",tmp_dst);
		updateDst(n,dst,dst_len);

		position = sprintf(tmp_dst,"Inst/Taken Branch");

		ts = 0;
		t1 = 0;

		for (int i = 0; i < DQR_MAXCORES; i++) {
			if (cores & (1<<i)) {
				while (position < tabs[ts]) { position += sprintf(tmp_dst+position," "); }
				position += sprintf(tmp_dst+position,"%13.2f",((float)core[i].num_inst)/(core[i].num_trace_dbranch+core[i].num_trace_ibranch+core[i].num_trace_dbranchws+core[i].num_trace_ibranchws+core[i].num_trace_ihistory_taken_branches+core[i].num_trace_resourcefull_taken_branches));
				t1 += core[i].num_trace_dbranch+core[i].num_trace_ibranch+core[i].num_trace_dbranchws+core[i].num_trace_ibranchws;
				ts += 1;
			}
		}

		if (srcBits > 0) {
			while (position < tabs[ts]) { position += sprintf(tmp_dst+position," "); }
			position += sprintf(tmp_dst+position,"%13.2f",((float)t2)/t1);
		}

		n = snprintf(dst,dst_len,"%s\n",tmp_dst);
		updateDst(n,dst,dst_len);

		position = sprintf(tmp_dst,"Avg Msg Length");

		ts = 0;
		t1 = 0;
		t2 = 0;

		for (int i = 0; i < DQR_MAXCORES; i++) {
			if (cores & (1<<i)) {
				while (position < tabs[ts]) { position += sprintf(tmp_dst+position," "); }
				position += sprintf(tmp_dst+position,"%13.2f",((float)core[i].trace_bits)/core[i].num_trace_msgs);
				t1 += core[i].trace_bits;
				t2 += core[i].num_trace_msgs;
				ts += 1;
			}
		}

		if (srcBits > 0) {
			while (position < tabs[ts]) { position += sprintf(tmp_dst+position," "); }
			position += sprintf(tmp_dst+position,"%13.2f",((float)t1)/t2);
		}

		n = snprintf(dst,dst_len,"%s\n",tmp_dst);
		updateDst(n,dst,dst_len);

		position = sprintf(tmp_dst,"Min Msg Length");

		ts = 0;
		t1 = 0;

		for (int i = 0; i < DQR_MAXCORES; i++) {
			if (cores & (1<<i)) {
				while (position < tabs[ts]) { position += sprintf(tmp_dst+position," "); }
				position += sprintf(tmp_dst+position,"%10u",core[i].trace_bits_min);
				if ((t1 == 0) || (core[i].trace_bits_min < t1)) {
					t1 = core[i].trace_bits_min;
				}
				ts += 1;
			}
		}

		if (srcBits > 0) {
			while (position < tabs[ts]) { position += sprintf(tmp_dst+position," "); }
			position += sprintf(tmp_dst+position,"%10u",t1);
		}

		n = snprintf(dst,dst_len,"%s\n",tmp_dst);
		updateDst(n,dst,dst_len);

		position = sprintf(tmp_dst,"Max Msg Length");

		ts = 0;
		t1 = 0;

		for (int i = 0; i < DQR_MAXCORES; i++) {
			if (cores & (1<<i)) {
				while (position < tabs[ts]) { position += sprintf(tmp_dst+position," "); }
				position += sprintf(tmp_dst+position,"%10u",core[i].trace_bits_max);
				if (core[i].trace_bits_max > t1) {
					t1 = core[i].trace_bits_max;
				}
				ts += 1;
			}
		}

		if (srcBits > 0) {
			while (position < tabs[ts]) { position += sprintf(tmp_dst+position," "); }
			position += sprintf(tmp_dst+position,"%10u",t1);
		}

		n = snprintf(dst,dst_len,"%s\n",tmp_dst);
		updateDst(n,dst,dst_len);

		position = sprintf(tmp_dst,"Timestamp Counts");

		ts = 0;
		t1 = 0;

		for (int i = 0; i < DQR_MAXCORES; i++) {
			if (cores & (1<<i)) {
				while (position < tabs[ts]) { position += sprintf(tmp_dst+position," "); }
				position += sprintf(tmp_dst+position,"%10u",core[i].num_trace_ts);
				t1 += core[i].num_trace_ts;
				ts += 1;
			}
		}

		if (srcBits > 0) {
			while (position < tabs[ts]) { position += sprintf(tmp_dst+position," "); }
			position += sprintf(tmp_dst+position,"%10u",t1);
		}

		n = snprintf(dst,dst_len,"%s\n",tmp_dst);
		updateDst(n,dst,dst_len);

		position = sprintf(tmp_dst,"  TStamp Size Avg");

		ts = 0;
		t2 = 0;

		for (int i = 0; i < DQR_MAXCORES; i++) {
			if (cores & (1<<i)) {
				while (position < tabs[ts]) { position += sprintf(tmp_dst+position," "); }
				if (core[i].num_trace_ts > 0) {
					position += sprintf(tmp_dst+position,"%13.2f",core[i].trace_bits_ts/core[i].num_trace_ts);
					t2 += core[i].trace_bits_ts;
				}
				else {
					position += sprintf(tmp_dst+position,"         0");
				}
				ts += 1;
			}
		}

		if (srcBits > 0) {
			while (position < tabs[ts]) { position += sprintf(tmp_dst+position," "); }
			if (t1 > 0) {
				position += sprintf(tmp_dst+position,"%13.2f",((float)t2)/t1);
			}
			else {
				position += sprintf(tmp_dst+position,"         0");
			}
		}

		n = snprintf(dst,dst_len,"%s\n",tmp_dst);
		updateDst(n,dst,dst_len);

		position = sprintf(tmp_dst,"  TStamp Size Min");

		ts = 0;
		t1 = 0;

		for (int i = 0; i < DQR_MAXCORES; i++) {
			if (cores & (1<<i)) {
				while (position < tabs[ts]) { position += sprintf(tmp_dst+position," "); }
				position += sprintf(tmp_dst+position,"%10u",core[i].trace_bits_ts_min);
				if ((t1 == 0) || (t1 > core[i].trace_bits_ts_min)) {
					t1 = core[i].trace_bits_ts_min;
				}
				ts += 1;
			}
		}

		if (srcBits > 0) {
			while (position < tabs[ts]) { position += sprintf(tmp_dst+position," "); }
			position += sprintf(tmp_dst+position,"%10u",t1);
		}

		n = snprintf(dst,dst_len,"%s\n",tmp_dst);
		updateDst(n,dst,dst_len);

		position = sprintf(tmp_dst,"  TStamp Size Max");

		ts = 0;
		t1 = 0;

		for (int i = 0; i < DQR_MAXCORES; i++) {
			if (cores & (1<<i)) {
				while (position < tabs[ts]) { position += sprintf(tmp_dst+position," "); }
				position += sprintf(tmp_dst+position,"%10u",core[i].trace_bits_ts_max);
				if (core[i].trace_bits_ts_max > t1) {
					t1 = core[i].trace_bits_ts_max;
				}
				ts += 1;
			}
		}

		if (srcBits > 0) {
			while (position < tabs[ts]) { position += sprintf(tmp_dst+position," "); }
			position += sprintf(tmp_dst+position,"%10u",t1);
		}

		n = snprintf(dst,dst_len,"%s\n",tmp_dst);
		updateDst(n,dst,dst_len);

		position = sprintf(tmp_dst,"Timestamp %% of Msg");

		ts = 0;
		t1 = 0;

		for (int i = 0; i < DQR_MAXCORES; i++) {
			if (cores & (1<<i)) {
				while (position < tabs[ts]) { position += sprintf(tmp_dst+position," "); }
				position += sprintf(tmp_dst+position,"%13.2f%%",((float)core[i].trace_bits_ts)/core[i].trace_bits*100.0);
				t1 += core[i].trace_bits;
				ts += 1;
			}
		}

		if (srcBits > 0) {
			while (position < tabs[ts]) { position += sprintf(tmp_dst+position," "); }
			position += sprintf(tmp_dst+position,"%13.2f%%",((float)t2)/t1*100.0);
		}

		n = snprintf(dst,dst_len,"%s\n",tmp_dst);
		updateDst(n,dst,dst_len);

		position = sprintf(tmp_dst,"UADDR Counts");

		ts = 0;
		t1 = 0;

		for (int i = 0; i < DQR_MAXCORES; i++) {
			if (cores & (1<<i)) {
				while (position < tabs[ts]) { position += sprintf(tmp_dst+position," "); }
				position += sprintf(tmp_dst+position,"%10u",core[i].num_trace_uaddr);
				t1 += core[i].num_trace_uaddr;
				ts += 1;
			}
		}

		if (srcBits > 0) {
			while (position < tabs[ts]) { position += sprintf(tmp_dst+position," "); }
			position += sprintf(tmp_dst+position,"%10u",t1);
		}

		n = snprintf(dst,dst_len,"%s\n",tmp_dst);
		updateDst(n,dst,dst_len);

		position = sprintf(tmp_dst,"  UADDR Size Avg");

		ts = 0;
		t2 = 0;

		for (int i = 0; i < DQR_MAXCORES; i++) {
			if (cores & (1<<i)) {
				while (position < tabs[ts]) { position += sprintf(tmp_dst+position," "); }
				if (core[i].num_trace_uaddr > 0) {
					position += sprintf(tmp_dst+position,"%13.2f",((float)core[i].trace_bits_uaddr)/core[i].num_trace_uaddr);
					t2 += core[i].trace_bits_uaddr;
				}
				else {
					position += sprintf(tmp_dst+position,"        0");
				}
				ts += 1;
			}
		}

		if (srcBits > 0) {
			while (position < tabs[ts]) { position += sprintf(tmp_dst+position," "); }
			if (t1 > 0) {
				position += sprintf(tmp_dst+position,"%13.2f",((float)t2)/t1);
			}
			else {
				position += sprintf(tmp_dst+position,"        0");
			}
		}

		n = snprintf(dst,dst_len,"%s\n",tmp_dst);
		updateDst(n,dst,dst_len);

		position = sprintf(tmp_dst,"  UADDR Size Min");

		ts = 0;
		t1 = 0;

		for (int i = 0; i < DQR_MAXCORES; i++) {
			if (cores & (1<<i)) {
				while (position < tabs[ts]) { position += sprintf(tmp_dst+position," "); }
				position += sprintf(tmp_dst+position,"%10u",core[i].trace_bits_uaddr_min);
				if ((t1 == 0) || (core[i].trace_bits_uaddr_min < t1)) {
					t1 = core[i].trace_bits_uaddr_min;
				}
				ts += 1;
			}
		}

		if (srcBits > 0) {
			while (position < tabs[ts]) { position += sprintf(tmp_dst+position," "); }
			position += sprintf(tmp_dst+position,"%10u",t1);
		}

		n = snprintf(dst,dst_len,"%s\n",tmp_dst);
		updateDst(n,dst,dst_len);

		position = sprintf(tmp_dst,"  UADDR Size Max");

		ts = 0;
		t1 = 0;

		for (int i = 0; i < DQR_MAXCORES; i++) {
			if (cores & (1<<i)) {
				while (position < tabs[ts]) { position += sprintf(tmp_dst+position," "); }
				position += sprintf(tmp_dst+position,"%10u",core[i].trace_bits_uaddr_max);
				if (t1 < core[i].trace_bits_uaddr_max) {
					t1 = core[i].trace_bits_uaddr_max;
				}
				ts += 1;
			}
		}

		if (srcBits > 0) {
			while (position < tabs[ts]) { position += sprintf(tmp_dst+position," "); }
			position += sprintf(tmp_dst+position,"%10u",t1);
		}

		n = snprintf(dst,dst_len,"%s\n",tmp_dst);
		updateDst(n,dst,dst_len);

		position = sprintf(tmp_dst,"FADDR Counts");

		ts = 0;
		t1 = 0;

		for (int i = 0; i < DQR_MAXCORES; i++) {
			if (cores & (1<<i)) {
				while (position < tabs[ts]) { position += sprintf(tmp_dst+position," "); }
				position += sprintf(tmp_dst+position,"%10u",core[i].num_trace_faddr);
				t1 += core[i].num_trace_faddr;
				ts += 1;
			}
		}

		if (srcBits > 0) {
			while (position < tabs[ts]) { position += sprintf(tmp_dst+position," "); }
			position += sprintf(tmp_dst+position,"%10u",t1);
		}

		n = snprintf(dst,dst_len,"%s\n",tmp_dst);
		updateDst(n,dst,dst_len);

		position = sprintf(tmp_dst,"  FADDR Size Avg");

		ts = 0;
		t2 = 0;

		for (int i = 0; i < DQR_MAXCORES; i++) {
			if (cores & (1<<i)) {
				while (position < tabs[ts]) { position += sprintf(tmp_dst+position," "); }
				if (core[i].num_trace_faddr > 0) {
					position += sprintf(tmp_dst+position,"%13.2f",((float)core[i].trace_bits_faddr)/core[i].num_trace_faddr);
					t2 = core[i].trace_bits_faddr;
				}
				else {
					position += sprintf(tmp_dst+position,"        0");
				}
				ts += 1;
			}
		}

		if (srcBits > 0) {
			while (position < tabs[ts]) { position += sprintf(tmp_dst+position," "); }
			if (t1 > 0) {
				position += sprintf(tmp_dst+position,"%13.2f",((float)t2)/t1);
			}
			else {
				position += sprintf(tmp_dst+position,"        0");
			}
		}

		n = snprintf(dst,dst_len,"%s\n",tmp_dst);
		updateDst(n,dst,dst_len);

		position = sprintf(tmp_dst,"  FADDR Size Min");

		ts = 0;
		t2 = 0;

		for (int i = 0; i < DQR_MAXCORES; i++) {
			if (cores & (1<<i)) {
				while (position < tabs[ts]) { position += sprintf(tmp_dst+position," "); }
				position += sprintf(tmp_dst+position,"%10u",core[i].trace_bits_faddr_min);
				if ((t2 == 0) || (core[i].trace_bits_faddr_min < t2)) {
					t2 = core[i].trace_bits_faddr_min;
				}
				ts += 1;
			}
		}

		if (srcBits > 0) {
			while (position < tabs[ts]) { position += sprintf(tmp_dst+position," "); }
			position += sprintf(tmp_dst+position,"%10u",t2);
		}

		n = snprintf(dst,dst_len,"%s\n",tmp_dst);
		updateDst(n,dst,dst_len);

		position = sprintf(tmp_dst,"  FADDR Size Max");

		ts = 0;
		t2 = 0;

		for (int i = 0; i < DQR_MAXCORES; i++) {
			if (cores & (1<<i)) {
				while (position < tabs[ts]) { position += sprintf(tmp_dst+position," "); }
				position += sprintf(tmp_dst+position,"%10u",core[i].trace_bits_faddr_max);
				if (core[i].trace_bits_faddr_max > t2) {
					t2 = core[i].trace_bits_faddr_max;
				}
				ts += 1;
			}
		}

		if (srcBits > 0) {
			while (position < tabs[ts]) { position += sprintf(tmp_dst+position," "); }
			position += sprintf(tmp_dst+position,"%10u",t2);
		}

		n = snprintf(dst,dst_len,"%s\n",tmp_dst);
		updateDst(n,dst,dst_len);

		position = sprintf(tmp_dst,"Taken Branches");

		ts = 0;
		t2 = 0;

		for (int i = 0; i < DQR_MAXCORES; i++) {
			if (cores & (1<<i)) {
				while (position < tabs[ts]) { position += sprintf(tmp_dst+position," "); }
				position += sprintf(tmp_dst+position,"%10u",core[i].num_taken_branches);
				ts += 1;
			}
		}

		n = snprintf(dst,dst_len,"%s\n",tmp_dst);
		updateDst(n,dst,dst_len);

		position = sprintf(tmp_dst,"Not Taken Branches");

		ts = 0;
		t2 = 0;

		for (int i = 0; i < DQR_MAXCORES; i++) {
			if (cores & (1<<i)) {
				while (position < tabs[ts]) { position += sprintf(tmp_dst+position," "); }
				position += sprintf(tmp_dst+position,"%10u",core[i].num_notTaken_branches);
				ts += 1;
			}
		}

		n = snprintf(dst,dst_len,"%s\n",tmp_dst);
		updateDst(n,dst,dst_len);

		position = sprintf(tmp_dst,"Calls");

		ts = 0;
		t2 = 0;

		for (int i = 0; i < DQR_MAXCORES; i++) {
			if (cores & (1<<i)) {
				while (position < tabs[ts]) { position += sprintf(tmp_dst+position," "); }
				position += sprintf(tmp_dst+position,"%10u",core[i].num_calls);
				ts += 1;
			}
		}

		n = snprintf(dst,dst_len,"%s\n",tmp_dst);
		updateDst(n,dst,dst_len);

		position = sprintf(tmp_dst,"Returns");

		ts = 0;
		t2 = 0;

		for (int i = 0; i < DQR_MAXCORES; i++) {
			if (cores & (1<<i)) {
				while (position < tabs[ts]) { position += sprintf(tmp_dst+position," "); }
				position += sprintf(tmp_dst+position,"%10u",core[i].num_returns);
				ts += 1;
			}
		}

		n = snprintf(dst,dst_len,"%s\n",tmp_dst);
		updateDst(n,dst,dst_len);

		position = sprintf(tmp_dst,"Swaps");

		ts = 0;
		t2 = 0;

		for (int i = 0; i < DQR_MAXCORES; i++) {
			if (cores & (1<<i)) {
				while (position < tabs[ts]) { position += sprintf(tmp_dst+position," "); }
				position += sprintf(tmp_dst+position,"%10u",core[i].num_swaps);
				ts += 1;
			}
		}

		n = snprintf(dst,dst_len,"%s\n",tmp_dst);
		updateDst(n,dst,dst_len);

		position = sprintf(tmp_dst,"Exceptions");

		ts = 0;
		t2 = 0;

		for (int i = 0; i < DQR_MAXCORES; i++) {
			if (cores & (1<<i)) {
				while (position < tabs[ts]) { position += sprintf(tmp_dst+position," "); }
				position += sprintf(tmp_dst+position,"%10u",core[i].num_exceptions);
				ts += 1;
			}
		}

		n = snprintf(dst,dst_len,"%s\n",tmp_dst);
		updateDst(n,dst,dst_len);

		position = sprintf(tmp_dst,"Exception Returns");

		ts = 0;
		t2 = 0;

		for (int i = 0; i < DQR_MAXCORES; i++) {
			if (cores & (1<<i)) {
				while (position < tabs[ts]) { position += sprintf(tmp_dst+position," "); }
				position += sprintf(tmp_dst+position,"%10u",core[i].num_exception_returns);
				ts += 1;
			}
		}

		n = snprintf(dst,dst_len,"%s\n",tmp_dst);
		updateDst(n,dst,dst_len);

		position = sprintf(tmp_dst,"Interrupts");

		ts = 0;
		t2 = 0;

		for (int i = 0; i < DQR_MAXCORES; i++) {
			if (cores & (1<<i)) {
				while (position < tabs[ts]) { position += sprintf(tmp_dst+position," "); }
				position += sprintf(tmp_dst+position,"%10u",core[i].num_interrupts);
				ts += 1;
			}
		}

		n = snprintf(dst,dst_len,"%s\n",tmp_dst);
		updateDst(n,dst,dst_len);
	}

#ifdef DO_TIMES
	n = snprintf(dst,dst_len,"\nTime %f seconds\n",etime);
	updateDst(n,dst,dst_len);
	n = snprintf(dst,dst_len,"Instructions decoded per second: %0.2f\n",num_inst_all_cores/etime);
	updateDst(n,dst,dst_len);
	n = snprintf(dst,dst_len,"Trace messages decoded per second: %0.2f\n",num_trace_msgs_all_cores/etime);
	updateDst(n,dst,dst_len);
#endif // DO_TIMES
}

std::string Analytics::toString(int detailLevel)
{
	char dst[4096];

	dst[0] = 0;

	toText(dst,sizeof dst,detailLevel);

	return std::string(dst);
}

uint32_t NexusMessage::targetFrequency = 0;

NexusMessage::NexusMessage()
{
	msgNum         = 0;
	tcode          = TraceDqr::TCODE_UNDEFINED;
	coreId         = 0;
	haveTimestamp  = false;
	timestamp      = 0;
	currentAddress = 0;
	time = 0;

	offset = 0;
	for (int i = 0; (size_t)i < sizeof rawData/sizeof rawData[0]; i++) {
		rawData[i] = 0xff;
	}
}

int NexusMessage::getI_Cnt()
{
	switch (tcode) {
	case TraceDqr::TCODE_DIRECT_BRANCH:
		return directBranch.i_cnt;
	case TraceDqr::TCODE_INDIRECT_BRANCH:
		return indirectBranch.i_cnt;
	case TraceDqr::TCODE_SYNC:
		return sync.i_cnt;
	case TraceDqr::TCODE_DIRECT_BRANCH_WS:
		return directBranchWS.i_cnt;
	case TraceDqr::TCODE_INDIRECT_BRANCH_WS:
		return indirectBranchWS.i_cnt;
	case TraceDqr::TCODE_CORRELATION:
		return correlation.i_cnt;
	case TraceDqr::TCODE_INDIRECTBRANCHHISTORY:
		return indirectHistory.i_cnt;
	case TraceDqr::TCODE_INDIRECTBRANCHHISTORY_WS:
		return indirectHistoryWS.i_cnt;
	case TraceDqr::TCODE_RESOURCEFULL:
		if (resourceFull.rCode == 0) {
			return resourceFull.i_cnt;
		}
		break;
	case TraceDqr::TCODE_DEBUG_STATUS:
	case TraceDqr::TCODE_DEVICE_ID:
	case TraceDqr::TCODE_OWNERSHIP_TRACE:
	case TraceDqr::TCODE_DATA_WRITE:
	case TraceDqr::TCODE_DATA_READ:
	case TraceDqr::TCODE_DATA_ACQUISITION:
	case TraceDqr::TCODE_ERROR:
	case TraceDqr::TCODE_CORRECTION:
	case TraceDqr::TCODE_DATA_WRITE_WS:
	case TraceDqr::TCODE_DATA_READ_WS:
	case TraceDqr::TCODE_WATCHPOINT:
	case TraceDqr::TCODE_OUTPUT_PORTREPLACEMENT:
	case TraceDqr::TCODE_INPUT_PORTREPLACEMENT:
	case TraceDqr::TCODE_AUXACCESS_READ:
	case TraceDqr::TCODE_AUXACCESS_WRITE:
	case TraceDqr::TCODE_AUXACCESS_READNEXT:
	case TraceDqr::TCODE_AUXACCESS_WRITENEXT:
	case TraceDqr::TCODE_AUXACCESS_RESPONSE:
	case TraceDqr::TCODE_REPEATBRANCH:
	case TraceDqr::TCODE_REPEATINSTRUCTION:
	case TraceDqr::TCODE_REPEATINSTRUCTION_WS:
	case TraceDqr::TCODE_INCIRCUITTRACE:
	case TraceDqr::TCODE_INCIRCUITTRACE_WS:
	case TraceDqr::TCODE_UNDEFINED:
		break;
	default:
		break;
	}

	return 0;
}

TraceDqr::ADDRESS NexusMessage::getU_Addr()
{
	switch (tcode) {
	case TraceDqr::TCODE_INDIRECT_BRANCH:
		return indirectBranch.u_addr;
	case TraceDqr::TCODE_INDIRECTBRANCHHISTORY:
		return indirectHistory.u_addr;
	case TraceDqr::TCODE_INCIRCUITTRACE:
		return ict.ckdata;
	case TraceDqr::TCODE_DEBUG_STATUS:
	case TraceDqr::TCODE_DEVICE_ID:
	case TraceDqr::TCODE_OWNERSHIP_TRACE:
	case TraceDqr::TCODE_DIRECT_BRANCH:
	case TraceDqr::TCODE_DATA_WRITE:
	case TraceDqr::TCODE_DATA_READ:
	case TraceDqr::TCODE_DATA_ACQUISITION:
	case TraceDqr::TCODE_ERROR:
	case TraceDqr::TCODE_SYNC:
	case TraceDqr::TCODE_CORRECTION:
	case TraceDqr::TCODE_DIRECT_BRANCH_WS:
	case TraceDqr::TCODE_INDIRECT_BRANCH_WS:
	case TraceDqr::TCODE_DATA_WRITE_WS:
	case TraceDqr::TCODE_DATA_READ_WS:
	case TraceDqr::TCODE_WATCHPOINT:
	case TraceDqr::TCODE_OUTPUT_PORTREPLACEMENT:
	case TraceDqr::TCODE_INPUT_PORTREPLACEMENT:
	case TraceDqr::TCODE_AUXACCESS_READ:
	case TraceDqr::TCODE_AUXACCESS_WRITE:
	case TraceDqr::TCODE_AUXACCESS_READNEXT:
	case TraceDqr::TCODE_AUXACCESS_WRITENEXT:
	case TraceDqr::TCODE_AUXACCESS_RESPONSE:
	case TraceDqr::TCODE_RESOURCEFULL:
	case TraceDqr::TCODE_INDIRECTBRANCHHISTORY_WS:
	case TraceDqr::TCODE_REPEATBRANCH:
	case TraceDqr::TCODE_REPEATINSTRUCTION:
	case TraceDqr::TCODE_REPEATINSTRUCTION_WS:
	case TraceDqr::TCODE_CORRELATION:
	case TraceDqr::TCODE_INCIRCUITTRACE_WS:
	case TraceDqr::TCODE_UNDEFINED:
		break;
	default:
		break;
	}

	return -1;
}

TraceDqr::ADDRESS NexusMessage::getF_Addr()
{
	switch (tcode) {
	case TraceDqr::TCODE_DIRECT_BRANCH_WS:
		return directBranchWS.f_addr;
	case TraceDqr::TCODE_INDIRECT_BRANCH_WS:
		return indirectBranchWS.f_addr;
	case TraceDqr::TCODE_SYNC:
		return sync.f_addr;
	case TraceDqr::TCODE_INDIRECTBRANCHHISTORY_WS:
		return indirectHistoryWS.f_addr;
	case TraceDqr::TCODE_INCIRCUITTRACE_WS:
		return ictWS.ckdata;
	case TraceDqr::TCODE_DEBUG_STATUS:
	case TraceDqr::TCODE_DEVICE_ID:
	case TraceDqr::TCODE_OWNERSHIP_TRACE:
	case TraceDqr::TCODE_DIRECT_BRANCH:
	case TraceDqr::TCODE_INDIRECT_BRANCH:
	case TraceDqr::TCODE_DATA_WRITE:
	case TraceDqr::TCODE_DATA_READ:
	case TraceDqr::TCODE_DATA_ACQUISITION:
	case TraceDqr::TCODE_ERROR:
	case TraceDqr::TCODE_CORRECTION:
	case TraceDqr::TCODE_DATA_WRITE_WS:
	case TraceDqr::TCODE_DATA_READ_WS:
	case TraceDqr::TCODE_WATCHPOINT:
	case TraceDqr::TCODE_OUTPUT_PORTREPLACEMENT:
	case TraceDqr::TCODE_INPUT_PORTREPLACEMENT:
	case TraceDqr::TCODE_AUXACCESS_READ:
	case TraceDqr::TCODE_AUXACCESS_WRITE:
	case TraceDqr::TCODE_AUXACCESS_READNEXT:
	case TraceDqr::TCODE_AUXACCESS_WRITENEXT:
	case TraceDqr::TCODE_AUXACCESS_RESPONSE:
	case TraceDqr::TCODE_RESOURCEFULL:
	case TraceDqr::TCODE_INDIRECTBRANCHHISTORY:
	case TraceDqr::TCODE_REPEATBRANCH:
	case TraceDqr::TCODE_REPEATINSTRUCTION:
	case TraceDqr::TCODE_REPEATINSTRUCTION_WS:
	case TraceDqr::TCODE_CORRELATION:
	case TraceDqr::TCODE_INCIRCUITTRACE:
	case TraceDqr::TCODE_UNDEFINED:
		break;
	default:
		break;
	}

	return -1;
}

//TraceDqr::ADDRESS NexusMessage::getNextAddr()
//{
//	TraceDqr::ADDRESS addr;
//
//	addr = getF_Addr();
//	if (addr != (TraceDqr::ADDRESS)-1) {
//		addr = addr << 1;
//	}
//
//	return addr;
//}

TraceDqr::BType NexusMessage::getB_Type()
{
	switch (tcode) {
	case TraceDqr::TCODE_INDIRECT_BRANCH:
		return indirectBranch.b_type;
	case TraceDqr::TCODE_INDIRECT_BRANCH_WS:
		return indirectBranchWS.b_type;
	case TraceDqr::TCODE_INDIRECTBRANCHHISTORY:
		return indirectHistory.b_type;
	case TraceDqr::TCODE_INDIRECTBRANCHHISTORY_WS:
		return indirectHistoryWS.b_type;
	case TraceDqr::TCODE_DEBUG_STATUS:
	case TraceDqr::TCODE_DEVICE_ID:
	case TraceDqr::TCODE_OWNERSHIP_TRACE:
	case TraceDqr::TCODE_DIRECT_BRANCH:
	case TraceDqr::TCODE_DATA_WRITE:
	case TraceDqr::TCODE_DATA_READ:
	case TraceDqr::TCODE_DATA_ACQUISITION:
	case TraceDqr::TCODE_ERROR:
	case TraceDqr::TCODE_SYNC:
	case TraceDqr::TCODE_CORRECTION:
	case TraceDqr::TCODE_DIRECT_BRANCH_WS:
	case TraceDqr::TCODE_DATA_WRITE_WS:
	case TraceDqr::TCODE_DATA_READ_WS:
	case TraceDqr::TCODE_WATCHPOINT:
	case TraceDqr::TCODE_OUTPUT_PORTREPLACEMENT:
	case TraceDqr::TCODE_INPUT_PORTREPLACEMENT:
	case TraceDqr::TCODE_AUXACCESS_READ:
	case TraceDqr::TCODE_AUXACCESS_WRITE:
	case TraceDqr::TCODE_AUXACCESS_READNEXT:
	case TraceDqr::TCODE_AUXACCESS_WRITENEXT:
	case TraceDqr::TCODE_AUXACCESS_RESPONSE:
	case TraceDqr::TCODE_RESOURCEFULL:
	case TraceDqr::TCODE_REPEATBRANCH:
	case TraceDqr::TCODE_REPEATINSTRUCTION:
	case TraceDqr::TCODE_REPEATINSTRUCTION_WS:
	case TraceDqr::TCODE_CORRELATION:
	case TraceDqr::TCODE_INCIRCUITTRACE:
	case TraceDqr::TCODE_INCIRCUITTRACE_WS:
	case TraceDqr::TCODE_UNDEFINED:
		break;
	default:
		break;
	}

	return TraceDqr::BTYPE_UNDEFINED;
}

TraceDqr::SyncReason NexusMessage::getSyncReason()
{
	switch (tcode) {
	case TraceDqr::TCODE_SYNC:
		return sync.sync;
	case TraceDqr::TCODE_DIRECT_BRANCH_WS:
		return directBranchWS.sync;
	case TraceDqr::TCODE_INDIRECT_BRANCH_WS:
		return indirectBranchWS.sync;
	case TraceDqr::TCODE_INDIRECTBRANCHHISTORY_WS:
		return indirectHistoryWS.sync;
	case TraceDqr::TCODE_DEBUG_STATUS:
	case TraceDqr::TCODE_DEVICE_ID:
	case TraceDqr::TCODE_OWNERSHIP_TRACE:
	case TraceDqr::TCODE_DIRECT_BRANCH:
	case TraceDqr::TCODE_INDIRECT_BRANCH:
	case TraceDqr::TCODE_DATA_WRITE:
	case TraceDqr::TCODE_DATA_READ:
	case TraceDqr::TCODE_DATA_ACQUISITION:
	case TraceDqr::TCODE_ERROR:
	case TraceDqr::TCODE_CORRECTION:
	case TraceDqr::TCODE_DATA_WRITE_WS:
	case TraceDqr::TCODE_DATA_READ_WS:
	case TraceDqr::TCODE_WATCHPOINT:
	case TraceDqr::TCODE_OUTPUT_PORTREPLACEMENT:
	case TraceDqr::TCODE_INPUT_PORTREPLACEMENT:
	case TraceDqr::TCODE_AUXACCESS_READ:
	case TraceDqr::TCODE_AUXACCESS_WRITE:
	case TraceDqr::TCODE_AUXACCESS_READNEXT:
	case TraceDqr::TCODE_AUXACCESS_WRITENEXT:
	case TraceDqr::TCODE_AUXACCESS_RESPONSE:
	case TraceDqr::TCODE_RESOURCEFULL:
	case TraceDqr::TCODE_INDIRECTBRANCHHISTORY:
	case TraceDqr::TCODE_REPEATBRANCH:
	case TraceDqr::TCODE_REPEATINSTRUCTION:
	case TraceDqr::TCODE_REPEATINSTRUCTION_WS:
	case TraceDqr::TCODE_CORRELATION:
	case TraceDqr::TCODE_INCIRCUITTRACE:
	case TraceDqr::TCODE_UNDEFINED:
		break;
	default:
		break;
	}

	return TraceDqr::SYNC_NONE;
}

TraceDqr::ICTReason NexusMessage::getICTReason()
{
	switch (tcode) {
	case TraceDqr::TCODE_INCIRCUITTRACE:
		return ict.cksrc;
	case TraceDqr::TCODE_INCIRCUITTRACE_WS:
		return ictWS.cksrc;
	case TraceDqr::TCODE_SYNC:
	case TraceDqr::TCODE_DIRECT_BRANCH_WS:
	case TraceDqr::TCODE_INDIRECT_BRANCH_WS:
	case TraceDqr::TCODE_INDIRECTBRANCHHISTORY_WS:
	case TraceDqr::TCODE_DEBUG_STATUS:
	case TraceDqr::TCODE_DEVICE_ID:
	case TraceDqr::TCODE_OWNERSHIP_TRACE:
	case TraceDqr::TCODE_DIRECT_BRANCH:
	case TraceDqr::TCODE_INDIRECT_BRANCH:
	case TraceDqr::TCODE_DATA_WRITE:
	case TraceDqr::TCODE_DATA_READ:
	case TraceDqr::TCODE_DATA_ACQUISITION:
	case TraceDqr::TCODE_ERROR:
	case TraceDqr::TCODE_CORRECTION:
	case TraceDqr::TCODE_DATA_WRITE_WS:
	case TraceDqr::TCODE_DATA_READ_WS:
	case TraceDqr::TCODE_WATCHPOINT:
	case TraceDqr::TCODE_OUTPUT_PORTREPLACEMENT:
	case TraceDqr::TCODE_INPUT_PORTREPLACEMENT:
	case TraceDqr::TCODE_AUXACCESS_READ:
	case TraceDqr::TCODE_AUXACCESS_WRITE:
	case TraceDqr::TCODE_AUXACCESS_READNEXT:
	case TraceDqr::TCODE_AUXACCESS_WRITENEXT:
	case TraceDqr::TCODE_AUXACCESS_RESPONSE:
	case TraceDqr::TCODE_RESOURCEFULL:
	case TraceDqr::TCODE_INDIRECTBRANCHHISTORY:
	case TraceDqr::TCODE_REPEATBRANCH:
	case TraceDqr::TCODE_REPEATINSTRUCTION:
	case TraceDqr::TCODE_REPEATINSTRUCTION_WS:
	case TraceDqr::TCODE_CORRELATION:
	case TraceDqr::TCODE_UNDEFINED:
		break;
	default:
		break;
	}

	return TraceDqr::ICT_NONE;
}

uint8_t NexusMessage::getEType()
{
	switch (tcode) {
	case TraceDqr::TCODE_ERROR:
		return error.etype;
	case TraceDqr::TCODE_DEBUG_STATUS:
	case TraceDqr::TCODE_DEVICE_ID:
	case TraceDqr::TCODE_OWNERSHIP_TRACE:
	case TraceDqr::TCODE_DIRECT_BRANCH:
	case TraceDqr::TCODE_INDIRECT_BRANCH:
	case TraceDqr::TCODE_DATA_WRITE:
	case TraceDqr::TCODE_DATA_READ:
	case TraceDqr::TCODE_DATA_ACQUISITION:
	case TraceDqr::TCODE_SYNC:
	case TraceDqr::TCODE_CORRECTION:
	case TraceDqr::TCODE_DIRECT_BRANCH_WS:
	case TraceDqr::TCODE_INDIRECT_BRANCH_WS:
	case TraceDqr::TCODE_DATA_WRITE_WS:
	case TraceDqr::TCODE_DATA_READ_WS:
	case TraceDqr::TCODE_WATCHPOINT:
	case TraceDqr::TCODE_OUTPUT_PORTREPLACEMENT:
	case TraceDqr::TCODE_INPUT_PORTREPLACEMENT:
	case TraceDqr::TCODE_AUXACCESS_READ:
	case TraceDqr::TCODE_AUXACCESS_WRITE:
	case TraceDqr::TCODE_AUXACCESS_READNEXT:
	case TraceDqr::TCODE_AUXACCESS_WRITENEXT:
	case TraceDqr::TCODE_AUXACCESS_RESPONSE:
	case TraceDqr::TCODE_RESOURCEFULL:
	case TraceDqr::TCODE_INDIRECTBRANCHHISTORY:
	case TraceDqr::TCODE_INDIRECTBRANCHHISTORY_WS:
	case TraceDqr::TCODE_REPEATBRANCH:
	case TraceDqr::TCODE_REPEATINSTRUCTION:
	case TraceDqr::TCODE_REPEATINSTRUCTION_WS:
	case TraceDqr::TCODE_CORRELATION:
	case TraceDqr::TCODE_INCIRCUITTRACE:
	case TraceDqr::TCODE_UNDEFINED:
		break;
	default:
		break;
	}

	return 0;
}

uint8_t NexusMessage::getCDF()
{
	switch (tcode) {
	case TraceDqr::TCODE_CORRELATION:
		return correlation.cdf;
	case TraceDqr::TCODE_DEBUG_STATUS:
	case TraceDqr::TCODE_DEVICE_ID:
	case TraceDqr::TCODE_OWNERSHIP_TRACE:
	case TraceDqr::TCODE_DIRECT_BRANCH:
	case TraceDqr::TCODE_INDIRECT_BRANCH:
	case TraceDqr::TCODE_DATA_WRITE:
	case TraceDqr::TCODE_DATA_READ:
	case TraceDqr::TCODE_DATA_ACQUISITION:
	case TraceDqr::TCODE_ERROR:
	case TraceDqr::TCODE_SYNC:
	case TraceDqr::TCODE_CORRECTION:
	case TraceDqr::TCODE_DIRECT_BRANCH_WS:
	case TraceDqr::TCODE_INDIRECT_BRANCH_WS:
	case TraceDqr::TCODE_DATA_WRITE_WS:
	case TraceDqr::TCODE_DATA_READ_WS:
	case TraceDqr::TCODE_WATCHPOINT:
	case TraceDqr::TCODE_OUTPUT_PORTREPLACEMENT:
	case TraceDqr::TCODE_INPUT_PORTREPLACEMENT:
	case TraceDqr::TCODE_AUXACCESS_READ:
	case TraceDqr::TCODE_AUXACCESS_WRITE:
	case TraceDqr::TCODE_AUXACCESS_READNEXT:
	case TraceDqr::TCODE_AUXACCESS_WRITENEXT:
	case TraceDqr::TCODE_AUXACCESS_RESPONSE:
	case TraceDqr::TCODE_RESOURCEFULL:
	case TraceDqr::TCODE_INDIRECTBRANCHHISTORY:
	case TraceDqr::TCODE_INDIRECTBRANCHHISTORY_WS:
	case TraceDqr::TCODE_REPEATBRANCH:
	case TraceDqr::TCODE_REPEATINSTRUCTION:
	case TraceDqr::TCODE_REPEATINSTRUCTION_WS:
	case TraceDqr::TCODE_INCIRCUITTRACE:
	case TraceDqr::TCODE_UNDEFINED:
		break;
	default:
		break;
	}

	return 0;
}

uint8_t NexusMessage::getEVCode()
{
	switch (tcode) {
	case TraceDqr::TCODE_CORRELATION:
		return correlation.evcode;
	case TraceDqr::TCODE_DEBUG_STATUS:
	case TraceDqr::TCODE_DEVICE_ID:
	case TraceDqr::TCODE_OWNERSHIP_TRACE:
	case TraceDqr::TCODE_DIRECT_BRANCH:
	case TraceDqr::TCODE_INDIRECT_BRANCH:
	case TraceDqr::TCODE_DATA_WRITE:
	case TraceDqr::TCODE_DATA_READ:
	case TraceDqr::TCODE_DATA_ACQUISITION:
	case TraceDqr::TCODE_ERROR:
	case TraceDqr::TCODE_SYNC:
	case TraceDqr::TCODE_CORRECTION:
	case TraceDqr::TCODE_DIRECT_BRANCH_WS:
	case TraceDqr::TCODE_INDIRECT_BRANCH_WS:
	case TraceDqr::TCODE_DATA_WRITE_WS:
	case TraceDqr::TCODE_DATA_READ_WS:
	case TraceDqr::TCODE_WATCHPOINT:
	case TraceDqr::TCODE_OUTPUT_PORTREPLACEMENT:
	case TraceDqr::TCODE_INPUT_PORTREPLACEMENT:
	case TraceDqr::TCODE_AUXACCESS_READ:
	case TraceDqr::TCODE_AUXACCESS_WRITE:
	case TraceDqr::TCODE_AUXACCESS_READNEXT:
	case TraceDqr::TCODE_AUXACCESS_WRITENEXT:
	case TraceDqr::TCODE_AUXACCESS_RESPONSE:
	case TraceDqr::TCODE_RESOURCEFULL:
	case TraceDqr::TCODE_INDIRECTBRANCHHISTORY:
	case TraceDqr::TCODE_INDIRECTBRANCHHISTORY_WS:
	case TraceDqr::TCODE_REPEATBRANCH:
	case TraceDqr::TCODE_REPEATINSTRUCTION:
	case TraceDqr::TCODE_REPEATINSTRUCTION_WS:
	case TraceDqr::TCODE_INCIRCUITTRACE:
		break;
	case TraceDqr::TCODE_UNDEFINED:
		break;
	default:
		break;
	}

	return 0;
}

uint32_t NexusMessage::getData()
{
	switch (tcode) {
	case TraceDqr::TCODE_DATA_ACQUISITION:
		return dataAcquisition.data;
	case TraceDqr::TCODE_AUXACCESS_WRITE:
		return auxAccessWrite.data;
	case TraceDqr::TCODE_DEBUG_STATUS:
	case TraceDqr::TCODE_DEVICE_ID:
	case TraceDqr::TCODE_OWNERSHIP_TRACE:
	case TraceDqr::TCODE_DIRECT_BRANCH:
	case TraceDqr::TCODE_INDIRECT_BRANCH:
	case TraceDqr::TCODE_DATA_WRITE:
	case TraceDqr::TCODE_DATA_READ:
	case TraceDqr::TCODE_ERROR:
	case TraceDqr::TCODE_SYNC:
	case TraceDqr::TCODE_CORRECTION:
	case TraceDqr::TCODE_DIRECT_BRANCH_WS:
	case TraceDqr::TCODE_INDIRECT_BRANCH_WS:
	case TraceDqr::TCODE_DATA_WRITE_WS:
	case TraceDqr::TCODE_DATA_READ_WS:
	case TraceDqr::TCODE_WATCHPOINT:
	case TraceDqr::TCODE_OUTPUT_PORTREPLACEMENT:
	case TraceDqr::TCODE_INPUT_PORTREPLACEMENT:
	case TraceDqr::TCODE_AUXACCESS_READ:
	case TraceDqr::TCODE_AUXACCESS_READNEXT:
	case TraceDqr::TCODE_AUXACCESS_WRITENEXT:
	case TraceDqr::TCODE_AUXACCESS_RESPONSE:
	case TraceDqr::TCODE_RESOURCEFULL:
	case TraceDqr::TCODE_INDIRECTBRANCHHISTORY:
	case TraceDqr::TCODE_INDIRECTBRANCHHISTORY_WS:
	case TraceDqr::TCODE_REPEATBRANCH:
	case TraceDqr::TCODE_REPEATINSTRUCTION:
	case TraceDqr::TCODE_REPEATINSTRUCTION_WS:
	case TraceDqr::TCODE_CORRELATION:
	case TraceDqr::TCODE_INCIRCUITTRACE:
	case TraceDqr::TCODE_UNDEFINED:
		break;
	default:
		break;
	}

	return 0;
}

uint32_t NexusMessage::getAddr()
{
	switch (tcode) {
	case TraceDqr::TCODE_AUXACCESS_WRITE:
		return auxAccessWrite.data;
	case TraceDqr::TCODE_DATA_ACQUISITION:
	case TraceDqr::TCODE_DEBUG_STATUS:
	case TraceDqr::TCODE_DEVICE_ID:
	case TraceDqr::TCODE_OWNERSHIP_TRACE:
	case TraceDqr::TCODE_DIRECT_BRANCH:
	case TraceDqr::TCODE_INDIRECT_BRANCH:
	case TraceDqr::TCODE_DATA_WRITE:
	case TraceDqr::TCODE_DATA_READ:
	case TraceDqr::TCODE_ERROR:
	case TraceDqr::TCODE_SYNC:
	case TraceDqr::TCODE_CORRECTION:
	case TraceDqr::TCODE_DIRECT_BRANCH_WS:
	case TraceDqr::TCODE_INDIRECT_BRANCH_WS:
	case TraceDqr::TCODE_DATA_WRITE_WS:
	case TraceDqr::TCODE_DATA_READ_WS:
	case TraceDqr::TCODE_WATCHPOINT:
	case TraceDqr::TCODE_OUTPUT_PORTREPLACEMENT:
	case TraceDqr::TCODE_INPUT_PORTREPLACEMENT:
	case TraceDqr::TCODE_AUXACCESS_READ:
	case TraceDqr::TCODE_AUXACCESS_READNEXT:
	case TraceDqr::TCODE_AUXACCESS_WRITENEXT:
	case TraceDqr::TCODE_AUXACCESS_RESPONSE:
	case TraceDqr::TCODE_RESOURCEFULL:
	case TraceDqr::TCODE_INDIRECTBRANCHHISTORY:
	case TraceDqr::TCODE_INDIRECTBRANCHHISTORY_WS:
	case TraceDqr::TCODE_REPEATBRANCH:
	case TraceDqr::TCODE_REPEATINSTRUCTION:
	case TraceDqr::TCODE_REPEATINSTRUCTION_WS:
	case TraceDqr::TCODE_CORRELATION:
	case TraceDqr::TCODE_INCIRCUITTRACE:
	case TraceDqr::TCODE_UNDEFINED:
		break;
	default:
		break;
	}

	return 0;
}

uint32_t NexusMessage::getIdTag()
{
	switch (tcode) {
	case TraceDqr::TCODE_DATA_ACQUISITION:
		return dataAcquisition.idTag;
	case TraceDqr::TCODE_DEBUG_STATUS:
	case TraceDqr::TCODE_DEVICE_ID:
	case TraceDqr::TCODE_OWNERSHIP_TRACE:
	case TraceDqr::TCODE_DIRECT_BRANCH:
	case TraceDqr::TCODE_INDIRECT_BRANCH:
	case TraceDqr::TCODE_DATA_WRITE:
	case TraceDqr::TCODE_DATA_READ:
	case TraceDqr::TCODE_ERROR:
	case TraceDqr::TCODE_SYNC:
	case TraceDqr::TCODE_CORRECTION:
	case TraceDqr::TCODE_DIRECT_BRANCH_WS:
	case TraceDqr::TCODE_INDIRECT_BRANCH_WS:
	case TraceDqr::TCODE_DATA_WRITE_WS:
	case TraceDqr::TCODE_DATA_READ_WS:
	case TraceDqr::TCODE_WATCHPOINT:
	case TraceDqr::TCODE_OUTPUT_PORTREPLACEMENT:
	case TraceDqr::TCODE_INPUT_PORTREPLACEMENT:
	case TraceDqr::TCODE_AUXACCESS_READ:
	case TraceDqr::TCODE_AUXACCESS_WRITE:
	case TraceDqr::TCODE_AUXACCESS_READNEXT:
	case TraceDqr::TCODE_AUXACCESS_WRITENEXT:
	case TraceDqr::TCODE_AUXACCESS_RESPONSE:
	case TraceDqr::TCODE_RESOURCEFULL:
	case TraceDqr::TCODE_INDIRECTBRANCHHISTORY:
	case TraceDqr::TCODE_INDIRECTBRANCHHISTORY_WS:
	case TraceDqr::TCODE_REPEATBRANCH:
	case TraceDqr::TCODE_REPEATINSTRUCTION:
	case TraceDqr::TCODE_REPEATINSTRUCTION_WS:
	case TraceDqr::TCODE_CORRELATION:
	case TraceDqr::TCODE_INCIRCUITTRACE:
	case TraceDqr::TCODE_UNDEFINED:
		break;
	default:
		break;
	}

	return 0;
}

uint32_t NexusMessage::getProcess()
{
	switch (tcode) {
	case TraceDqr::TCODE_OWNERSHIP_TRACE:
		return ownership.process;
	case TraceDqr::TCODE_DEBUG_STATUS:
	case TraceDqr::TCODE_DEVICE_ID:
	case TraceDqr::TCODE_DIRECT_BRANCH:
	case TraceDqr::TCODE_INDIRECT_BRANCH:
	case TraceDqr::TCODE_DATA_WRITE:
	case TraceDqr::TCODE_DATA_READ:
	case TraceDqr::TCODE_DATA_ACQUISITION:
	case TraceDqr::TCODE_ERROR:
	case TraceDqr::TCODE_SYNC:
	case TraceDqr::TCODE_CORRECTION:
	case TraceDqr::TCODE_DIRECT_BRANCH_WS:
	case TraceDqr::TCODE_INDIRECT_BRANCH_WS:
	case TraceDqr::TCODE_DATA_WRITE_WS:
	case TraceDqr::TCODE_DATA_READ_WS:
	case TraceDqr::TCODE_WATCHPOINT:
	case TraceDqr::TCODE_OUTPUT_PORTREPLACEMENT:
	case TraceDqr::TCODE_INPUT_PORTREPLACEMENT:
	case TraceDqr::TCODE_AUXACCESS_READ:
	case TraceDqr::TCODE_AUXACCESS_WRITE:
	case TraceDqr::TCODE_AUXACCESS_READNEXT:
	case TraceDqr::TCODE_AUXACCESS_WRITENEXT:
	case TraceDqr::TCODE_AUXACCESS_RESPONSE:
	case TraceDqr::TCODE_RESOURCEFULL:
	case TraceDqr::TCODE_INDIRECTBRANCHHISTORY:
	case TraceDqr::TCODE_INDIRECTBRANCHHISTORY_WS:
	case TraceDqr::TCODE_REPEATBRANCH:
	case TraceDqr::TCODE_REPEATINSTRUCTION:
	case TraceDqr::TCODE_REPEATINSTRUCTION_WS:
	case TraceDqr::TCODE_CORRELATION:
	case TraceDqr::TCODE_INCIRCUITTRACE:
	case TraceDqr::TCODE_UNDEFINED:
		break;
	default:
		break;
	}

	return 0;
}

uint32_t NexusMessage::getRCode()
{
	switch (tcode) {
	case TraceDqr::TCODE_RESOURCEFULL:
		return resourceFull.rCode;
	case TraceDqr::TCODE_OWNERSHIP_TRACE:
	case TraceDqr::TCODE_DEBUG_STATUS:
	case TraceDqr::TCODE_DEVICE_ID:
	case TraceDqr::TCODE_DIRECT_BRANCH:
	case TraceDqr::TCODE_INDIRECT_BRANCH:
	case TraceDqr::TCODE_DATA_WRITE:
	case TraceDqr::TCODE_DATA_READ:
	case TraceDqr::TCODE_DATA_ACQUISITION:
	case TraceDqr::TCODE_ERROR:
	case TraceDqr::TCODE_SYNC:
	case TraceDqr::TCODE_CORRECTION:
	case TraceDqr::TCODE_DIRECT_BRANCH_WS:
	case TraceDqr::TCODE_INDIRECT_BRANCH_WS:
	case TraceDqr::TCODE_DATA_WRITE_WS:
	case TraceDqr::TCODE_DATA_READ_WS:
	case TraceDqr::TCODE_WATCHPOINT:
	case TraceDqr::TCODE_OUTPUT_PORTREPLACEMENT:
	case TraceDqr::TCODE_INPUT_PORTREPLACEMENT:
	case TraceDqr::TCODE_AUXACCESS_READ:
	case TraceDqr::TCODE_AUXACCESS_WRITE:
	case TraceDqr::TCODE_AUXACCESS_READNEXT:
	case TraceDqr::TCODE_AUXACCESS_WRITENEXT:
	case TraceDqr::TCODE_AUXACCESS_RESPONSE:
	case TraceDqr::TCODE_INDIRECTBRANCHHISTORY:
	case TraceDqr::TCODE_INDIRECTBRANCHHISTORY_WS:
	case TraceDqr::TCODE_REPEATBRANCH:
	case TraceDqr::TCODE_REPEATINSTRUCTION:
	case TraceDqr::TCODE_REPEATINSTRUCTION_WS:
	case TraceDqr::TCODE_CORRELATION:
	case TraceDqr::TCODE_INCIRCUITTRACE:
	case TraceDqr::TCODE_UNDEFINED:
		break;
	default:
		break;
	}

	return 0;
}

uint64_t NexusMessage::getRData()
{
	switch (tcode) {
	case TraceDqr::TCODE_RESOURCEFULL:
		switch (resourceFull.rCode) {
		case 0:
			return (uint64_t)resourceFull.i_cnt;
		case 1:
			return resourceFull.history;
		case 8:
			return (uint64_t)resourceFull.notTakenCount;
		case 9:
			return (uint64_t)resourceFull.takenCount;
		default:
			break;
		}
		return 0;
	case TraceDqr::TCODE_OWNERSHIP_TRACE:
	case TraceDqr::TCODE_DEBUG_STATUS:
	case TraceDqr::TCODE_DEVICE_ID:
	case TraceDqr::TCODE_DIRECT_BRANCH:
	case TraceDqr::TCODE_INDIRECT_BRANCH:
	case TraceDqr::TCODE_DATA_WRITE:
	case TraceDqr::TCODE_DATA_READ:
	case TraceDqr::TCODE_DATA_ACQUISITION:
	case TraceDqr::TCODE_ERROR:
	case TraceDqr::TCODE_SYNC:
	case TraceDqr::TCODE_CORRECTION:
	case TraceDqr::TCODE_DIRECT_BRANCH_WS:
	case TraceDqr::TCODE_INDIRECT_BRANCH_WS:
	case TraceDqr::TCODE_DATA_WRITE_WS:
	case TraceDqr::TCODE_DATA_READ_WS:
	case TraceDqr::TCODE_WATCHPOINT:
	case TraceDqr::TCODE_OUTPUT_PORTREPLACEMENT:
	case TraceDqr::TCODE_INPUT_PORTREPLACEMENT:
	case TraceDqr::TCODE_AUXACCESS_READ:
	case TraceDqr::TCODE_AUXACCESS_WRITE:
	case TraceDqr::TCODE_AUXACCESS_READNEXT:
	case TraceDqr::TCODE_AUXACCESS_WRITENEXT:
	case TraceDqr::TCODE_AUXACCESS_RESPONSE:
	case TraceDqr::TCODE_INDIRECTBRANCHHISTORY:
	case TraceDqr::TCODE_INDIRECTBRANCHHISTORY_WS:
	case TraceDqr::TCODE_REPEATBRANCH:
	case TraceDqr::TCODE_REPEATINSTRUCTION:
	case TraceDqr::TCODE_REPEATINSTRUCTION_WS:
	case TraceDqr::TCODE_CORRELATION:
	case TraceDqr::TCODE_INCIRCUITTRACE:
	case TraceDqr::TCODE_UNDEFINED:
		break;
	default:
		break;
	}

	return 0;
}

uint64_t NexusMessage::getHistory()
{
	switch (tcode) {
	case TraceDqr::TCODE_RESOURCEFULL:
		if (resourceFull.rCode == 1) {
			return resourceFull.history;
		}
		break;
	case TraceDqr::TCODE_CORRELATION:
		return correlation.history;
	case TraceDqr::TCODE_INDIRECTBRANCHHISTORY:
		return indirectHistory.history;
	case TraceDqr::TCODE_INDIRECTBRANCHHISTORY_WS:
		return indirectHistoryWS.history;
	case TraceDqr::TCODE_OWNERSHIP_TRACE:
	case TraceDqr::TCODE_DEBUG_STATUS:
	case TraceDqr::TCODE_DEVICE_ID:
	case TraceDqr::TCODE_DIRECT_BRANCH:
	case TraceDqr::TCODE_INDIRECT_BRANCH:
	case TraceDqr::TCODE_DATA_WRITE:
	case TraceDqr::TCODE_DATA_READ:
	case TraceDqr::TCODE_DATA_ACQUISITION:
	case TraceDqr::TCODE_ERROR:
	case TraceDqr::TCODE_SYNC:
	case TraceDqr::TCODE_CORRECTION:
	case TraceDqr::TCODE_DIRECT_BRANCH_WS:
	case TraceDqr::TCODE_INDIRECT_BRANCH_WS:
	case TraceDqr::TCODE_DATA_WRITE_WS:
	case TraceDqr::TCODE_DATA_READ_WS:
	case TraceDqr::TCODE_WATCHPOINT:
	case TraceDqr::TCODE_OUTPUT_PORTREPLACEMENT:
	case TraceDqr::TCODE_INPUT_PORTREPLACEMENT:
	case TraceDqr::TCODE_AUXACCESS_READ:
	case TraceDqr::TCODE_AUXACCESS_WRITE:
	case TraceDqr::TCODE_AUXACCESS_READNEXT:
	case TraceDqr::TCODE_AUXACCESS_WRITENEXT:
	case TraceDqr::TCODE_AUXACCESS_RESPONSE:
	case TraceDqr::TCODE_REPEATBRANCH:
	case TraceDqr::TCODE_REPEATINSTRUCTION:
	case TraceDqr::TCODE_REPEATINSTRUCTION_WS:
	case TraceDqr::TCODE_INCIRCUITTRACE:
	case TraceDqr::TCODE_UNDEFINED:
		break;
	default:
		break;
	}

	return 0;
}

bool NexusMessage::processITCPrintData(ITCPrint *itcPrint)
{
	bool rc = false;

	// need to only do this once per message! Set a flag so we know we have done it.
	// if flag set, return string (or null if none)

	if (itcPrint != nullptr) {
		switch (tcode) {
		case TraceDqr::TCODE_DATA_ACQUISITION:
			rc = itcPrint->print(coreId,dataAcquisition.idTag,dataAcquisition.data,time);
			break;
		case TraceDqr::TCODE_AUXACCESS_WRITE:
			rc = itcPrint->print(coreId,auxAccessWrite.addr,auxAccessWrite.data,time);
			break;
		default:
			break;
		}
	}

	return rc;
}

std::string NexusMessage::messageToString(int detailLevel)
{
	char dst[512];

	messageToText(dst,sizeof dst,detailLevel);

	return std::string(dst);
}

void  NexusMessage::messageToText(char *dst,size_t dst_len,int level)
{
	assert(dst != nullptr);

	const char *sr;
	const char *bt;
	int n;

	// level = 0, itcprint (always process itc print info)
	// level = 1, timestamp + target + itcprint
	// level = 2, message info + timestamp + target + itcprint
	// level = 3, message info + timestamp + target + itcprint + raw trace data

	if (level <= 0) {
		dst[0] = 0;
		return;
	}

	n = snprintf(dst,dst_len,"Msg # %d, ",msgNum);

	if (level >= 3) {
		n += snprintf(dst+n,dst_len-n,"Offset %d, ",offset);

		int i = 0;

		do {
			if (i > 0) {
				n += snprintf(dst+n,dst_len-n,":%02x",rawData[i]);
			}
			else {
				n += snprintf(dst+n,dst_len-n,"%02x",rawData[i]);
			}
			i += 1;
		} while (((size_t)i < sizeof rawData / sizeof rawData[0]) && ((rawData[i-1] & 0x3) != TraceDqr::MSEO_END));
		n += snprintf(dst+n,dst_len-n,", ");
	}

	if (haveTimestamp) {
		if (NexusMessage::targetFrequency != 0) {
			n += snprintf(dst+n,dst_len-n,"time: %0.8f, NxtAddr: %08llx, TCode: ",((double)time)/NexusMessage::targetFrequency,currentAddress);
		}
		else {
			n += snprintf(dst+n,dst_len-n,"Tics: %lld, NxtAddr: %08llx, TCode: ",time,currentAddress);
		}
	}
	else {
		n += snprintf(dst+n,dst_len-n,"NxtAddr: %08llx, TCode: ",currentAddress);
	}

	switch (tcode) {
	case TraceDqr::TCODE_DEBUG_STATUS:
		snprintf(dst+n,dst_len-n,"DEBUG STATUS (%d)",tcode);
		break;
	case TraceDqr::TCODE_DEVICE_ID:
		snprintf(dst+n,dst_len-n,"DEVICE ID (%d)",tcode);
		break;
	case TraceDqr::TCODE_OWNERSHIP_TRACE:
		n += snprintf(dst+n,dst_len-n,"OWNERSHIP TRACE (%d)",tcode);

		if (level >= 2) {
			snprintf(dst+n,dst_len-n," process: %d",ownership.process);
		}
		break;
	case TraceDqr::TCODE_DIRECT_BRANCH:
		n += snprintf(dst+n,dst_len-n,"DIRECT BRANCH (%d)",tcode);
		if (level >= 2) {
			snprintf(dst+n,dst_len-n," I-CNT: %d",directBranch.i_cnt);
		}
		break;
	case TraceDqr::TCODE_INDIRECT_BRANCH:
		n += snprintf(dst+n,dst_len-n,"INDIRECT BRANCH (%d)",tcode);

		if (level >= 2) {
			switch (indirectBranch.b_type) {
			case TraceDqr::BTYPE_INDIRECT:
				bt = "Indirect";
				break;
			case TraceDqr::BTYPE_EXCEPTION:
				bt = "Exception";
				break;
			case TraceDqr::BTYPE_HARDWARE:
				bt = "Hardware";
				break;
			case TraceDqr::BTYPE_UNDEFINED:
				bt = "Undefined";
				break;
			default:
				bt = "Bad Branch Type";
				break;
			}

			snprintf(dst+n,dst_len-n," Branch Type: %s (%d) I-CNT: %d U-ADDR: 0x%08llx ",bt,indirectBranch.b_type,indirectBranch.i_cnt,indirectBranch.u_addr);
		}
		break;
	case TraceDqr::TCODE_DATA_WRITE:
		snprintf(dst+n,dst_len-n,"DATA WRITE (%d)",tcode);
		break;
	case TraceDqr::TCODE_DATA_READ:
		snprintf(dst+n,dst_len-n,"DATA READ (%d)",tcode);
		break;
	case TraceDqr::TCODE_ERROR:
		n += snprintf(dst+n,dst_len-n,"ERROR (%d)",tcode);
		if (level >= 2) {
			snprintf(dst+n,dst_len-n," Error Type %d",error.etype);
		}
		break;
	case TraceDqr::TCODE_SYNC:
		n += snprintf(dst+n,dst_len-n,"SYNC (%d)",tcode);

		if (level >= 2) {
			switch (sync.sync) {
			case TraceDqr::SYNC_EVTI:
				sr = "EVTI";
				break;
			case TraceDqr::SYNC_EXIT_RESET:
				sr = "Exit Reset";
				break;
			case TraceDqr::SYNC_T_CNT:
				sr = "T Count";
				break;
			case TraceDqr::SYNC_EXIT_DEBUG:
				sr = "Exit Debug";
				break;
			case TraceDqr::SYNC_I_CNT_OVERFLOW:
				sr = "I-Count Overflow";
				break;
			case TraceDqr::SYNC_TRACE_ENABLE:
				sr = "Trace Enable";
				break;
			case TraceDqr::SYNC_WATCHPINT:
				sr = "Watchpoint";
				break;
			case TraceDqr::SYNC_FIFO_OVERRUN:
				sr = "FIFO Overrun";
				break;
			case TraceDqr::SYNC_EXIT_POWERDOWN:
				sr = "Exit Powerdown";
				break;
			case TraceDqr::SYNC_MESSAGE_CONTENTION:
				sr = "Message Contention";
				break;
			case TraceDqr::SYNC_PC_SAMPLE:
				sr = "PC Sample";
				break;
			case TraceDqr::SYNC_NONE:
				sr = "None";
				break;
			default:
				sr = "Bad Sync Reason";
				break;
			}

			snprintf(dst+n,dst_len-n," Reason: (%d) %s I-CNT: %d F-Addr: 0x%08llx",sync.sync,sr,sync.i_cnt,sync.f_addr);
		}
		break;
	case TraceDqr::TCODE_CORRECTION:
		snprintf(dst+n,dst_len-n,"Correction (%d)",tcode);
		break;
	case TraceDqr::TCODE_DIRECT_BRANCH_WS:
		n += snprintf(dst+n,dst_len-n,"DIRECT BRANCH WS (%d)",tcode);

		if (level >= 2) {
			switch (directBranchWS.sync) {
			case TraceDqr::SYNC_EVTI:
				sr = "EVTI";
				break;
			case TraceDqr::SYNC_EXIT_RESET:
				sr = "Exit Reset";
				break;
			case TraceDqr::SYNC_T_CNT:
				sr = "T Count";
				break;
			case TraceDqr::SYNC_EXIT_DEBUG:
				sr = "Exit Debug";
				break;
			case TraceDqr::SYNC_I_CNT_OVERFLOW:
				sr = "I-Count Overflow";
				break;
			case TraceDqr::SYNC_TRACE_ENABLE:
				sr = "Trace Enable";
				break;
			case TraceDqr::SYNC_WATCHPINT:
				sr = "Watchpoint";
				break;
			case TraceDqr::SYNC_FIFO_OVERRUN:
				sr = "FIFO Overrun";
				break;
			case TraceDqr::SYNC_EXIT_POWERDOWN:
				sr = "Exit Powerdown";
				break;
			case TraceDqr::SYNC_MESSAGE_CONTENTION:
				sr = "Message Contention";
				break;
			case TraceDqr::SYNC_PC_SAMPLE:
				sr = "PC Sample";
				break;
			case TraceDqr::SYNC_NONE:
				sr = "None";
				break;
			default:
				sr = "Bad Sync Reason";
				break;
			}

			snprintf(dst+n,dst_len-n," Reason: (%d) %s I-CNT: %d F-Addr: 0x%08llx",directBranchWS.sync,sr,directBranchWS.i_cnt,directBranchWS.f_addr);
		}
		break;
	case TraceDqr::TCODE_INDIRECT_BRANCH_WS:
		n += snprintf(dst+n,dst_len-n,"INDIRECT BRANCH WS (%d)",tcode);

		if (level >= 2) {
			switch (indirectBranchWS.sync) {
			case TraceDqr::SYNC_EVTI:
				sr = "EVTI";
				break;
			case TraceDqr::SYNC_EXIT_RESET:
				sr = "Exit Reset";
				break;
			case TraceDqr::SYNC_T_CNT:
				sr = "T Count";
				break;
			case TraceDqr::SYNC_EXIT_DEBUG:
				sr = "Exit Debug";
				break;
			case TraceDqr::SYNC_I_CNT_OVERFLOW:
				sr = "I-Count Overflow";
				break;
			case TraceDqr::SYNC_TRACE_ENABLE:
				sr = "Trace Enable";
				break;
			case TraceDqr::SYNC_WATCHPINT:
				sr = "Watchpoint";
				break;
			case TraceDqr::SYNC_FIFO_OVERRUN:
				sr = "FIFO Overrun";
				break;
			case TraceDqr::SYNC_EXIT_POWERDOWN:
				sr = "Exit Powerdown";
				break;
			case TraceDqr::SYNC_MESSAGE_CONTENTION:
				sr = "Message Contention";
				break;
			case TraceDqr::SYNC_PC_SAMPLE:
				sr = "PC Sample";
				break;
			case TraceDqr::SYNC_NONE:
				sr = "None";
				break;
			default:
				sr = "Bad Sync Reason";
				break;
			}

			switch (indirectBranchWS.b_type) {
			case TraceDqr::BTYPE_INDIRECT:
				bt = "Indirect";
				break;
			case TraceDqr::BTYPE_EXCEPTION:
				bt = "Exception";
				break;
			case TraceDqr::BTYPE_HARDWARE:
				bt = "Hardware";
				break;
			case TraceDqr::BTYPE_UNDEFINED:
				bt = "Undefined";
				break;
			default:
				bt = "Bad Branch Type";
				break;
			}

			snprintf(dst+n,dst_len-n," Reason: (%d) %s Branch Type %s (%d) I-CNT: %d F-Addr: 0x%08llx",indirectBranchWS.sync,sr,bt,indirectBranchWS.b_type,indirectBranchWS.i_cnt,indirectBranchWS.f_addr);
		}
		break;
	case TraceDqr::TCODE_DATA_WRITE_WS:
		snprintf(dst+n,dst_len-n,"DATA WRITE WS (%d)",tcode);
		break;
	case TraceDqr::TCODE_DATA_READ_WS:
		snprintf(dst+n,dst_len-n,"DATA READ WS (%d)",tcode);
		break;
	case TraceDqr::TCODE_WATCHPOINT:
		snprintf(dst+n,dst_len-n,"TCode: WATCHPOINT (%d)",tcode);
		break;
	case TraceDqr::TCODE_OUTPUT_PORTREPLACEMENT:
		snprintf(dst+n,dst_len-n,"OUTPUT PORT REPLACEMENT (%d)",tcode);
		break;
	case TraceDqr::TCODE_INPUT_PORTREPLACEMENT:
		snprintf(dst+n,dst_len-n,"INPUT PORT REPLACEMENT (%d)",tcode);
		break;
	case TraceDqr::TCODE_AUXACCESS_READ:
		snprintf(dst+n,dst_len-n,"AUX ACCESS READ (%d)",tcode);
		break;
	case TraceDqr::TCODE_DATA_ACQUISITION:
		n += snprintf(dst+n,dst_len-n,"DATA ACQUISITION (%d)",tcode);

		if (level >= 2) { // here, if addr not on word boudry, have a partial write!
			switch (dataAcquisition.idTag & 0x03) {
			case 0:
			case 1:
				snprintf(dst+n,dst_len-n," idTag: 0x%08x Data: 0x%08x",dataAcquisition.idTag,dataAcquisition.data);
				break;
			case 2:
				snprintf(dst+n,dst_len-n," idTag: 0x%08x Data: 0x%04x",dataAcquisition.idTag,(uint16_t)dataAcquisition.data);
				break;
			case 3:
				snprintf(dst+n,dst_len-n," idTag: 0x%08x Data: 0x%02x",dataAcquisition.idTag,(uint8_t)dataAcquisition.data);
				break;
			}
		}
		break;
	case TraceDqr::TCODE_AUXACCESS_WRITE:
		n += snprintf(dst+n,dst_len-n,"AUX ACCESS WRITE (%d)",tcode);

		if (level >= 2) { // here, if addr not on word boudry, have a partial write!
			switch (auxAccessWrite.addr & 0x03) {
			case 0:
			case 1:
				snprintf(dst+n,dst_len-n," Addr: 0x%08x Data: 0x%08x",auxAccessWrite.addr,auxAccessWrite.data);
				break;
			case 2:
				snprintf(dst+n,dst_len-n," Addr: 0x%08x Data: 0x%04x",auxAccessWrite.addr,(uint16_t)auxAccessWrite.data);
				break;
			case 3:
				snprintf(dst+n,dst_len-n," Addr: 0x%08x Data: 0x%02x",auxAccessWrite.addr,(uint8_t)auxAccessWrite.data);
				break;
			}
		}
		break;
	case TraceDqr::TCODE_AUXACCESS_READNEXT:
		snprintf(dst+n,dst_len-n,"AUX ACCESS READNEXT (%d)",tcode);
		break;
	case TraceDqr::TCODE_AUXACCESS_WRITENEXT:
		snprintf(dst+n,dst_len-n,"AUX ACCESS WRITENEXT (%d)",tcode);
		break;
	case TraceDqr::TCODE_AUXACCESS_RESPONSE:
		snprintf(dst+n,dst_len-n,"AUXACCESS RESPOINSE (%d)",tcode);
		break;
	case TraceDqr::TCODE_RESOURCEFULL:
		n += snprintf(dst+n,dst_len-n,"RESOURCE FULL (%d)",tcode);

		if (level >= 2) { // here, if addr not on word boudry, have a partial write!
			n += snprintf(dst+n,dst_len-n," RCode: %d",resourceFull.rCode);

			switch (resourceFull.rCode) {
			case 0:
				snprintf(dst+n,dst_len-n," I-CNT: %d",resourceFull.i_cnt);
				break;
			case 1:
				snprintf(dst+n,dst_len-n," History: 0x%08llx",resourceFull.history);
				break;
			case 8:
				snprintf(dst+n,dst_len-n," Not Taken Count: %d",resourceFull.notTakenCount);
				break;
			case 9:
				snprintf(dst+n,dst_len-n," Taken Count: %d",resourceFull.takenCount);
				break;
			default:
				snprintf(dst+n,dst_len-n," Invalid rCode");
				break;
			}
		}
		break;
	case TraceDqr::TCODE_INDIRECTBRANCHHISTORY:
		n += snprintf(dst+n,dst_len-n,"INDIRECT BRANCH HISTORY (%d)",tcode);

		if (level >= 2) {
			switch (indirectHistory.b_type) {
			case TraceDqr::BTYPE_INDIRECT:
				bt = "Indirect";
				break;
			case TraceDqr::BTYPE_EXCEPTION:
				bt = "Exception";
				break;
			case TraceDqr::BTYPE_HARDWARE:
				bt = "Hardware";
				break;
			case TraceDqr::BTYPE_UNDEFINED:
				bt = "Undefined";
				break;
			default:
				bt = "Bad Branch Type";
				break;
			}

			snprintf(dst+n,dst_len-n," Branch Type: %s (%d) I-CNT: %d U-ADDR: 0x%08llx History: 0x%08llx",bt,indirectHistory.b_type,indirectHistory.i_cnt,indirectHistory.u_addr,indirectHistory.history);
		}
		break;
	case TraceDqr::TCODE_INDIRECTBRANCHHISTORY_WS:
		n += snprintf(dst+n,dst_len-n,"INDIRECT BRANCH HISTORY WS (%d)",tcode);

		if (level >= 2) {
			switch (indirectHistoryWS.sync) {
			case TraceDqr::SYNC_EVTI:
				sr = "EVTI";
				break;
			case TraceDqr::SYNC_EXIT_RESET:
				sr = "Exit Reset";
				break;
			case TraceDqr::SYNC_T_CNT:
				sr = "T Count";
				break;
			case TraceDqr::SYNC_EXIT_DEBUG:
				sr = "Exit Debug";
				break;
			case TraceDqr::SYNC_I_CNT_OVERFLOW:
				sr = "I-Count Overflow";
				break;
			case TraceDqr::SYNC_TRACE_ENABLE:
				sr = "Trace Enable";
				break;
			case TraceDqr::SYNC_WATCHPINT:
				sr = "Watchpoint";
				break;
			case TraceDqr::SYNC_FIFO_OVERRUN:
				sr = "FIFO Overrun";
				break;
			case TraceDqr::SYNC_EXIT_POWERDOWN:
				sr = "Exit Powerdown";
				break;
			case TraceDqr::SYNC_MESSAGE_CONTENTION:
				sr = "Message Contention";
				break;
			case TraceDqr::SYNC_PC_SAMPLE:
				sr = "PC Sample";
				break;
			case TraceDqr::SYNC_NONE:
				sr = "None";
				break;
			default:
				sr = "Bad Sync Reason";
				break;
			}

			switch (indirectHistoryWS.b_type) {
			case TraceDqr::BTYPE_INDIRECT:
				bt = "Indirect";
				break;
			case TraceDqr::BTYPE_EXCEPTION:
				bt = "Exception";
				break;
			case TraceDqr::BTYPE_HARDWARE:
				bt = "Hardware";
				break;
			case TraceDqr::BTYPE_UNDEFINED:
				bt = "Undefined";
				break;
			default:
				bt = "Bad Branch Type";
				break;
			}

			snprintf(dst+n,dst_len-n," Reason: (%d) %s Branch Type %s (%d) I-CNT: %d F-Addr: 0x%08llx History: 0x%08llx",indirectHistoryWS.sync,sr,bt,indirectHistoryWS.b_type,indirectHistoryWS.i_cnt,indirectHistoryWS.f_addr,indirectHistoryWS.history);
		}
		break;
	case TraceDqr::TCODE_REPEATBRANCH:
		snprintf(dst+n,dst_len-n,"REPEAT BRANCH (%d)",tcode);
		break;
	case TraceDqr::TCODE_REPEATINSTRUCTION:
		snprintf(dst+n,dst_len-n,"REPEAT INSTRUCTION (%d)",tcode);
		break;
	case TraceDqr::TCODE_REPEATINSTRUCTION_WS:
		snprintf(dst+n,dst_len-n,"REPEAT INSTRUCTIN WS (%d)",tcode);
		break;
	case TraceDqr::TCODE_CORRELATION:
		n += snprintf(dst+n,dst_len-n,"CORRELATION (%d)",tcode);

		if (level >= 2) {
			n += snprintf(dst+n,dst_len-n," EVCODE: %d CDF: %d I-CNT: %d",correlation.evcode,correlation.cdf,correlation.i_cnt);
			if (correlation.cdf > 0) {
				snprintf(dst+n,dst_len-n," History: 0x%08llx",correlation.history);
			}
		}
		break;
	case TraceDqr::TCODE_INCIRCUITTRACE:
		n += snprintf(dst+n,dst_len-n,"INCIRCUITTRACE (%d)",tcode);

		if (level >= 2) {
			switch (ict.cksrc) {
			case TraceDqr::ICT_EXT_TRIG:
				sr = "External Trigger";
				break;
			case TraceDqr::ICT_WATCHPOINT:
				sr = "Watchpoint";
				break;
			case TraceDqr::ICT_PC_SAMPLE:
				sr = "PC Sample";
				break;
			default:
				sr = "Bad ICT Reason";
				break;
			}

			snprintf(dst+n,dst_len-n," ICT Reason: %s (%d) U-ADDR: 0x%08llx",sr,ict.cksrc,ict.ckdata);
		}
		break;
	case TraceDqr::TCODE_INCIRCUITTRACE_WS:
		n += snprintf(dst+n,dst_len-n,"INCIRCUITTRACE WS (%d)",tcode);

		if (level >= 2) {
			switch (ict.cksrc) {
			case TraceDqr::ICT_EXT_TRIG:
				sr = "External Trigger";
				break;
			case TraceDqr::ICT_WATCHPOINT:
				sr = "Watchpoint";
				break;
			case TraceDqr::ICT_PC_SAMPLE:
				sr = "PC Sample";
				break;
			default:
				sr = "Bad ICT Reason";
				break;
			}

			snprintf(dst+n,dst_len-n," ICT Reason: %s (%d) F-ADDR: 0x%08llx",sr,ict.cksrc,ict.ckdata);
		}
		break;
	case TraceDqr::TCODE_UNDEFINED:
		snprintf(dst+n,dst_len-n,"UNDEFINED (%d)",tcode);
		break;
	default:
		snprintf(dst+n,dst_len-n,"BAD TCODE (%d)",tcode);
		break;
	}
}

double NexusMessage::seconds()
{
	if (haveTimestamp == false) {
		return 0.0;
	}

	if (targetFrequency != 0) {
		return ((double)time) / targetFrequency;
	}

	return (double)time;
}

void NexusMessage::dump()
{
	switch (tcode) {
	case TraceDqr::TCODE_DEBUG_STATUS:
		std::cout << "unsupported debug status trace message\n";
		break;
	case TraceDqr::TCODE_DEVICE_ID:
		std::cout << "unsupported device id trace message\n";
		break;
	case TraceDqr::TCODE_OWNERSHIP_TRACE:
//bks
		std::cout << "  # Trace Message(" << msgNum << "): Ownership, process=" << ownership.process; // << ", TS=0x" << hex << timestamp << dec; // << endl;
		break;
	case TraceDqr::TCODE_DIRECT_BRANCH:
//		cout << "(" << traceNum << ") ";
//		cout << "  | Direct Branch | TYPE=DBM, ICNT=" << i_cnt << ", TS=0x" << hex << timestamp << dec; // << endl;
//bks		cout << "  # Trace Message(" << msgNum << "): Direct Branch, ICNT=" << i_cnt; // << ", TS=0x" << hex << timestamp << dec; // << endl;
		break;
	case TraceDqr::TCODE_INDIRECT_BRANCH:
//		cout << "(" << traceNum << ") ";
//		cout << "  | Indirect Branch | TYPE=IBM, BTYPE=" << b_type << ", ICNT=" << i_cnt << ", UADDR=0x" << hex << u_addr << ", TS=0x" << timestamp << dec;// << endl;
//bks		cout << "  # Trace Message(" << msgNum << "): Indirect Branch, BTYPE=" << b_type << ", ICNT=" << i_cnt << ", UADDR=0x" << hex << u_addr << dec; // << ", TS=0x" << timestamp << dec;// << endl;
		break;
	case TraceDqr::TCODE_DATA_WRITE:
		std::cout << "unsupported data write trace message\n";
		break;
	case TraceDqr::TCODE_DATA_READ:
		std::cout << "unsupported data read trace message\n";
		break;
	case TraceDqr::TCODE_DATA_ACQUISITION:
		std::cout << "unsupported data acquisition trace message\n";
		break;
	case TraceDqr::TCODE_ERROR:
//bks		cout << "  # Trace Message(" << msgNum << "): Error, ETYPE=" << (uint32_t)etype; // << ", TS=0x" << timestamp << dec;// << endl;
		break;
	case TraceDqr::TCODE_SYNC:
//		cout << "(" << traceNum << "," << syncNum << ") ";
//		cout << "  | Sync | TYPE=SYNC, SYNC=" << sync << ", ICNT=" << i_cnt << ", FADDR=0x" << hex << f_addr << ", TS=0x" << timestamp << dec;// << endl;
//bks		cout << "  # Trace Message(" << msgNum << "): Sync, SYNCREASON=" << sync << ", ICNT=" << i_cnt << ", FADDR=0x" << hex << f_addr << dec; // << ", TS=0x" << timestamp << dec;// << endl;
		break;
	case TraceDqr::TCODE_CORRECTION:
		std::cout << "unsupported correction trace message\n";
		break;
	case TraceDqr::TCODE_DIRECT_BRANCH_WS:
//		cout << "(" << traceNum << "," << syncNum << ") ";
//		cout << "  | Direct Branch With Sync | TYPE=DBWS, SYNC=" << sync << ", ICNT=" << i_cnt << ", FADDR=0x" << hex << f_addr << ", TS=0x" << timestamp << dec;// << endl;
//bks		cout << "  # Trace Message(" << msgNum << "): Direct Branch With Sync, SYNCTYPE=" << sync << ", ICNT=" << i_cnt << ", FADDR=0x" << hex << f_addr << dec; // << ", TS=0x" << timestamp << dec;// << endl;
		break;
	case TraceDqr::TCODE_INDIRECT_BRANCH_WS:
//		cout << "(" << traceNum << "," << syncNum << ") ";
//		cout << "  | Indirect Branch With Sync | TYPE=IBWS, SYNC=" << sync << ", BTYPE=" << b_type << ", ICNT=" << i_cnt << ", FADDR=0x" << hex << f_addr << ", TS=0x" << timestamp << dec;// << endl;
//bks		cout << "  # Trace Message(" << msgNum << "): Indirect Branch With sync, SYNCTYPE=" << sync << ", BTYPE=" << b_type << ", ICNT=" << i_cnt << ", FADDR=0x" << hex << f_addr << dec; // << ", TS=0x" << timestamp << dec;// << endl;
		break;
	case TraceDqr::TCODE_DATA_WRITE_WS:
		std::cout << "unsupported data write with sync trace message\n";
		break;
	case TraceDqr::TCODE_DATA_READ_WS:
		std::cout << "unsupported data read with sync trace message\n";
		break;
	case TraceDqr::TCODE_WATCHPOINT:
		std::cout << "unsupported watchpoint trace message\n";
		break;
	case TraceDqr::TCODE_AUXACCESS_WRITE:
//bks		cout << "  # Trace Message(" << msgNum << "): Auxillary Access Write, address=" << hex << auxAddr << dec << ", data=" << hex << data << dec; // << ", TS=0x" << hex << timestamp << dec; // << endl;
		break;
	case TraceDqr::TCODE_CORRELATION:
//bks		cout << "  # Trace Message(" << msgNum << "): Correlation, EVCODE=" << (uint32_t)evcode << ", CDF=" << (int)cdf << ", ICNT=" << i_cnt << "\n"; // << ", TS=0x" << timestamp << dec;// << endl;
		break;
	default:
		std::cout << "Error: NexusMessage::dump(): Unknown TCODE " << std::hex << tcode << std::dec << "msgnum: " << msgNum << std::endl;
	}
}

Count::Count()
{
	for (int i = 0; i < (int)(sizeof i_cnt / sizeof i_cnt[0]); i++) {
		i_cnt[i] = 0;
	}

	for (int i = 0; i < (int)(sizeof history / sizeof history[0]); i++) {
		history[i] = 0;
	}

	for (int i = 0; i < (int)(sizeof histBit / sizeof histBit[0]); i++) {
		histBit[i] = -1;
	}

	for (int i = 0; i < (int)(sizeof takenCount / sizeof takenCount[0]); i++) {
		takenCount[i] = 0;
	}

	for (int i = 0; i < (int)(sizeof notTakenCount / sizeof notTakenCount[0]); i++) {
		notTakenCount[i] = 0;
	}
}

Count::~Count()
{
	// nothing to do here!
}

void Count::resetCounts(int core)
{
	i_cnt[core] = 0;
	histBit[core] = -1;
	takenCount[core] = 0;
	notTakenCount[core] = 0;
}

TraceDqr::CountType Count::getCurrentCountType(int core)
{
	// types are prioritized! Order is important

	if (histBit[core] >= 0) {
		return TraceDqr::COUNTTYPE_history;
	}

	if (takenCount[core] > 0) {
		return TraceDqr::COUNTTYPE_taken;
	}

	if (notTakenCount[core] > 0) {
		return TraceDqr::COUNTTYPE_notTaken;
	}

	// i-cnt is the lowest priority

	if (i_cnt[core] > 0) {
		return TraceDqr::COUNTTYPE_i_cnt;
	}

//	printf("count type: hist: %d taken:%d not taken:%d i_cnt: %d\n",histBit[core],takenCount[core],notTakenCount[core],i_cnt[core]);

	return TraceDqr::COUNTTYPE_none;
}

TraceDqr::DQErr Count::setICnt(int core,int count)
{
	i_cnt[core] += count;

	return TraceDqr::DQERR_OK;
}

TraceDqr::DQErr Count::setHistory(int core,uint64_t hist)
{
	TraceDqr::DQErr rc = TraceDqr::DQERR_OK;

	if (histBit[core] >= 0) {
		rc = TraceDqr::DQERR_ERR;
	}
	else if (takenCount[core] != 0) {
		rc = TraceDqr::DQERR_ERR;
	}
	else if (notTakenCount[core] != 0) {
		rc = TraceDqr::DQERR_ERR;
	}
	else if (hist == 0) {
		history[core] = 0;
		histBit[core] = -1;
	}
	else {
		history[core] = hist;

		int i;

		for (i = sizeof hist * 8 - 1; i >= 0; i -= 1) {
			if (hist & (((uint64_t)1) << i)) {
				histBit[core] = i-1;
				break;
			}
		}

		if (i < 0) {
			histBit[core] = -1;
		}
	}

	return rc;
}

TraceDqr::DQErr Count::setHistory(int core,uint64_t hist,int count)
{
	TraceDqr::DQErr rc;

	rc = setICnt(core,count);
	if (rc != TraceDqr::DQERR_OK) {
		return rc;
	}

	rc = setHistory(core,hist);
	if (rc != TraceDqr::DQERR_OK) {
		return rc;
	}

	return TraceDqr::DQERR_OK;
}

TraceDqr::DQErr Count::setTakenCount(int core,int takenCnt)
{
	TraceDqr::DQErr rc;

	if (histBit[core] >= 0) {
		rc = TraceDqr::DQERR_ERR;
	}
	else if (takenCount[core] != 0) {
		rc = TraceDqr::DQERR_ERR;
	}
	else if (notTakenCount[core] != 0) {
		rc = TraceDqr::DQERR_ERR;
	}
	else {
		takenCount[core] = takenCnt;
		rc = TraceDqr::DQERR_OK;
	}

	return rc;
}

TraceDqr::DQErr Count::setNotTakenCount(int core,int notTakenCnt)
{
	TraceDqr::DQErr rc;

	if (histBit[core] >= 0) {
		rc = TraceDqr::DQERR_ERR;
	}
	else if (takenCount[core] != 0) {
		rc = TraceDqr::DQERR_ERR;
	}
	else if (notTakenCount[core] != 0) {
		rc = TraceDqr::DQERR_ERR;
	}
	else {
		notTakenCount[core] = notTakenCnt;
		rc = TraceDqr::DQERR_OK;
	}

	return rc;
}

TraceDqr::DQErr Count::setCounts(NexusMessage *nm)
{
	TraceDqr::DQErr rc;
	int tmp_i_cnt = 0;
	uint64_t tmp_history = 0;
	int tmp_taken = 0;
	int tmp_notTaken = 0;

	switch (nm->tcode) {
	case TraceDqr::TCODE_DEBUG_STATUS:
	case TraceDqr::TCODE_DEVICE_ID:
	case TraceDqr::TCODE_OWNERSHIP_TRACE:
	case TraceDqr::TCODE_DATA_WRITE:
	case TraceDqr::TCODE_DATA_READ:
	case TraceDqr::TCODE_DATA_ACQUISITION:
	case TraceDqr::TCODE_ERROR:
	case TraceDqr::TCODE_CORRECTION:
	case TraceDqr::TCODE_AUXACCESS_WRITE:
	case TraceDqr::TCODE_INCIRCUITTRACE:
	case TraceDqr::TCODE_INCIRCUITTRACE_WS:
		// no counts, do nothing
		return TraceDqr::DQERR_OK;
	case TraceDqr::TCODE_DIRECT_BRANCH:
		tmp_i_cnt = nm->directBranch.i_cnt;
		break;
	case TraceDqr::TCODE_INDIRECT_BRANCH:
		tmp_i_cnt = nm->indirectBranch.i_cnt;
		break;
	case TraceDqr::TCODE_SYNC:
		tmp_i_cnt = nm->sync.i_cnt;
		break;
	case TraceDqr::TCODE_DIRECT_BRANCH_WS:
		tmp_i_cnt = nm->directBranchWS.i_cnt;
		break;
	case TraceDqr::TCODE_INDIRECT_BRANCH_WS:
		tmp_i_cnt = nm->indirectBranchWS.i_cnt;
		break;
	case TraceDqr::TCODE_RESOURCEFULL:
		switch (nm->resourceFull.rCode) {
		case 0:
			tmp_i_cnt = nm->resourceFull.i_cnt;
			break;
		case 1:
			tmp_history = nm->resourceFull.history;
			break;
		case 8:
			tmp_notTaken = nm->resourceFull.notTakenCount;
			break;
		case 9:
			tmp_taken = nm->resourceFull.takenCount;
			break;
		default:
			printf("Error: Count::setCount(): invalid or unsupported rCode for reourceFull TCODE\n");

			return TraceDqr::DQERR_ERR;
		}
		break;
	case TraceDqr::TCODE_INDIRECTBRANCHHISTORY:
		tmp_i_cnt = nm->indirectHistory.i_cnt;
		tmp_history = nm->indirectHistory.history;
		break;
	case TraceDqr::TCODE_INDIRECTBRANCHHISTORY_WS:
		tmp_i_cnt = nm->indirectHistoryWS.i_cnt;
		tmp_history = nm->indirectHistoryWS.history;
		break;
	case TraceDqr::TCODE_CORRELATION:
		tmp_i_cnt = nm->correlation.i_cnt;
		if (nm->correlation.cdf == 1) {
			tmp_history = nm->correlation.history;
		}
		break;
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
		printf("Error: Count::setCount(): invalid or unsupported TCODE\n");

		return TraceDqr::DQERR_ERR;
	}

	if (tmp_i_cnt != 0) {
		rc = setICnt(nm->coreId,tmp_i_cnt);
		if (rc != TraceDqr::DQERR_OK) {
			return rc;
		}
	}

	if (tmp_history != 0) {
		rc = setHistory(nm->coreId,tmp_history);
		if (rc != TraceDqr::DQERR_OK) {
			return rc;
		}
	}

	if (tmp_taken != 0) {
		rc = setTakenCount(nm->coreId,tmp_taken);
		if (rc != TraceDqr::DQERR_OK) {
			return rc;
		}
	}

	if (tmp_notTaken != 0) {
		rc = setNotTakenCount(nm->coreId,tmp_notTaken);
		if (rc != TraceDqr::DQERR_OK) {
			return rc;
		}
	}

	return TraceDqr::DQERR_OK;
}

int Count::consumeICnt(int core,int numToConsume)
{
	i_cnt[core] -= numToConsume;

	return i_cnt[core];
}

int Count::consumeHistory(int core,bool &taken)
{
	if (histBit[core] < 0) {
		return 1;
	}

	taken = (history[core] & (1 << histBit[core])) != 0;

	histBit[core] -= 1;

	return 0;
}

int Count::consumeTakenCount(int core)
{
	if (takenCount[core] <= 0) {
		return 1;
	}

	takenCount[core] -= 1;

	return 0;
}

int Count::consumeNotTakenCount(int core)
{
	if (notTakenCount[core] <= 0) {
		return 1;
	}

	notTakenCount[core] -= 1;

	return 0;
}

void Count::dumpCounts(int core)
{
	printf("Count::dumpCounts(): core: %d, i_cnt: %d, history: 0x%08llx, histBit: %d, takenCount: %d, notTakenCount: %d\n",core,i_cnt[core],history[core],histBit[core],takenCount[core],notTakenCount[core]);
}

SliceFileParser::SliceFileParser(char *filename, bool binary, int srcBits)
{
	assert(filename != nullptr);

	srcbits = srcBits;

	msgSlices      = 0;
	bitIndex       = 0;

	this->binary = binary;

	tfSize = 0;

	if (binary) {
		tf.open(filename, std::ios::in | std::ios::binary);
	}
	else {
		tf.open(filename, std::ios::in);
	}

	if (!tf) {
		printf("Error: SliceFileParder(): could not open file %s for input\n",filename);
		status = TraceDqr::DQERR_OPEN;
	}
	else {
		status = TraceDqr::DQERR_OK;
	}

	tf.seekg (0, tf.end);
	tfSize = tf.tellg();
	tf.seekg (0, tf.beg);

	msgOffset = 0;

#if	0
	// read entire slice file, create multiple quese base on src field

	readAllTraceMsgs();
#endif
}

SliceFileParser::~SliceFileParser()
{
	if (tf.is_open()) {
		tf.close();
	}
}

TraceDqr::DQErr SliceFileParser::getFileOffset(int &size,int &offset)
{
	if (!tf.is_open()) {
		return TraceDqr::DQERR_ERR;
	}

	size = tfSize;
	offset = tf.tellg();

	return TraceDqr::DQERR_OK;
}

void SliceFileParser::dump()
{
	//msg and msgSlices

	for (int i = 0; i < msgSlices; i++) {
		std::cout << std::setw(2) << i+1;
		std::cout << " | ";
		std::cout << std::setfill('0');
		std::cout << std::setw(2) << std::hex << int(msg[i] >> 2) << std::dec;
		std::cout << std::setfill(' ');
		std::cout << " | ";
		std::cout << int((msg[i] >> 1) & 1);
		std::cout << " ";
		std::cout << int(msg[i] & 1);
		std::cout << std::endl;
	}
}

TraceDqr::DQErr SliceFileParser::parseICT(NexusMessage &nm,Analytics &analytics)
{
	TraceDqr::DQErr rc;
	uint64_t   tmp;
	int        width;
	int        bits = 6;	// start bits at 6 to account for tcode
	int        ts_bits = 0;
	int        addr_bits;

	nm.tcode = TraceDqr::TCODE_INCIRCUITTRACE;

	if (srcbits > 0) {
        rc = parseFixedField(srcbits,&tmp);
        if (rc != TraceDqr::DQERR_OK) {
            status = rc;

            return status;
        }

        bits += srcbits;

        nm.coreId = (uint8_t)tmp;
	}
	else {
		nm.coreId = 0;
	}

	// parse the four bit CKSRC (reason for ICT)

	rc = parseFixedField(4,&tmp);
	if (rc  != TraceDqr::DQERR_OK) {
		status = rc;

		return status;
	}

	bits += 4;

	nm.ict.cksrc = (TraceDqr::ICTReason)tmp;

	// parse the 2 bit CKDF field (number of CKDATA fields: 0 means 1, etc)

	rc = parseFixedField(2,&tmp);
	if (rc != TraceDqr::DQERR_OK) {
		status = rc;

		return status;
	}

	bits += 2;

	if (tmp != 0) {
		status = TraceDqr::DQERR_ERR;

		return status;
	}

	nm.ict.ckdf = (uint8_t)tmp;

	// parse the variable length the CKDATA field

	rc = parseVarField(&tmp,&width);
	if (rc != TraceDqr::DQERR_OK) {
		status = rc;

		return status;
	}

	bits += width;
	addr_bits = width;

    nm.ict.ckdata = (TraceDqr::ADDRESS)tmp;

	if (eom == true) {
		nm.haveTimestamp = false;
		nm.timestamp = 0;
	}
	else {
		rc = parseVarField(&tmp,&width); // this field is optional - check err
		if (rc != TraceDqr::DQERR_OK) {
			status = rc;

			return status;
		}

		bits += width;
		ts_bits = width;

		// check if entire message has been consumed

		if (eom != true) {
			status = TraceDqr::DQERR_BM;

			return status;
		}

		nm.haveTimestamp = true;
		nm.timestamp = (TraceDqr::TIMESTAMP)tmp;
	}

	status = analytics.updateTraceInfo(nm,bits+msgSlices*2,msgSlices*2,ts_bits,addr_bits);

	nm.msgNum = analytics.currentTraceMsgNum();

	return status;
}

TraceDqr::DQErr SliceFileParser::parseICTWS(NexusMessage &nm,Analytics &analytics)
{
	TraceDqr::DQErr rc;
	uint64_t   tmp;
	int        width;
	int        bits = 6;	// start bits at 6 to account for tcode
	int        ts_bits = 0;
	int        addr_bits;

	nm.tcode = TraceDqr::TCODE_INCIRCUITTRACE_WS;

	if (srcbits > 0) {
        rc = parseFixedField(srcbits,&tmp);
        if (rc != TraceDqr::DQERR_OK) {
            status = rc;

            return status;
        }

        bits += srcbits;

        nm.coreId = (uint8_t)tmp;
	}
	else {
		nm.coreId = 0;
	}

	// parse the four bit CKSRC (reason for ICT)

	rc = parseFixedField(4,&tmp);
	if (rc  != TraceDqr::DQERR_OK) {
		status = rc;

		return status;
	}

	bits += 4;

	nm.ictWS.cksrc = (TraceDqr::ICTReason)tmp;

	// parse the 2 bit CKDF field (number of CKDATA fields: 0 means 1, etc)

	rc = parseFixedField(2,&tmp);
	if (rc != TraceDqr::DQERR_OK) {
		status = rc;

		return status;
	}

	bits += 2;

	if (tmp != 0) {
		status = TraceDqr::DQERR_ERR;

		return status;
	}

	nm.ictWS.ckdf = (uint8_t)tmp;

	// parse the variable length the CKDATA field

	rc = parseVarField(&tmp,&width);
	if (rc != TraceDqr::DQERR_OK) {
		status = rc;

		return status;
	}

	bits += width;
	addr_bits = width;

    nm.ictWS.ckdata = (TraceDqr::ADDRESS)tmp;

	if (eom == true) {
		nm.haveTimestamp = false;
		nm.timestamp = 0;
	}
	else {
		rc = parseVarField(&tmp,&width); // this field is optional - check err
		if (rc != TraceDqr::DQERR_OK) {
			status = rc;

			return status;
		}

		bits += width;
		ts_bits = width;

		// check if entire message has been consumed

		if (eom != true) {
			status = TraceDqr::DQERR_BM;

			return status;
		}

		nm.haveTimestamp = true;
		nm.timestamp = (TraceDqr::TIMESTAMP)tmp;
	}

	status = analytics.updateTraceInfo(nm,bits+msgSlices*2,msgSlices*2,ts_bits,addr_bits);

	nm.msgNum = analytics.currentTraceMsgNum();

	return status;
}

TraceDqr::DQErr SliceFileParser::parseIndirectHistory(NexusMessage &nm,Analytics &analytics)
{
	TraceDqr::DQErr rc;
	uint64_t   tmp;
	int        width;
	int        bits = 6;	// start bits at 6 to account for tcode
	int        ts_bits = 0;
	int        addr_bits;

	nm.tcode = TraceDqr::TCODE_INDIRECTBRANCHHISTORY;

	if (srcbits > 0) {
        rc = parseFixedField(srcbits,&tmp);
        if (rc != TraceDqr::DQERR_OK) {
            status = rc;

            return status;
        }

        bits += srcbits;

        nm.coreId = (uint8_t)tmp;
	}
	else {
		nm.coreId = 0;
	}

	// parse the two bit b-type

	rc = parseFixedField(2,&tmp);
	if (rc  != TraceDqr::DQERR_OK) {
		status = rc;

		return status;
	}

	bits += 2;

	nm.indirectHistory.b_type = (TraceDqr::BType)tmp;

	// parse the variable length the i-cnt

	rc = parseVarField(&tmp,&width);
	if (rc != TraceDqr::DQERR_OK) {
		status = rc;

		return status;
	}

	bits += width;

    nm.indirectHistory.i_cnt = (int)tmp;

    // parse the u-addr field

    rc = parseVarField(&tmp,&width);
    if (rc != TraceDqr::DQERR_OK) {
    	status = rc;

    	return status;
    }

    bits += width;
	addr_bits = width;

    nm.indirectHistory.u_addr = (TraceDqr::ADDRESS)tmp;

    // parse the variable lenght history field

    rc = parseVarField(&tmp,&width);
    if (rc != TraceDqr::DQERR_OK) {
    	status = rc;

    	return status;
    }

    bits += width;

    nm.indirectHistory.history = tmp;

	if (eom == true) {
		nm.haveTimestamp = false;
		nm.timestamp = 0;
	}
	else {
		rc = parseVarField(&tmp,&width); // this field is optional - check err
		if (rc != TraceDqr::DQERR_OK) {
			status = rc;

			return status;
		}

		bits += width;
		ts_bits = width;

		// check if entire message has been consumed

		if (eom != true) {
			status = TraceDqr::DQERR_BM;

			return status;
		}

		nm.haveTimestamp = true;
		nm.timestamp = (TraceDqr::TIMESTAMP)tmp;
	}

	status = analytics.updateTraceInfo(nm,bits+msgSlices*2,msgSlices*2,ts_bits,addr_bits);

	nm.msgNum = analytics.currentTraceMsgNum();

	return status;
}

TraceDqr::DQErr SliceFileParser::parseIndirectHistoryWS(NexusMessage &nm,Analytics &analytics)
{
	TraceDqr::DQErr rc;
	uint64_t   tmp;
	int        width;
	int        bits = 6;	// start bits at 6 to account for tcode
	int        ts_bits = 0;
	int        addr_bits;

	nm.tcode = TraceDqr::TCODE_INDIRECTBRANCHHISTORY_WS;

	// if multicore, parse src field

	if (srcbits > 0) {
        rc = parseFixedField(srcbits,&tmp);
        if (rc != TraceDqr::DQERR_OK) {
            status = rc;

            return status;
        }

        bits += srcbits;

        nm.coreId = (uint8_t)tmp;
	}
	else {
		nm.coreId = 0;
	}

	// parse the fixed length sync reason field

	rc = parseFixedField(4,&tmp);
	if (rc != TraceDqr::DQERR_OK) {
		status = rc;

		return status;
	}

	bits += 4;

	nm.indirectHistoryWS.sync   = (TraceDqr::SyncReason)tmp;

	// parse the two bit b-type

	rc = parseFixedField(2,&tmp);
	if (rc  != TraceDqr::DQERR_OK) {
		status = rc;

		return status;
	}

	bits += 2;

	nm.indirectHistoryWS.b_type = (TraceDqr::BType)tmp;

	// parse the variable length the i-cnt

	rc = parseVarField(&tmp,&width);
	if (rc != TraceDqr::DQERR_OK) {
		status = rc;

		return status;
	}

	bits += width;

    nm.indirectHistoryWS.i_cnt = (int)tmp;

    // parse the f-addr field

    rc = parseVarField(&tmp,&width);
    if (rc != TraceDqr::DQERR_OK) {
    	status = rc;

    	return status;
    }

    bits += width;
	addr_bits = width;

    nm.indirectHistoryWS.f_addr = (TraceDqr::ADDRESS)tmp;

    // parse the variable length history field

    rc = parseVarField(&tmp,&width);
    if (rc != TraceDqr::DQERR_OK) {
    	status = rc;

    	return status;
    }

    bits += width;

    nm.indirectHistoryWS.history = tmp;

	if (eom == true) {
		nm.haveTimestamp = false;
		nm.timestamp = 0;
	}
	else {
		rc = parseVarField(&tmp,&width); // this field is optional - check err
		if (rc != TraceDqr::DQERR_OK) {
			status = rc;

			return status;
		}

		bits += width;
		ts_bits = width;

		// check if entire message has been consumed

		if (eom != true) {
			status = TraceDqr::DQERR_BM;

			return status;
		}

		nm.haveTimestamp = true;
		nm.timestamp = (TraceDqr::TIMESTAMP)tmp;
	}

	status = analytics.updateTraceInfo(nm,bits+msgSlices*2,msgSlices*2,ts_bits,addr_bits);

	nm.msgNum = analytics.currentTraceMsgNum();

	return status;
}

TraceDqr::DQErr SliceFileParser::parseResourceFull(NexusMessage &nm,Analytics &analytics)
{
	TraceDqr::DQErr rc;
	uint64_t   tmp;
	int        width;
	int        bits = 6;	// start bits at 6 to account for tcode
	int        ts_bits = 0;

	nm.tcode = TraceDqr::TCODE_RESOURCEFULL;

	// if multicore, parse src field

	if (srcbits > 0) {
        rc = parseFixedField(srcbits,&tmp);
        if (rc != TraceDqr::DQERR_OK) {
            status = rc;

            return status;
        }

        bits += srcbits;

        nm.coreId = (uint8_t)tmp;
	}
	else {
		nm.coreId = 0;
	}

	// parse the 4 bit RCODE

	rc = parseFixedField(4,&tmp);
	if (rc != TraceDqr::DQERR_OK) {
		status = rc;

		return status;
	}

	bits += 4;

    nm.resourceFull.rCode = (int)tmp;

    // parse the vairable length rdata field

    rc = parseVarField(&tmp,&width);
    if (rc != TraceDqr::DQERR_OK) {
    	status = rc;

    	return status;
    }

    bits += width;

    switch (nm.resourceFull.rCode) {
    case 0:
    	nm.resourceFull.i_cnt = (int)tmp;
    	break;
    case 1:
    	nm.resourceFull.history = tmp;
    	break;
    case 8:
    	nm.resourceFull.notTakenCount = (int)tmp;
    	break;
    case 9:
    	nm.resourceFull.takenCount = (int)tmp;
    	break;
    default:
    	printf("Error: parseResourceFull(): unknown rCode: %d\n",nm.resourceFull.rCode);

    	status = TraceDqr::DQERR_ERR;
    	return status;
    }

	if (eom == true) {
		nm.haveTimestamp = false;
		nm.timestamp = 0;
	}
	else {
		rc = parseVarField(&tmp,&width); // this field is optional - check err
		if (rc != TraceDqr::DQERR_OK) {
			status = rc;

			return status;
		}

		bits += width;
		ts_bits = width;

		// check if entire message has been consumed

		if (eom != true) {
			status = TraceDqr::DQERR_BM;

			return status;
		}

		nm.haveTimestamp = true;
		nm.timestamp = (TraceDqr::TIMESTAMP)tmp;
	}

	status = analytics.updateTraceInfo(nm,bits+msgSlices*2,msgSlices*2,ts_bits,0);

	nm.msgNum = analytics.currentTraceMsgNum();

	return status;
}

TraceDqr::DQErr SliceFileParser::parseDirectBranch(NexusMessage &nm,Analytics &analytics)
{
	TraceDqr::DQErr rc;
	uint64_t   tmp;
	int        width;
	int        bits = 6;	// start bits at 6 to account for tcode
	int        ts_bits = 0;

	nm.tcode = TraceDqr::TCODE_DIRECT_BRANCH;

	// if multicore, parse src field

	if (srcbits > 0) {
        rc = parseFixedField(srcbits,&tmp);
        if (rc != TraceDqr::DQERR_OK) {
            status = rc;

            return status;
        }

        bits += srcbits;

        nm.coreId = (uint8_t)tmp;
	}
	else {
		nm.coreId = 0;
	}

	// parse the variable length the i-cnt

	rc = parseVarField(&tmp,&width);
	if (rc != TraceDqr::DQERR_OK) {
		status = rc;

		return status;
	}

	bits += width;

    nm.directBranch.i_cnt = (int)tmp;

	if (eom == true) {
		nm.haveTimestamp = false;
		nm.timestamp = 0;
	}
	else {
		rc = parseVarField(&tmp,&width); // this field is optional - check err
		if (rc != TraceDqr::DQERR_OK) {
			status = rc;

			return status;
		}

		bits += width;
		ts_bits = width;

		// check if entire message has been consumed

		if (eom != true) {
			status = TraceDqr::DQERR_BM;

			return status;
		}

		nm.haveTimestamp = true;
		nm.timestamp = (TraceDqr::TIMESTAMP)tmp;
	}

	status = analytics.updateTraceInfo(nm,bits+msgSlices*2,msgSlices*2,ts_bits,0);

	nm.msgNum = analytics.currentTraceMsgNum();

	return status;
}

TraceDqr::DQErr SliceFileParser::parseDirectBranchWS(NexusMessage &nm,Analytics &analytics)
{
	TraceDqr::DQErr rc;
	uint64_t   tmp;
	int        width;
	int        bits = 6;
	int        ts_bits = 0;
	int        addr_bits;

	nm.tcode = TraceDqr::TCODE_DIRECT_BRANCH_WS;

	// if multicore, parse src field

	if (srcbits > 0) {
        rc = parseFixedField(srcbits,&tmp);
        if (rc != TraceDqr::DQERR_OK) {
            status = rc;

            return status;
        }

        bits += srcbits;

        nm.coreId = (uint8_t)tmp;
	}
	else {
		nm.coreId = 0;
	}

	// parse the fixed length sync reason field

	rc = parseFixedField(4,&tmp);
	if (rc != TraceDqr::DQERR_OK) {
		status = rc;

		return status;
	}

	bits += 4;

	nm.directBranchWS.sync   = (TraceDqr::SyncReason)tmp;

	// parse the variable length the i-cnt

	rc = parseVarField(&tmp,&width);
	if (rc != TraceDqr::DQERR_OK) {
		status = rc;

		return status;
	}

	bits += width;

	nm.directBranchWS.i_cnt  = (int)tmp;

	rc = parseVarField(&tmp,&width);
	if (rc != TraceDqr::DQERR_OK) {
		status = rc;

		return status;
	}

	bits += width;
	addr_bits = width;

	nm.directBranchWS.f_addr = (TraceDqr::ADDRESS)tmp;

	if (eom == true) {
		nm.haveTimestamp = false;
		nm.timestamp = 0;
	}
	else {
		rc = parseVarField(&tmp,&width); // this field is optional - check err
		if (rc != TraceDqr::DQERR_OK) {
			status = rc;

			return status;
		}

		bits += width;
		ts_bits = width;

		// check if entire message has been consumed

		if (eom != true) {
			status = TraceDqr::DQERR_BM;

			return status;
		}

		nm.haveTimestamp = true;
		nm.timestamp = (TraceDqr::TIMESTAMP)tmp;
	}

	status = analytics.updateTraceInfo(nm,bits+msgSlices*2,msgSlices*2,ts_bits,addr_bits);

	nm.msgNum = analytics.currentTraceMsgNum();

	return status;
}

TraceDqr::DQErr SliceFileParser::parseIndirectBranch(NexusMessage &nm,Analytics &analytics)
{
	TraceDqr::DQErr rc;
	uint64_t   tmp;
	int        width;
	int        bits = 6;
	int        ts_bits = 0;
	int        addr_bits;

	nm.tcode = TraceDqr::TCODE_INDIRECT_BRANCH;

	// if multicore, parse src field

	if (srcbits > 0) {
        rc = parseFixedField(srcbits,&tmp);
        if (rc != TraceDqr::DQERR_OK) {
            status = rc;

            return status;
        }

        bits += srcbits;

        nm.coreId = (uint8_t)tmp;
	}
	else {
		nm.coreId = 0;
	}

	// parse the fixed length b-type

	rc = parseFixedField(2,&tmp);
	if (rc != TraceDqr::DQERR_OK) {
		status = rc;

		return status;
	}

	bits += 2;

	nm.indirectBranch.b_type = (TraceDqr::BType)tmp;

	// parse the variable length the i-cnt

	rc = parseVarField(&tmp,&width);
	if (rc != TraceDqr::DQERR_OK) {
		status = rc;

		return status;
	}

	bits += width;

	nm.indirectBranch.i_cnt  = (int)tmp;

	rc = parseVarField(&tmp,&width);
	if (rc != TraceDqr::DQERR_OK) {
		status = rc;

		return status;
	}

	bits += width;
	addr_bits = width;

	nm.indirectBranch.u_addr = (TraceDqr::ADDRESS)tmp;

	if (eom == true) {
		nm.haveTimestamp = false;
		nm.timestamp = 0;
	}
	else {
		rc = parseVarField(&tmp,&width); // this field is optional - check err
		if (rc != TraceDqr::DQERR_OK) {
			status = rc;

			return status;
		}

		bits += width;
		ts_bits = width;

		// check if entire message has been consumed

		if (eom != true) {
			status = TraceDqr::DQERR_BM;

			return status;
		}

		nm.haveTimestamp = true;
		nm.timestamp = (TraceDqr::TIMESTAMP)tmp;
	}

	status = analytics.updateTraceInfo(nm,bits+msgSlices*2,msgSlices*2,ts_bits,addr_bits);

	nm.msgNum = analytics.currentTraceMsgNum();

	return status;
}

TraceDqr::DQErr SliceFileParser::parseIndirectBranchWS(NexusMessage &nm,Analytics &analytics)
{
	TraceDqr::DQErr rc;
	uint64_t   tmp;
	int        width;
	int        bits = 6;
	int        ts_bits = 0;
	int        addr_bits;

	nm.tcode = TraceDqr::TCODE_INDIRECT_BRANCH_WS;

	// if multicore, parse src field

	if (srcbits > 0) {
        rc = parseFixedField(srcbits,&tmp);
        if (rc != TraceDqr::DQERR_OK) {
            status = rc;

            return status;
        }

        bits += srcbits;

        nm.coreId = (uint8_t)tmp;
	}
	else {
		nm.coreId = 0;
	}

	// parse the fixed length sync field

	rc = parseFixedField(4,&tmp);
	if (rc != TraceDqr::DQERR_OK) {
		status = rc;

		return status;
	}

	bits += 4;

	nm.indirectBranchWS.sync = (TraceDqr::SyncReason)tmp;

	// parse the fixed length b-type

	rc = parseFixedField(2,&tmp);
	if (rc != TraceDqr::DQERR_OK) {
		status = rc;

		return status;
	}

	bits += 2;

	nm.indirectBranchWS.b_type = (TraceDqr::BType)tmp;

	// parse the variable length the i-cnt

	rc = parseVarField(&tmp,&width);
	if (rc != TraceDqr::DQERR_OK) {
		status = rc;

		return status;
	}

	bits += width;

	nm.indirectBranchWS.i_cnt = (int)tmp;

	rc = parseVarField(&tmp,&width);
	if (rc != TraceDqr::DQERR_OK) {
		status = rc;

		return status;
	}

	bits += width;
	addr_bits = width;

	nm.indirectBranchWS.f_addr = (TraceDqr::ADDRESS)tmp;

	if (eom == true) {
		nm.haveTimestamp = false;
		nm.timestamp = 0;
	}
	else {
		rc = parseVarField(&tmp,&width); // this field is optional - check err
		if (rc != TraceDqr::DQERR_OK) {
			status = rc;

			return status;
		}

		// check if entire message has been consumed

		if (eom != true) {
			status = TraceDqr::DQERR_BM;

			return status;
		}

		bits += width;
		ts_bits = width;

		nm.haveTimestamp = true;
		nm.timestamp = (TraceDqr::TIMESTAMP)tmp;
	}

	status = analytics.updateTraceInfo(nm,bits+msgSlices*2,msgSlices*2,ts_bits,addr_bits);

	nm.msgNum = analytics.currentTraceMsgNum();

	return status;
}

TraceDqr::DQErr SliceFileParser::parseSync(NexusMessage &nm,Analytics &analytics)
{
	TraceDqr::DQErr rc;
	uint64_t   tmp;
	int        width;
	int        bits = 6;
	int        ts_bits = 0;
	int        addr_bits;

	nm.tcode         = TraceDqr::TCODE_SYNC;

	// if multicore, parse src field

	if (srcbits > 0) {
        rc = parseFixedField(srcbits,&tmp);
        if (rc != TraceDqr::DQERR_OK) {
            status = rc;

            return status;
        }

        bits += srcbits;

        nm.coreId = (uint8_t)tmp;
	}
	else {
		nm.coreId = 0;
	}

	// parse the sync resons

	rc = parseFixedField(4,&tmp);
	if (rc != TraceDqr::DQERR_OK) {
		status = rc;

		return status;
	}

	bits += 4;

	nm.sync.sync     = (TraceDqr::SyncReason)tmp;

	rc = parseVarField(&tmp,&width);
	if (rc != TraceDqr::DQERR_OK) {
		status = rc;

		return status;
	}

	bits += width;

	nm.sync.i_cnt    = (int)tmp;

	rc = parseVarField(&tmp,&width);
	if (rc != TraceDqr::DQERR_OK) {
		status = rc;

		return status;
	}

	bits += width;
	addr_bits = width;

	nm.sync.f_addr   = (TraceDqr::ADDRESS)tmp;

	if (eom == true) {
		nm.haveTimestamp = false;
		nm.timestamp = 0;
	}
	else {
		rc = parseVarField(&tmp,&width); // this field is optional - check err
		if (rc != TraceDqr::DQERR_OK) {
			status = rc;

			return status;
		}

		// check if entire message has been consumed

		if (eom != true) {
			status = TraceDqr::DQERR_BM;

			return status;
		}

		bits += width;
		ts_bits = width;

		nm.haveTimestamp = true;
		nm.timestamp = (TraceDqr::TIMESTAMP)tmp;
	}

	status = analytics.updateTraceInfo(nm,bits+msgSlices*2,msgSlices*2,ts_bits,addr_bits);

	nm.msgNum = analytics.currentTraceMsgNum();

	return status;
}

TraceDqr::DQErr SliceFileParser::parseCorrelation(NexusMessage &nm,Analytics &analytics)
{
	TraceDqr::DQErr rc;
	uint64_t   tmp;
	int        width;
	int        bits = 6;
	int        ts_bits = 0;

	nm.tcode = TraceDqr::TCODE_CORRELATION;

//	printf("parse correlation\n");

	// if multicore, parse src field

	if (srcbits > 0) {
        rc = parseFixedField(srcbits,&tmp);
        if (rc != TraceDqr::DQERR_OK) {
            status = rc;

            return status;
        }

        bits += srcbits;

        nm.coreId = (uint8_t)tmp;
	}
	else {
		nm.coreId = 0;
	}

	// parse the 4-bit evcode field

	rc = parseFixedField(4,&tmp);
	if (rc != TraceDqr::DQERR_OK) {
		status = rc;

		return status;
	}

	bits += 4;

	nm.correlation.evcode = (uint8_t)tmp;

	// parse the 2-bit cdf field.

	rc = parseFixedField(2,&tmp);
	if (rc != TraceDqr::DQERR_OK) {
		status = rc;

		return status;
	}

	bits += 2;

	nm.correlation.cdf = (uint8_t)tmp;

	// parse the variable length i-cnt field

	rc = parseVarField(&tmp,&width);
	if (rc != TraceDqr::DQERR_OK) {
		status = rc;

		return status;
	}

	bits += width;

	nm.correlation.i_cnt = (int)tmp;

	switch (nm.correlation.cdf) {
	case 0:
		break;
	case 1:
		rc = parseVarField(&tmp,&width);
		if (rc != TraceDqr::DQERR_OK) {
			status = rc;

			return status;
		}

		bits += width;

		nm.correlation.history = (int)tmp;

		break;
	default:
		printf("Error: parseCorrelation(): invalid CDF field: %d\n",nm.correlation.cdf);

		status = TraceDqr::DQERR_ERR;
		return status;
	}

	if (eom == true) {
		nm.haveTimestamp = false;
		nm.timestamp = 0;
	}
	else {
		rc = parseVarField(&tmp,&width); // this field is optional - check err
		if (rc != TraceDqr::DQERR_OK) {
			status = rc;

			return status;
		}

		// check if entire message has been consumed

		if (eom != true) {
			status = TraceDqr::DQERR_BM;

			return status;
		}

		bits += width;
		ts_bits = width;

		nm.haveTimestamp = true;
		nm.timestamp = (TraceDqr::TIMESTAMP)tmp;
	}

	status = analytics.updateTraceInfo(nm,bits+msgSlices*2,msgSlices*2,ts_bits,0);

	nm.msgNum = analytics.currentTraceMsgNum();

	return status;
}

TraceDqr::DQErr SliceFileParser::parseError(NexusMessage &nm,Analytics &analytics)
{
	TraceDqr::DQErr rc;
	uint64_t   tmp;
	int        width;
	int        bits = 6;
	int        ts_bits = 0;

	nm.tcode = TraceDqr::TCODE_ERROR;

	// if multicore, parse src field

	if (srcbits > 0) {
        rc = parseFixedField(srcbits,&tmp);
        if (rc != TraceDqr::DQERR_OK) {
            status = rc;

            return status;
        }

        bits += srcbits;

        nm.coreId = (uint8_t)tmp;
	}
	else {
		nm.coreId = 0;
	}

	// parse the 4 bit ETYPE field

	rc = parseFixedField(4,&tmp);
	if (rc != TraceDqr::DQERR_OK) {
		status = rc;

		return status;
	}

	bits += 4;

	nm.error.etype = (uint8_t)tmp;

	// parse the variable sized padd field

	rc = parseVarField(&tmp,&width);
	if (rc != TraceDqr::DQERR_OK) {
		status = rc;

		return status;
	}

	bits += width;

	if (eom == true) {
		nm.haveTimestamp = false;
		nm.timestamp = 0;
	}
	else {
		rc = parseVarField(&tmp,&width); // this field is optional - check err
		if (rc != TraceDqr::DQERR_OK) {
			status = rc;

			return status;
		}

		// check if entire message has been consumed

		if (eom != true) {
			status = TraceDqr::DQERR_BM;

			return status;
		}

		bits += width;
		ts_bits = width;

		nm.haveTimestamp = true;
		nm.timestamp = (TraceDqr::TIMESTAMP)tmp;
	}

	status = analytics.updateTraceInfo(nm,bits+msgSlices*2,msgSlices*2,ts_bits,0);

	nm.msgNum = analytics.currentTraceMsgNum();

	return status;
}

TraceDqr::DQErr SliceFileParser::parseOwnershipTrace(NexusMessage &nm,Analytics &analytics)
{
	TraceDqr::DQErr rc;
	uint64_t   tmp;
	int        width;
	int        bits = 6;
	int        ts_bits = 0;

	nm.tcode = TraceDqr::TCODE_OWNERSHIP_TRACE;

	// if multicore, parse src field

	if (srcbits > 0) {
        rc = parseFixedField(srcbits,&tmp);
        if (rc != TraceDqr::DQERR_OK) {
            status = rc;

            return status;
        }

        bits += srcbits;

        nm.coreId = (uint8_t)tmp;
	}
	else {
		nm.coreId = 0;
	}

	// parse the variable length process ID field

	rc = parseVarField(&tmp,&width);
	if (rc != TraceDqr::DQERR_OK) {
		status = rc;

		return status;
	}

	bits += width;

	nm.ownership.process = (int)tmp;

	if (eom == true) {
		nm.haveTimestamp = false;
		nm.timestamp = 0;
	}
	else {
		rc = parseVarField(&tmp,&width); // this field is optional - check err
		if (rc != TraceDqr::DQERR_OK) {
			status = rc;

			return status;
		}

		// check if entire message has been consumed

		if (eom != true) {
			status = TraceDqr::DQERR_BM;

			return status;
		}

		bits += width;
		ts_bits = width;

		nm.haveTimestamp = true;
		nm.timestamp = (TraceDqr::TIMESTAMP)tmp;
	}

	status = analytics.updateTraceInfo(nm,bits+msgSlices*2,msgSlices*2,ts_bits,0);

	nm.msgNum = analytics.currentTraceMsgNum();

	return status;
}

TraceDqr::DQErr SliceFileParser::parseAuxAccessWrite(NexusMessage &nm,Analytics &analytics)
{
	TraceDqr::DQErr rc;
	uint64_t   tmp;
	int        width;
	int        bits = 6;
	int        ts_bits = 0;

	nm.tcode = TraceDqr::TCODE_AUXACCESS_WRITE;

	// if multicore, parse src field

	if (srcbits > 0) {
        rc = parseFixedField(srcbits,&tmp);
        if (rc != TraceDqr::DQERR_OK) {
            status = rc;

            return status;
        }

        bits += srcbits;

        nm.coreId = (uint8_t)tmp;
	}
	else {
		nm.coreId = 0;
	}

	// parse the ADDR field

	rc = parseVarField(&tmp,&width);
	if (rc != TraceDqr::DQERR_OK) {
		status = rc;

		return status;
	}

	bits += width;

	nm.auxAccessWrite.addr = (uint32_t)tmp;

	// parse the data field

	rc = parseVarField(&tmp,&width);
	if (rc != TraceDqr::DQERR_OK) {
		status = rc;

		return status;
	}

	bits += width;

	nm.auxAccessWrite.data = (uint32_t)tmp;

	if (eom == true) {
		nm.haveTimestamp = false;
		nm.timestamp = 0;
	}
	else {
		rc = parseVarField(&tmp,&width); // this field is optional - check err
		if (rc != TraceDqr::DQERR_OK) {
			status = rc;

			return status;
		}

		// check if entire message has been consumed

		if (eom != true) {
			status = TraceDqr::DQERR_BM;

			return status;
		}

		bits += width;
		ts_bits = width;

		nm.haveTimestamp = true;
		nm.timestamp = (TraceDqr::TIMESTAMP)tmp;
	}

	status = analytics.updateTraceInfo(nm,bits+msgSlices*2,msgSlices*2,ts_bits,0);

	nm.msgNum = analytics.currentTraceMsgNum();

	return status;
}

TraceDqr::DQErr SliceFileParser::parseDataAcquisition(NexusMessage &nm,Analytics &analytics)
{
	TraceDqr::DQErr rc;
	uint64_t   tmp;
	int        width;
	int        bits = 6;
	int        ts_bits = 0;

	nm.tcode = TraceDqr::TCODE_DATA_ACQUISITION;

	// if multicore, parse src field

	if (srcbits > 0) {
        rc = parseFixedField(srcbits,&tmp);
        if (rc != TraceDqr::DQERR_OK) {
            status = rc;

            return status;
        }

        bits += srcbits;

        nm.coreId = (uint8_t)tmp;
	}
	else {
		nm.coreId = 0;
	}

	// parse the idTag field

	rc = parseVarField(&tmp,&width);
	if (rc != TraceDqr::DQERR_OK) {
		status = rc;

		return status;
	}

	bits += width;

	nm.dataAcquisition.idTag = (uint32_t)tmp;

	// parse the data field

	rc = parseVarField(&tmp,&width);
	if (rc != TraceDqr::DQERR_OK) {
		status = rc;

		return status;
	}

	bits += width;

	nm.dataAcquisition.data = (uint32_t)tmp;

	if (eom == true) {
		nm.haveTimestamp = false;
		nm.timestamp = 0;
	}
	else {
		rc = parseVarField(&tmp,&width); // this field is optional - check err
		if (rc != TraceDqr::DQERR_OK) {
			status = rc;

			return status;
		}

		// check if entire message has been consumed

		if (eom != true) {
			status = TraceDqr::DQERR_BM;

			return status;
		}

		bits += width;
		ts_bits = width;

		nm.haveTimestamp = true;
		nm.timestamp = (TraceDqr::TIMESTAMP)tmp;
	}

	status = analytics.updateTraceInfo(nm,bits+msgSlices*2,msgSlices*2,ts_bits,0);

	nm.msgNum = analytics.currentTraceMsgNum();

	return status;
}

TraceDqr::DQErr SliceFileParser::parseFixedField(int width, uint64_t *val)
{
	assert(width != 0);
	assert(val != nullptr);

	uint64_t tmp_val = 0;

	int i;
	int b;

	i = bitIndex / 6;
	b = bitIndex % 6;

//	printf("parseFixedField(): bitIndex:%d, i:%d, b:%d, width: %d\n",bitIndex,i,b,width);

	bitIndex += width;

	// for read error checking we should make sure that the MSEO bits are
	// correct for this field. But for now, we don't

	if (bitIndex >= msgSlices * 6) {
		// read past end of message

		status = TraceDqr::DQERR_EOM;

		return TraceDqr::DQERR_EOM;
	}

	if (b+width > 6) {
		// fixed field crossed byte boundry - get the bits at msg[i]

		// work from lsb's to msb's

		tmp_val = (uint64_t)(msg[i] >> (b+2));	// add 2 because of the mseo bits

		int consumed = 6-b;
		int remainingWidth = width - consumed;

		i += 1;

//		b = 0; comment this line out because we jsut don't use b anymore. It is 0 for the rest of the call

		// get the middle bits

		while (remainingWidth >= 6) {
			tmp_val |= ((uint64_t)(msg[i] >> 2)) << consumed;
			i += 1;
			remainingWidth -= 6;
			consumed += 6;
		}

		// now get the last bits

		if (remainingWidth > 0) {
			tmp_val |= ((uint64_t)(((uint8_t)(msg[i] << (6-remainingWidth))) >> (6-remainingWidth+2))) << consumed;
		}

		*val = tmp_val;
	}
	else {
		uint8_t v;

		// strip off upper and lower bits not part of field

		v = msg[i] << (6-(b+width));
		v = v >> ((6-(b+width))+b+2);

		*val = uint64_t(v);
	}

	if ((msg[i] & 0x03) == TraceDqr::MSEO_END) {
		eom = true;
	}

	return TraceDqr::DQERR_OK;
}

TraceDqr::DQErr SliceFileParser::parseVarField(uint64_t *val,int *width)
{
	assert(val != nullptr);

	int i;
	int b;
	int w = 0;

	i = bitIndex / 6;
	b = bitIndex % 6;

	if (i >= msgSlices) {
		// read past end of message

		status = TraceDqr::DQERR_EOM;

		return TraceDqr::DQERR_EOM;
	}

	uint64_t v;

	// strip off upper and lower bits not part of field

	w = 6-b;

//	printf("parseVarField(): bitIndex:%d, i:%d, b:%d, width: %d\n",bitIndex,i,b,width);

	v = ((uint64_t)msg[i]) >> (b+2);

	while ((msg[i] & 0x03) == TraceDqr::MSEO_NORMAL) {
		i += 1;
		if (i >= msgSlices) {
			// read past end of message

			status = TraceDqr::DQERR_ERR;

			return TraceDqr::DQERR_ERR;
		}

		v = v | ((((uint64_t)msg[i]) >> 2) << w);
		w += 6;
	}

	if (w > (int)sizeof(v)*8) {
		// variable field overflowed size of v

		status = TraceDqr::DQERR_ERR;

		return TraceDqr::DQERR_ERR;
	}

	if ((msg[i] & 0x03) == TraceDqr::MSEO_END) {
		eom = true;
	}

	bitIndex += w;

	*width = w;
	*val = v;

	return TraceDqr::DQERR_OK;
}

TraceDqr::DQErr SliceFileParser::readBinaryMsg()
{
	// start by stripping off end of message or end of var bytes. These would be here in the case
	// of a wrapped buffer, or some kind of corruption

	do {
		tf.read((char*)&msg[0],sizeof msg[0]);
		if (!tf) {
			if (tf.eof()) {
				status = TraceDqr::DQERR_EOF;
			}
			else {
				status = TraceDqr::DQERR_ERR;

				std::cout << "Error reading trace file\n";
			}

			tf.close();

			return status;
		}


		if (((msg[0] & 0x3) != TraceDqr::MSEO_NORMAL) && (msg[0] != 0xff)) {
			printf("Info: SliceFileParser::readBinaryMsg(): Skipping: %02x\n",msg[0]);
		}
	} while ((msg[0] & 0x3) != TraceDqr::MSEO_NORMAL);

	msgOffset = ((uint32_t)tf.tellg())-1;

	bool done = false;

	for (int i = 1; !done; i++) {
		if (i >= (int)(sizeof msg / sizeof msg[0])) {

			tf.close();

			std::cout << "Error: SliceFileParser::readBinaryMsg(): msg buffer overflow" << std::endl;

			status = TraceDqr::DQERR_ERR;

			return TraceDqr::DQERR_ERR;
		}

		tf.read((char*)&msg[i],sizeof msg[0]);
		if (!tf) {
			if (tf.eof()) {
				status = TraceDqr::DQERR_EOF;
			}
			else {
				status = TraceDqr::DQERR_ERR;

				std::cout << "Error reading trace file\n";
			}

			tf.close();

			return status;
		}

		if ((msg[i] & 0x03) == TraceDqr::MSEO_END) {
			done = true;
			msgSlices = i+1;
		}
	}

	eom = false;
	bitIndex = 0;

	return TraceDqr::DQERR_OK;
}

TraceDqr::DQErr SliceFileParser::readNextByte(uint8_t *byte)
{
	char c;

	// strip white space, comments, cr, and lf

	enum {
		STRIPPING_WHITESPACE,
		STRIPPING_COMMENT,
		STRIPPING_DONE
	} ss = STRIPPING_WHITESPACE;

	do {
		tf.read((char*)&c,sizeof c);
		if (!tf) {
			tf.close();

			status = TraceDqr::DQERR_EOF;

			return TraceDqr::DQERR_EOF;
		}

		switch (ss) {
		case STRIPPING_WHITESPACE:
			switch (c) {
			case '#':
				ss = STRIPPING_COMMENT;
				break;
			case '\n':
			case '\r':
			case ' ':
			case '\t':
				break;
			default:
				ss = STRIPPING_DONE;
			}
			break;
		case STRIPPING_COMMENT:
			switch (c) {
			case '\n':
				ss = STRIPPING_WHITESPACE;
				break;
			}
			break;
		default:
			break;
		}
	} while (ss != STRIPPING_DONE);

	// now try to get two hex digits

	tf.read((char*)&c,sizeof c);
	if (!tf) {
		tf.close();

		status = TraceDqr::DQERR_EOF;

		return TraceDqr::DQERR_EOF;
	}

	int hd;
	uint8_t hn;

	hd = atoh(c);
	if (hd < 0) {
		status = TraceDqr::DQERR_ERR;

		return TraceDqr::DQERR_ERR;
	}

	hn = hd << 4;

	hd = atoh(c);
	if (hd < 0) {
		status = TraceDqr::DQERR_ERR;

		return TraceDqr::DQERR_ERR;
	}

	hn = hn | hd;

	*byte = hn;

	return TraceDqr::DQERR_OK;
}

TraceDqr::DQErr SliceFileParser::readAscMsg()
{
	std::cout << "Error: SliceFileP{arser::readAscMsg(): not implemented" << std::endl;
	status = TraceDqr::DQERR_ERR;

	return TraceDqr::DQERR_ERR;

	// strip off idle bytes

	do {
		if (readNextByte(&msg[0]) != TraceDqr::DQERR_OK) {
			return status;
		}
	} while ((msg[0] & 0x3) == TraceDqr::MSEO_END);

	// make sure this is start of nexus message

	if ((msg[0] & 0x3) != TraceDqr::MSEO_NORMAL) {
		status = TraceDqr::DQERR_ERR;

		std::cout << "Error: SliceFileParser::readAscMsg(): expected start of message; got" << std::hex << static_cast<uint8_t>(msg[0] & 0x3) << std::dec << std::endl;

		return TraceDqr::DQERR_ERR;
	}

	// now get bytes

	bool done = false;

	for (int i = 1; !done; i++) {
		if (i >= (int)(sizeof msg / sizeof msg[0])) {
			tf.close();

			std::cout << "Error: SliceFileParser::readAscMsg(): msg buffer overflow" << std::endl;

			status = TraceDqr::DQERR_ERR;

			return TraceDqr::DQERR_ERR;
		}

		if (readNextByte(&msg[i]) != TraceDqr::DQERR_OK) {
			return status;
		}

		if ((msg[i] & 0x03) == TraceDqr::MSEO_END) {
			done = true;
			msgSlices = i+1;
		}
	}

	eom = false;

	bitIndex = 0;

	return TraceDqr::DQERR_OK;
}

TraceDqr::DQErr SliceFileParser::readNextTraceMsg(NexusMessage &nm,Analytics &analytics)	// generator to read trace messages one at a time
{
	if (status != TraceDqr::DQERR_OK) {
		return status;
	}

	TraceDqr::DQErr rc;
	uint64_t   val;
	uint8_t    tcode;
	int getMsgAttempt = 3;

	do { // we will try to get a good message most 3 times if there are problems

		status = TraceDqr::DQERR_OK;

		// read from file, store in object, compute and fill out full fields, such as address and more later

		if (binary) {
			rc = readBinaryMsg();
			if (rc != TraceDqr::DQERR_OK) {

				// all errors from readBinaryMsg() are non-recoverable.

				if (rc != TraceDqr::DQERR_EOF) {
					std::cout << "Error: (): readNextTraceMsg() returned error " << rc << std::endl;
				}

				status = rc;

				return status;
			}
		}
		else {	// text trace messages
			rc = readAscMsg();
			if (rc != TraceDqr::DQERR_OK) {
				if (rc != TraceDqr::DQERR_EOF) {
					std::cout << "Error: (): readNextTraceMsg() returned error " << rc << std::endl;
				}

				status = rc;

				return status;
			}
		}

		nm.offset = msgOffset;

		int i = 0;

		do {
			nm.rawData[i] = msg[i];
			i += 1;
		} while (((size_t)i < sizeof nm.rawData / sizeof nm.rawData[0]) && ((msg[i-1] & 0x03) != TraceDqr::MSEO_END));

		rc = parseFixedField(6, &val);
		if (rc == TraceDqr::DQERR_OK) {
			tcode = (uint8_t)val;

			switch (tcode) {
			case TraceDqr::TCODE_DEBUG_STATUS:
				std::cout << "Unsupported debug status trace message\n";
				rc = TraceDqr::DQERR_ERR;
				break;
			case TraceDqr::TCODE_DEVICE_ID:
				std::cout << "Unsupported device id trace message\n";
				rc = TraceDqr::DQERR_ERR;
				break;
			case TraceDqr::TCODE_OWNERSHIP_TRACE:
				rc = parseOwnershipTrace(nm,analytics);
				break;
			case TraceDqr::TCODE_DIRECT_BRANCH:
				rc = parseDirectBranch(nm,analytics);
				break;
			case TraceDqr::TCODE_INDIRECT_BRANCH:
				rc = parseIndirectBranch(nm,analytics);
				break;
			case TraceDqr::TCODE_DATA_WRITE:
				std::cout << "unsupported data write trace message\n";
				rc = TraceDqr::DQERR_ERR;
				break;
			case TraceDqr::TCODE_DATA_READ:
				std::cout << "unsupported data read trace message\n";
				rc = TraceDqr::DQERR_ERR;
				break;
			case TraceDqr::TCODE_DATA_ACQUISITION:
				rc = parseDataAcquisition(nm,analytics);
				break;
			case TraceDqr::TCODE_ERROR:
				rc = parseError(nm,analytics);
				break;
			case TraceDqr::TCODE_SYNC:
				rc = parseSync(nm,analytics);
				break;
			case TraceDqr::TCODE_CORRECTION:
				std::cout << "Unsupported correction trace message\n";
				rc = TraceDqr::DQERR_ERR;
				break;
			case TraceDqr::TCODE_DIRECT_BRANCH_WS:
				rc = parseDirectBranchWS(nm,analytics);
				break;
			case TraceDqr::TCODE_INDIRECT_BRANCH_WS:
				rc = parseIndirectBranchWS(nm,analytics);
				break;
			case TraceDqr::TCODE_DATA_WRITE_WS:
				std::cout << "unsupported data write with sync trace message\n";
				rc = TraceDqr::DQERR_ERR;
				break;
			case TraceDqr::TCODE_DATA_READ_WS:
				std::cout << "unsupported data read with sync trace message\n";
				rc = TraceDqr::DQERR_ERR;
				break;
			case TraceDqr::TCODE_WATCHPOINT:
				std::cout << "unsupported watchpoint trace message\n";
				rc = TraceDqr::DQERR_ERR;
				break;
			case TraceDqr::TCODE_CORRELATION:
				rc = parseCorrelation(nm,analytics);
				break;
			case TraceDqr::TCODE_AUXACCESS_WRITE:
				rc = parseAuxAccessWrite(nm,analytics);
				break;
			case TraceDqr::TCODE_INDIRECTBRANCHHISTORY:
				rc = parseIndirectHistory(nm,analytics);
				break;
			case TraceDqr::TCODE_INDIRECTBRANCHHISTORY_WS:
				rc = parseIndirectHistoryWS(nm,analytics);
				break;
			case TraceDqr::TCODE_INCIRCUITTRACE:
				rc = parseICT(nm,analytics);
				break;
			case TraceDqr::TCODE_INCIRCUITTRACE_WS:
				rc = parseICTWS(nm,analytics);
				break;
			case TraceDqr::TCODE_RESOURCEFULL:
				rc = parseResourceFull(nm,analytics);
				break;
			default:
				std::cout << "Error: readNextTraceMsg(): Unknown TCODE " << std::hex << int(tcode) << std::dec << std::endl;
				rc = TraceDqr::DQERR_ERR;
			}

			if (rc != TraceDqr::DQERR_OK) {
				std::cout << "Error possibly due to corrupted first message in trace - skipping message" << std::endl;
			}
		}

		getMsgAttempt -= 1;
	} while ((rc != TraceDqr::DQERR_OK) && (getMsgAttempt > 0));

	status = rc;

	return status;
}

Disassembler::Disassembler(bfd *abfd)
{
	assert(abfd != nullptr);

	pType = TraceDqr::PATH_TO_UNIX;

	this->abfd = abfd;

    prev_index       = -1;
    cached_sym_index = -1;
    cached_sym_vma   = 0;
    cached_sym_size  = 0;

    start_address = bfd_get_start_address(abfd);

    long storage_needed;

    storage_needed = bfd_get_symtab_upper_bound(abfd);

//    printf("storage needed = %d\n",storage_needed);

    if (storage_needed > 0) {
    	symbol_table = (asymbol **)new (std::nothrow) char[storage_needed];

    	assert(symbol_table != nullptr);

    	number_of_syms = bfd_canonicalize_symtab(abfd,symbol_table);

    	if (number_of_syms > 0) {
    		sorted_syms = new (std::nothrow) asymbol*[number_of_syms];

    		assert(sorted_syms != nullptr);

//        	make sure all symbols with no type info in code sections have function type added!

    		for (int i = 0; i < number_of_syms; i++) {
    			struct bfd_section *section;

    			section = symbol_table[i]->section;

//    			printf("sym %d, section flags: %08x, name: %s, flags: %08x, value: %08x, base:%08x (%08x)\n",i,section->flags,symbol_table[i]->name,symbol_table[i]->flags,symbol_table[i]->value,section->vma,section->vma+symbol_table[i]->value);

    			if ((section->flags & SEC_CODE) && ((symbol_table[i]->flags == BSF_NO_FLAGS) || (symbol_table[i]->flags == BSF_GLOBAL))) {
    				symbol_table[i]->flags |= BSF_FUNCTION;
    			}

    			sorted_syms[i] = symbol_table[i];
    		}

    		// could probably use C++ template sort here, but this works for now

//    		for (int i = 0; i < number_of_syms; i++) {
//    			printf("symbol[%d]:%s, addr:%08x, flags:%08x\n",i,sorted_syms[i]->name,bfd_asymbol_value(sorted_syms[i]),sorted_syms[i]->flags);
//    		}

    		// note: qsort does not preserver order on equal items!

    		qsort((void*)sorted_syms,(size_t)number_of_syms,sizeof sorted_syms[0],asymbol_compare_func);

    		func_info = new (std::nothrow) func_info_t[number_of_syms];

    		assert(func_info != nullptr);

    		// compute sizes of functions and data items for lookup by address

    		for (int i = 0; i < number_of_syms; i++) {
				func_info[i].sym_flags = sorted_syms[i]->flags;

				if ((sorted_syms[i]->flags & (BSF_FUNCTION | BSF_OBJECT)) != 0) {

    				// set func_vma to the base+offset address, not just the offest

    				func_info[i].func_vma = bfd_asymbol_value(sorted_syms[i]);

    				int j;

    				for (j = i+1; j < number_of_syms; j++) {
    					if ((sorted_syms[j]->flags & (BSF_FUNCTION | BSF_OBJECT)) != 0) {
    						func_info[i].func_size = bfd_asymbol_value(sorted_syms[j]) - func_info[i].func_vma;
    						break;
    					}
    				}

    				if (j >= number_of_syms) {
    					// size size of func(i) to end of section

    					asection *section;
    					bfd_vma base_vma;
    					section = sorted_syms[i]->section;
    					base_vma = section->vma;

    					func_info[i].func_size = (base_vma + section->size) - func_info[i].func_vma;
    				}
    			}
    			else {
    				func_info[i].func_size = 0;	// this probably isn't right
    			}

//				printf("symbol[%d]:%s, addr:%08x size:%d, flags:%08x\n",i,sorted_syms[i]->name,func_info[i].func_vma,func_info[i].func_size,sorted_syms[i]->flags);
    		}
    	}
    }
    else {
    	symbol_table = nullptr;
    	sorted_syms = nullptr;
    }

    codeSectionLst = nullptr;

    for (asection *p = abfd->sections; p != NULL; p = p->next) {
  	  if (p->flags & SEC_CODE) {
            // found a code section, add to list

  		  section *sp = new section;
  		  if (sp->initSection(&codeSectionLst,p,true) == nullptr) {
  			  status = TraceDqr::DQERR_ERR;
  			  return;
  		  }
  	  }
    }

    if (codeSectionLst == nullptr) {
    	printf("Error: no code sections found\n");

    	status = TraceDqr::DQERR_ERR;
    	return;
    }

   	info = new (std::nothrow) disassemble_info;

   	assert(info != nullptr);

   	disassemble_func = disassembler(bfd_get_arch(abfd), bfd_big_endian(abfd),bfd_get_mach(abfd), abfd);
   	if (disassemble_func == nullptr) {
   		printf("Error: Disassmbler::Disassembler(): could not create disassembler\n");

   		delete [] info;
   		info = nullptr;

   		status = TraceDqr::DQERR_ERR;
   		return;
   	}

   	init_disassemble_info(info,stdout,(fprintf_ftype)stringify_callback);

   	info->print_address_func = override_print_address;
   	info->arch = bfd_get_arch(abfd);
   	info->mach = bfd_get_mach(abfd);

   	info->buffer_vma = codeSectionLst->startAddr;
   	info->buffer_length = codeSectionLst->size;
   	info->section = codeSectionLst->asecptr;
   	info->buffer = (bfd_byte*)codeSectionLst->code;

   	info->application_data = (void*)this;

   	disassemble_init_for_target(info);

   	fileReader = new class fileReader();

    const bfd_arch_info_type *aitp;

    aitp = bfd_get_arch_info(abfd);
    if (aitp == nullptr) {
  	  printf("Error: Disassembler::Disassembler(): Cannot get arch type for elf file\n");

  	  status = TraceDqr::TraceDqr::DQERR_ERR;
  	  return;
    }

    switch (aitp->mach) {
    case bfd_mach_riscv32:
  	  archSize = 32;
  	  break;
    case bfd_mach_riscv64:
  	  archSize = 64;
  	  break;
    default:
  	  printf("Error: Disassembler::Disassembler(): Unknown machine type\n");

  	  status = TraceDqr::TraceDqr::DQERR_ERR;
  	  return;
    }

    status = TraceDqr::DQERR_OK;
}

Disassembler::~Disassembler()
{
	if (symbol_table != nullptr) {
		delete [] symbol_table;
		symbol_table = nullptr;
	}

	if (sorted_syms != nullptr) {
		delete [] sorted_syms;
		sorted_syms = nullptr;
	}

	if (func_info != nullptr) {
		delete [] func_info;
		func_info = nullptr;
	}

	while (codeSectionLst != nullptr) {
		section *nextSection = codeSectionLst->next;
		delete codeSectionLst;
		codeSectionLst = nextSection;
	}

	if (info != nullptr) {
		delete info;
		info = nullptr;
	}

	if (fileReader != nullptr) {
		delete fileReader;
		fileReader = nullptr;
	}
}

TraceDqr::DQErr Disassembler::setPathType(TraceDqr::pathType pt)
{
	TraceDqr::DQErr rc;
	rc = TraceDqr::DQERR_OK;

	switch (pt) {
	case TraceDqr::PATH_TO_WINDOWS:
		pType = TraceDqr::PATH_TO_WINDOWS;
		break;
	case TraceDqr::PATH_TO_UNIX:
		pType = TraceDqr::PATH_TO_UNIX;
		break;
	case TraceDqr::PATH_RAW:
		pType = TraceDqr::PATH_RAW;
		break;
	default:
		pType = TraceDqr::PATH_TO_UNIX;
		rc = TraceDqr::DQERR_ERR;
		break;
	}

	return rc;
}

int Disassembler::lookupInstructionByAddress(bfd_vma vma,uint32_t *ins,int *ins_size)
{
	assert(ins != nullptr);

	uint32_t inst;
	int size;
	int rc;

	// need to support multiple code sections and do some error checking on the address!

	if (codeSectionLst == nullptr) {
		status = TraceDqr::DQERR_ERR;

		return 1;
	}

	section *sp;

	sp = codeSectionLst->getSectionByAddress((TraceDqr::ADDRESS)vma);

	if (sp == nullptr) {
		status = TraceDqr::DQERR_ERR;

		return 1;
	}

	inst = (uint32_t)sp->code[(vma - sp->startAddr)/2];

	rc = decodeInstructionSize(inst, size);
	if (rc != 0) {
		status = TraceDqr::DQERR_ERR;

		return rc;
	}

	*ins_size = size;

	if (size == 16) {
		*ins = inst;
	}
	else {
		*ins = (((uint32_t)sp->code[(vma - sp->startAddr)/2+1]) << 16) | inst;
	}

	return 0;
}

int Disassembler::lookup_symbol_by_address(bfd_vma vma,flagword flags,int *index,int *offset)
{
	assert(index != nullptr);
	assert(offset != nullptr);

	// find the function closest to the address. Address should either be start of function, or in body
	// of function.

	if ( vma == 0) {
		return 0;
	}

	int i;

	// check for a cache hit

	if (cached_sym_index != -1) {
		if ((vma >= cached_sym_vma) && (vma < (cached_sym_vma + cached_sym_size))) {
			*index = cached_sym_index;
			*offset = vma - cached_sym_vma;

			return 1;
		}
	}

	//syms = sorted_syms;

	for (i = 0; i < number_of_syms; i++) {
		if ((func_info[i].sym_flags & (BSF_FUNCTION | BSF_OBJECT)) != 0) {

			// note: func_vma already has the base+offset address in it

			if (vma == func_info[i].func_vma) {
				// exact match on vma. Make sure function isn't zero sized
				// if it is, try to find a following one at the same address

//				printf("\n->vma:%08x size: %d, i=%d, name: %s\n",vma,func_info[i].func_size,i,sorted_syms[i]->name);

				for (int j = i+1; j < number_of_syms; j++) {
					if ((func_info[j].sym_flags & (BSF_FUNCTION | BSF_OBJECT)) != 0) {
						if (func_info[i].func_vma == func_info[j].func_vma) {
							i = j;
						}
					}
				}

				// have a match with a function

				//printf("have match. Index %d\n",i);

				// cache it for re-lookup speed improvement

				cached_sym_index = i;
				cached_sym_vma = func_info[i].func_vma;
				cached_sym_size = func_info[i].func_size;

				*index = i;
				*offset = vma - cached_sym_vma;

				return 1;
			}
			else if (vma > func_info[i].func_vma) {
				if (vma < (func_info[i].func_vma + func_info[i].func_size)) {

					cached_sym_index = i;
					cached_sym_vma = func_info[i].func_vma;
					cached_sym_size = func_info[i].func_size;

					*index = i;
					*offset = vma - cached_sym_vma;

					return 1;
				}
			}
		}
	}

	return 0;
}

void Disassembler::overridePrintAddress(bfd_vma addr, struct disassemble_info *info)
{
	assert(info != nullptr);

	//	lookup symbol at addr.

	// use field in info to point to disassembler object so we can call member funcs

	if (info != this->info) {
//		printf("Error: overridePrintAddress(): info does not match (0x%08x, 0x%08x)\n",this->info,info);
//		this may be okay. Different info.
	}

	instruction.operandAddress = addr;
	instruction.haveOperandAddress = true;

	int index;
	int offset;
	int rc;

	rc = lookup_symbol_by_address(addr,BSF_FUNCTION,&index,&offset);

	if (rc != 0) {
		// found symbol

//		printf("found symbol @0x%08x '%s' %x\n",addr,sorted_syms[index]->name,offset);

		// putting in wrong instrction object!!!!!

		instruction.operandLabel = sorted_syms[index]->name;
		instruction.operandLabelOffset = offset;

//		if (offset == 0) {
//			printf("%08lx <%s>",addr,sorted_syms[index]->name);
//		}
//		else {
//			printf("%08lx <%s+%x>",addr,sorted_syms[index]->name,offset);
//		}
	}
	else {
//		printf("%08lx",addr);

		instruction.operandLabel = nullptr;
		instruction.operandLabelOffset = 0;
	}
}


void Disassembler::print_address(bfd_vma vma)
{
	// find closest preceeding function symbol and print with offset

	int index;
	int offset;
	int rc;

	rc = lookup_symbol_by_address(vma,BSF_FUNCTION,&index,&offset);
	if (rc != 0) {
		// found symbol

	    if (prev_index != index) {
	    	prev_index = index;

//	    	printf("\n");

	    	// need to know if this is 64 or 32 bit

			switch (archSize) {
			case 32:
				if (offset == 0) {
					printf("%08x <%s>:\n",(uint32_t)vma,sorted_syms[index]->name);
				}
				else {
					printf("%08x <%s+%x>:\n",(uint32_t)vma,sorted_syms[index]->name,offset);
				}
				printf("%8x: ",(uint32_t)vma);
				break;
			case 64:
				if (offset == 0) {
					printf("%08llx <%s>:\n",vma,sorted_syms[index]->name);
				}
				else {
					printf("%08llx <%s+%x>:\n",vma,sorted_syms[index]->name,offset);
				}
				printf("%8llx: ",vma);
				break;
			default:
				printf("Error: Disassembler::print_address(): Bad arch size: %d\n",archSize);
				break;
			}
	    }
	}
	else {
		// didn't find symbol

		prev_index = -1;

		switch (archSize) {
		case 32:
			printf("%8lx:",(uint32_t)vma);
			break;
		case 64:
			printf("%8llx:",vma);
			break;
		default:
			printf("Error: Disassembler::print_address(): Bad arch size: %d\n",archSize);
			break;
		}
	}
}

void Disassembler::print_address_and_instruction(bfd_vma vma)
{
	uint32_t ins = 0;
	int ins_size = 0;

	print_address(vma);

	lookupInstructionByAddress(vma,&ins,&ins_size);

	if (ins_size > 16) {
		 printf("       %08x                ",ins);
	}
	else {
		 printf("       %04x                    ",ins);
	}
}

void Disassembler::getAddressSyms(bfd_vma vma)
{
	// find closest preceeding function symbol and print with offset

	int index;
	int offset;
	int rc;

	rc = lookup_symbol_by_address(vma,BSF_FUNCTION,&index,&offset);
	if (rc == 0) {
		// did not find symbol

		instruction.addressLabel = nullptr;
		instruction.addressLabelOffset = 0;
	}
	else {

		// found symbol

		instruction.addressLabel = sorted_syms[index]->name;
		instruction.addressLabelOffset = offset;
	}
}

void Disassembler::clearOperandAddress()
{
	instruction.haveOperandAddress = false;
}

void Disassembler::setInstructionAddress(bfd_vma vma)
{
	instruction.address = vma;

	// find closest preceeding function symbol and print with offset

	int index;
	int offset;
	int rc;

	lookupInstructionByAddress(vma,&instruction.instruction,&instruction.instSize);

	rc = lookup_symbol_by_address(vma,BSF_FUNCTION,&index,&offset);
	if (rc == 0) {
		// did not find symbol

		instruction.addressLabel = nullptr;
		instruction.addressLabelOffset = 0;
	}
	else {

		// found symbol

		instruction.addressLabel = sorted_syms[index]->name;
		instruction.addressLabelOffset = offset;
	}
}

int Disassembler::decodeInstructionSize(uint32_t inst, int &inst_size)
{
	switch (inst & 0x0003) {
	case 0x0000:	// quadrant 0, compressed
		inst_size = 16;
		return 0;
	case 0x0001:	// quadrant 1, compressed
		inst_size = 16;
		return 0;
	case 0x0002:	// quadrant 2, compressed
		inst_size = 16;
		return 0;
	case 0x0003:	// not compressed. Assume RV32 for now
		if ((inst & 0x1f) == 0x1f) {
			fprintf(stderr,"Error: decode_instruction(): cann't decode instructions longer than 32 bits\n");
			return 1;
		}

		inst_size = 32;
		return 0;
	}

	// error return

	return 1;
}

#define MOVE_BIT(bits,s,d)	(((bits)&(1<<(s)))?(1<<(d)):0)

int Disassembler::decodeRV32Q0Instruction(uint32_t instruction,int &inst_size,TraceDqr::InstType &inst_type,TraceDqr::Reg &rs1,TraceDqr::Reg &rd,int32_t &immediate,bool &is_branch)
{
	// no branch or jump instruction in quadrant 0

	inst_size = 16;
	is_branch = false;
	immediate = 0;

	if ((instruction & 0x0003) != 0x0000) {
		return 1;
	}

	// we only crare about rs1 and rd for branch instructions, so just set them to unknown for now
	rs1 = TraceDqr::REG_unknown;
	rd = TraceDqr::REG_unknown;

	inst_type = TraceDqr::INST_UNKNOWN;

	return 0;
}

int Disassembler::decodeRV32Q1Instruction(uint32_t instruction,int &inst_size,TraceDqr::InstType &inst_type,TraceDqr::Reg &rs1,TraceDqr::Reg &rd,int32_t &immediate,bool &is_branch)
{
	// Q1 compressed instruction

	uint32_t t;

	inst_size = 16;

	switch (instruction >> 13) {
	case 0x1:
		inst_type = TraceDqr::INST_C_JAL;
		is_branch = true;

		t = MOVE_BIT(instruction,3,1);
		t |= MOVE_BIT(instruction,4,2);
		t |= MOVE_BIT(instruction,5,3);
		t |= MOVE_BIT(instruction,11,4);
		t |= MOVE_BIT(instruction,2,5);
		t |= MOVE_BIT(instruction,7,6);
		t |= MOVE_BIT(instruction,6,7);
		t |= MOVE_BIT(instruction,9,8);
		t |= MOVE_BIT(instruction,10,9);
		t |= MOVE_BIT(instruction,8,10);
		t |= MOVE_BIT(instruction,12,11);

		if (t & (1<<11)) { // sign extend offset
			t |= 0xfffff000;
		}

		immediate = t;

		rs1 = TraceDqr::REG_unknown;
		rd = TraceDqr::REG_1;
		break;
	case 0x5:
		inst_type = TraceDqr::INST_C_J;
		is_branch = true;

		t = MOVE_BIT(instruction,3,1);
		t |= MOVE_BIT(instruction,4,2);
		t |= MOVE_BIT(instruction,5,3);
		t |= MOVE_BIT(instruction,11,4);
		t |= MOVE_BIT(instruction,2,5);
		t |= MOVE_BIT(instruction,7,6);
		t |= MOVE_BIT(instruction,6,7);
		t |= MOVE_BIT(instruction,9,8);
		t |= MOVE_BIT(instruction,10,9);
		t |= MOVE_BIT(instruction,8,10);
		t |= MOVE_BIT(instruction,12,11);

		if (t & (1<<11)) { // sign extend offset
			t |= 0xfffff000;
		}

		rs1 = TraceDqr::REG_unknown;
		rd = TraceDqr::REG_0;

		immediate = t;
		break;
	case 0x6:
		inst_type = TraceDqr::INST_C_BEQZ;
		is_branch = true;

		t = MOVE_BIT(instruction,3,1);
		t |= MOVE_BIT(instruction,4,2);
		t |= MOVE_BIT(instruction,10,3);
		t |= MOVE_BIT(instruction,11,4);
		t |= MOVE_BIT(instruction,2,5);
		t |= MOVE_BIT(instruction,5,6);
		t |= MOVE_BIT(instruction,6,7);
		t |= MOVE_BIT(instruction,12,8);

		if (t & (1<<8)) { // sign extend offset
			t |= 0xfffffe00;
		}

		rs1 = (TraceDqr::Reg)((instruction >> 7) & 0x03);
		rd = TraceDqr::REG_unknown;

		immediate = t;
		break;
	case 0x7:
		inst_type = TraceDqr::INST_C_BNEZ;
		is_branch = true;

		t = MOVE_BIT(instruction,3,1);
		t |= MOVE_BIT(instruction,4,2);
		t |= MOVE_BIT(instruction,10,3);
		t |= MOVE_BIT(instruction,11,4);
		t |= MOVE_BIT(instruction,2,5);
		t |= MOVE_BIT(instruction,5,6);
		t |= MOVE_BIT(instruction,6,7);
		t |= MOVE_BIT(instruction,12,8);

		if (t & (1<<8)) { // sign extend offset
			t |= 0xfffffe00;
		}

		rs1 = (TraceDqr::Reg)((instruction >> 7) & 0x03);
		rd = TraceDqr::REG_unknown;

		immediate = t;
		break;
	default:
		rs1 = TraceDqr::REG_unknown;
		rd = TraceDqr::REG_unknown;
		inst_type = TraceDqr::INST_UNKNOWN;
		immediate = 0;
		is_branch = 0;
		break;
	}

	return 0;
}

int Disassembler::decodeRV32Q2Instruction(uint32_t instruction,int &inst_size,TraceDqr::InstType &inst_type,TraceDqr::Reg &rs1,TraceDqr::Reg &rd,int32_t &immediate,bool &is_branch)
{
	// Q1 compressed instruction

	inst_size = 16;

	inst_type = TraceDqr::INST_UNKNOWN;
	is_branch = false;
	rs1 = TraceDqr::REG_unknown;
	rd = TraceDqr::REG_unknown;

	switch (instruction >> 13) {
	case 0x4:
		if (instruction & (1<<12)) {
			if ((instruction & 0x007c) == 0x0000) {
				if ((instruction & 0x0f80) != 0x0000) {
					inst_type = TraceDqr::INST_C_JALR;
					is_branch = true;

					rs1 = (TraceDqr::Reg)((instruction >> 7) & 0x1f);
					rd = TraceDqr::REG_1;
					immediate = 0;
				}
				else {
					inst_type = TraceDqr::INST_C_EBREAK;
					immediate = 0;
					is_branch = true;
				}
			}
		}
		else {
			if ((instruction & 0x007c) == 0x0000) {
				if ((instruction & 0x0f80) != 0x0000) {
					inst_type = TraceDqr::INST_C_JR;
					is_branch = true;

					rs1 = (TraceDqr::Reg)((instruction >> 7) & 0x1f);
					rd = TraceDqr::REG_0;
					immediate = 0;
				}
			}
		}
		break;
	default:
		break;
	}

	return 0;
}

int Disassembler::decodeRV32Instruction(uint32_t instruction,int &inst_size,TraceDqr::InstType &inst_type,TraceDqr::Reg &rs1,TraceDqr::Reg &rd,int32_t &immediate,bool &is_branch)
{
	if ((instruction & 0x1f) == 0x1f) {
		fprintf(stderr,"Error: decodeBranch(): cann't decode instructions longer than 32 bits\n");
		return 1;
	}

	uint32_t t;

	inst_size = 32;

	switch (instruction & 0x7f) {
	case 0x6f:
		inst_type = TraceDqr::INST_JAL;
		is_branch = true;

		t = MOVE_BIT(instruction,21,1);
		t |= MOVE_BIT(instruction,22,2);
		t |= MOVE_BIT(instruction,23,3);
		t |= MOVE_BIT(instruction,24,4);
		t |= MOVE_BIT(instruction,25,5);
		t |= MOVE_BIT(instruction,26,6);
		t |= MOVE_BIT(instruction,27,7);
		t |= MOVE_BIT(instruction,28,8);
		t |= MOVE_BIT(instruction,29,9);
		t |= MOVE_BIT(instruction,30,10);
		t |= MOVE_BIT(instruction,20,11);
		t |= MOVE_BIT(instruction,12,12);
		t |= MOVE_BIT(instruction,13,13);
		t |= MOVE_BIT(instruction,14,14);
		t |= MOVE_BIT(instruction,15,15);
		t |= MOVE_BIT(instruction,16,16);
		t |= MOVE_BIT(instruction,17,17);
		t |= MOVE_BIT(instruction,18,18);
		t |= MOVE_BIT(instruction,19,19);
		t |= MOVE_BIT(instruction,31,20);

		if (t & (1<<20)) { // sign extend offset
			t |= 0xffe00000;
		}

		immediate = t;
		rd = (TraceDqr::Reg)((instruction >> 7) & 0x1f);
		rs1 = TraceDqr::REG_unknown;
		break;
	case 0x67:
		if ((instruction & 0x7000) == 0x000) {
			inst_type = TraceDqr::INST_JALR;
			is_branch = true;

			t = instruction >> 20;

			if (t & (1<<11)) { // sign extend offset
				t |= 0xfffff000;
			}

			immediate = t;
			rd = (TraceDqr::Reg)((instruction >> 7) & 0x1f);
			rs1 = (TraceDqr::Reg)((instruction >> 15) & 0x1f);
		}
		else {
			inst_type = TraceDqr::INST_UNKNOWN;
			immediate = 0;
			rd = TraceDqr::REG_unknown;
			rs1 = TraceDqr::REG_unknown;
			is_branch = false;
		}
		break;
	case 0x63:
		switch ((instruction >> 12) & 0x7) {
		case 0x0:
			inst_type = TraceDqr::INST_BEQ;
			break;
		case 0x1:
			inst_type = TraceDqr::INST_BNE;
			break;
		case 0x4:
			inst_type = TraceDqr::INST_BLT;
			break;
		case 0x5:
			inst_type = TraceDqr::INST_BGE;
			break;
		case 0x6:
			inst_type = TraceDqr::INST_BLTU;
			break;
		case 0x7:
			inst_type = TraceDqr::INST_BGEU;
			break;
		default:
			inst_type = TraceDqr::INST_UNKNOWN;
			immediate = 0;
			rd = TraceDqr::REG_unknown;
			rs1 = TraceDqr::REG_unknown;
			is_branch = false;
			return 0;
		}

		is_branch = true;

		t = MOVE_BIT(instruction,8,1);
		t |= MOVE_BIT(instruction,9,2);
		t |= MOVE_BIT(instruction,10,3);
		t |= MOVE_BIT(instruction,11,4);
		t |= MOVE_BIT(instruction,25,5);
		t |= MOVE_BIT(instruction,26,6);
		t |= MOVE_BIT(instruction,27,7);
		t |= MOVE_BIT(instruction,28,8);
		t |= MOVE_BIT(instruction,29,9);
		t |= MOVE_BIT(instruction,30,10);
		t |= MOVE_BIT(instruction,7,11);
		t |= MOVE_BIT(instruction,31,12);

		if (t & (1<<12)) { // sign extend offset
			t |= 0xffffe000;
		}

		immediate = t;
		rd = TraceDqr::REG_unknown;
		rs1 = (TraceDqr::Reg)((instruction >> 15) & 0x1f);
		break;
	case 0x73:
		if (instruction == 0x00200073) {
			inst_type = TraceDqr::INST_URET;
			is_branch = true;
			immediate = 0;
		}
		else if (instruction == 0x10200073) {
			inst_type = TraceDqr::INST_SRET;
			is_branch = true;
			immediate = 0;
		}
		else if (instruction == 0x30200073) {
			inst_type = TraceDqr::INST_MRET;
			is_branch = true;
			immediate = 0;
		}
		else if (instruction == 0x00000073) {
			inst_type = TraceDqr::INST_ECALL;
			immediate = 0;
			is_branch = true;
		}
		else if (instruction == 0x00100073) {
			inst_type = TraceDqr::INST_EBREAK;
			immediate = 0;
			is_branch = true;
		}
		else {
			inst_type = TraceDqr::INST_UNKNOWN;
			immediate = 0;
			rd = TraceDqr::REG_unknown;
			rs1 = TraceDqr::REG_unknown;
			is_branch = false;
		}
		break;
	default:
		inst_type = TraceDqr::INST_UNKNOWN;
		immediate = 0;
		rd = TraceDqr::REG_unknown;
		rs1 = TraceDqr::REG_unknown;
		is_branch = false;
		break;
	}

	return 0;
}

int Disassembler::decodeRV64Q0Instruction(uint32_t instruction,int &inst_size,TraceDqr::InstType &inst_type,TraceDqr::Reg &rs1,TraceDqr::Reg &rd,int32_t &immediate,bool &is_branch)
{
	// no branch or jump instruction in quadrant 0

	inst_size = 16;
	is_branch = false;
	immediate = 0;

	if ((instruction & 0x0003) != 0x0000) {
		return 1;
	}

	// we only crare about rs1 and rd for branch instructions, so just set them to unknown for now
	rs1 = TraceDqr::REG_unknown;
	rd = TraceDqr::REG_unknown;

	inst_type = TraceDqr::INST_UNKNOWN;

	return 0;
}

int Disassembler::decodeRV64Q1Instruction(uint32_t instruction,int &inst_size,TraceDqr::InstType &inst_type,TraceDqr::Reg &rs1,TraceDqr::Reg &rd,int32_t &immediate,bool &is_branch)
{
	// Q1 compressed instruction

	uint32_t t;

	inst_size = 16;

	switch (instruction >> 13) {
	case 0x5:
		inst_type = TraceDqr::INST_C_J;
		is_branch = true;

		t = MOVE_BIT(instruction,3,1);
		t |= MOVE_BIT(instruction,4,2);
		t |= MOVE_BIT(instruction,5,3);
		t |= MOVE_BIT(instruction,11,4);
		t |= MOVE_BIT(instruction,2,5);
		t |= MOVE_BIT(instruction,7,6);
		t |= MOVE_BIT(instruction,6,7);
		t |= MOVE_BIT(instruction,9,8);
		t |= MOVE_BIT(instruction,10,9);
		t |= MOVE_BIT(instruction,8,10);
		t |= MOVE_BIT(instruction,12,11);

		if (t & (1<<11)) { // sign extend offset
			t |= 0xfffff000;
		}

		rs1 = TraceDqr::REG_unknown;
		rd = TraceDqr::REG_0;

		immediate = t;
		break;
	case 0x6:
		inst_type = TraceDqr::INST_C_BEQZ;
		is_branch = true;

		t = MOVE_BIT(instruction,3,1);
		t |= MOVE_BIT(instruction,4,2);
		t |= MOVE_BIT(instruction,10,3);
		t |= MOVE_BIT(instruction,11,4);
		t |= MOVE_BIT(instruction,2,5);
		t |= MOVE_BIT(instruction,5,6);
		t |= MOVE_BIT(instruction,6,7);
		t |= MOVE_BIT(instruction,12,8);

		if (t & (1<<8)) { // sign extend offset
			t |= 0xfffffe00;
		}

		rs1 = (TraceDqr::Reg)((instruction >> 7) & 0x03);
		rd = TraceDqr::REG_unknown;

		immediate = t;
		break;
	case 0x7:
		inst_type = TraceDqr::INST_C_BNEZ;
		is_branch = true;

		t = MOVE_BIT(instruction,3,1);
		t |= MOVE_BIT(instruction,4,2);
		t |= MOVE_BIT(instruction,10,3);
		t |= MOVE_BIT(instruction,11,4);
		t |= MOVE_BIT(instruction,2,5);
		t |= MOVE_BIT(instruction,5,6);
		t |= MOVE_BIT(instruction,6,7);
		t |= MOVE_BIT(instruction,12,8);

		if (t & (1<<8)) { // sign extend offset
			t |= 0xfffffe00;
		}

		rs1 = (TraceDqr::Reg)((instruction >> 7) & 0x03);
		rd = TraceDqr::REG_unknown;

		immediate = t;
		break;
	default:
		rs1 = TraceDqr::REG_unknown;
		rd = TraceDqr::REG_unknown;
		inst_type = TraceDqr::INST_UNKNOWN;
		immediate = 0;
		is_branch = 0;
		break;
	}

	return 0;
}

int Disassembler::decodeRV64Q2Instruction(uint32_t instruction,int &inst_size,TraceDqr::InstType &inst_type,TraceDqr::Reg &rs1,TraceDqr::Reg &rd,int32_t &immediate,bool &is_branch)
{
	// Q2 compressed instruction

	inst_size = 16;

	inst_type = TraceDqr::INST_UNKNOWN;
	is_branch = false;
	rs1 = TraceDqr::REG_unknown;
	rd = TraceDqr::REG_unknown;

	switch (instruction >> 13) {
	case 0x4:
		if (instruction & (1<<12)) {
			if ((instruction & 0x007c) == 0x0000) {
				if ((instruction & 0x0f80) != 0x0000) {
					inst_type = TraceDqr::INST_C_JALR;
					is_branch = true;

					rs1 = (TraceDqr::Reg)((instruction >> 7) & 0x1f);
					rd = TraceDqr::REG_1;
					immediate = 0;
				}
				else {
					inst_type = TraceDqr::INST_EBREAK;
					immediate = 0;
					is_branch = true;
				}
			}
		}
		else {
			if ((instruction & 0x007c) == 0x0000) {
				if ((instruction & 0x0f80) != 0x0000) {
					inst_type = TraceDqr::INST_C_JR;
					is_branch = true;

					rs1 = (TraceDqr::Reg)((instruction >> 7) & 0x1f);
					rd = TraceDqr::REG_0;
					immediate = 0;
				}
			}
		}
		break;
	default:
		break;
	}

	return 0;
}

int Disassembler::decodeRV64Instruction(uint32_t instruction,int &inst_size,TraceDqr::InstType &inst_type,TraceDqr::Reg &rs1,TraceDqr::Reg &rd,int32_t &immediate,bool &is_branch)
{
	if ((instruction & 0x1f) == 0x1f) {
		fprintf(stderr,"Error: decodeRV64Instruction(): cann't decode instructions longer than 32 bits\n");
		return 1;
	}

	uint32_t t;

	inst_size = 32;

	switch (instruction & 0x7f) {
	case 0x6f:
		inst_type = TraceDqr::INST_JAL;
		is_branch = true;

		t = MOVE_BIT(instruction,21,1);
		t |= MOVE_BIT(instruction,22,2);
		t |= MOVE_BIT(instruction,23,3);
		t |= MOVE_BIT(instruction,24,4);
		t |= MOVE_BIT(instruction,25,5);
		t |= MOVE_BIT(instruction,26,6);
		t |= MOVE_BIT(instruction,27,7);
		t |= MOVE_BIT(instruction,28,8);
		t |= MOVE_BIT(instruction,29,9);
		t |= MOVE_BIT(instruction,30,10);
		t |= MOVE_BIT(instruction,20,11);
		t |= MOVE_BIT(instruction,12,12);
		t |= MOVE_BIT(instruction,13,13);
		t |= MOVE_BIT(instruction,14,14);
		t |= MOVE_BIT(instruction,15,15);
		t |= MOVE_BIT(instruction,16,16);
		t |= MOVE_BIT(instruction,17,17);
		t |= MOVE_BIT(instruction,18,18);
		t |= MOVE_BIT(instruction,19,19);
		t |= MOVE_BIT(instruction,31,20);

		if (t & (1<<20)) { // sign extend offset
			t |= 0xffe00000;
		}

		immediate = t;
		rd = (TraceDqr::Reg)((instruction >> 7) & 0x1f);
		rs1 = TraceDqr::REG_unknown;
		break;
	case 0x67:
		if ((instruction & 0x7000) == 0x000) {
			inst_type = TraceDqr::INST_JALR;
			is_branch = true;

			t = instruction >> 20;

			if (t & (1<<11)) { // sign extend offset
				t |= 0xfffff000;
			}

			immediate = t;
			rd = (TraceDqr::Reg)((instruction >> 7) & 0x1f);
			rs1 = (TraceDqr::Reg)((instruction >> 15) & 0x1f);
		}
		else {
			inst_type = TraceDqr::INST_UNKNOWN;
			immediate = 0;
			rd = TraceDqr::REG_unknown;
			rs1 = TraceDqr::REG_unknown;
			is_branch = false;
		}
		break;
	case 0x63:
		switch ((instruction >> 12) & 0x7) {
		case 0x0:
			inst_type = TraceDqr::INST_BEQ;
			break;
		case 0x1:
			inst_type = TraceDqr::INST_BNE;
			break;
		case 0x4:
			inst_type = TraceDqr::INST_BLT;
			break;
		case 0x5:
			inst_type = TraceDqr::INST_BGE;
			break;
		case 0x6:
			inst_type = TraceDqr::INST_BLTU;
			break;
		case 0x7:
			inst_type = TraceDqr::INST_BGEU;
			break;
		default:
			inst_type = TraceDqr::INST_UNKNOWN;
			immediate = 0;
			rd = TraceDqr::REG_unknown;
			rs1 = TraceDqr::REG_unknown;
			is_branch = false;
			return 0;
		}

		is_branch = true;

		t = MOVE_BIT(instruction,8,1);
		t |= MOVE_BIT(instruction,9,2);
		t |= MOVE_BIT(instruction,10,3);
		t |= MOVE_BIT(instruction,11,4);
		t |= MOVE_BIT(instruction,25,5);
		t |= MOVE_BIT(instruction,26,6);
		t |= MOVE_BIT(instruction,27,7);
		t |= MOVE_BIT(instruction,28,8);
		t |= MOVE_BIT(instruction,29,9);
		t |= MOVE_BIT(instruction,30,10);
		t |= MOVE_BIT(instruction,7,11);
		t |= MOVE_BIT(instruction,31,12);

		if (t & (1<<12)) { // sign extend offset
			t |= 0xffffe000;
		}

		immediate = t;
		rd = TraceDqr::REG_unknown;
		rs1 = (TraceDqr::Reg)((instruction >> 15) & 0x1f);
		break;
	case 0x73:
		if (instruction == 0x00200073) {
			inst_type = TraceDqr::INST_URET;
			is_branch = true;
			immediate = 0;
		}
		else if (instruction == 0x10200073) {
			inst_type = TraceDqr::INST_SRET;
			is_branch = true;
			immediate = 0;
		}
		else if (instruction == 0x30200073) {
			inst_type = TraceDqr::INST_MRET;
			is_branch = true;
			immediate = 0;
		}
		else if (instruction == 0x00000073) {
			inst_type = TraceDqr::INST_ECALL;
			immediate = 0;
			is_branch = true;
		}
		else if (instruction == 0x00100073) {
			inst_type = TraceDqr::INST_EBREAK;
			immediate = 0;
			is_branch = true;
		}
		else {
			inst_type = TraceDqr::INST_UNKNOWN;
			immediate = 0;
			rd = TraceDqr::REG_unknown;
			rs1 = TraceDqr::REG_unknown;
			is_branch = false;
		}
		break;
	default:
		inst_type = TraceDqr::INST_UNKNOWN;
		immediate = 0;
		rd = TraceDqr::REG_unknown;
		rs1 = TraceDqr::REG_unknown;
		is_branch = false;
		break;
	}

	return 0;
}

int Disassembler::decodeInstruction(uint32_t instruction,int archSize,int &inst_size,TraceDqr::InstType &inst_type,TraceDqr::Reg &rs1, TraceDqr::Reg &rd,int32_t &immediate,bool &is_branch)
{
	int rc;

	switch (archSize) {
	case 32:
		switch (instruction & 0x0003) {
		case 0x0000:	// quadrant 0, compressed
			rc = decodeRV32Q0Instruction(instruction,inst_size,inst_type,rs1,rd,immediate,is_branch);
			break;
		case 0x0001:	// quadrant 1, compressed
			rc = decodeRV32Q1Instruction(instruction,inst_size,inst_type,rs1,rd,immediate,is_branch);
			break;
		case 0x0002:	// quadrant 2, compressed
			rc = decodeRV32Q2Instruction(instruction,inst_size,inst_type,rs1,rd,immediate,is_branch);
			break;
		case 0x0003:	// not compressed. Assume RV32 for now
			rc = decodeRV32Instruction(instruction,inst_size,inst_type,rs1,rd,immediate,is_branch);
			break;
		}
		break;
	case 64:
		switch (instruction & 0x0003) {
		case 0x0000:	// quadrant 0, compressed
			rc = decodeRV64Q0Instruction(instruction,inst_size,inst_type,rs1,rd,immediate,is_branch);
			break;
		case 0x0001:	// quadrant 1, compressed
			rc = decodeRV64Q1Instruction(instruction,inst_size,inst_type,rs1,rd,immediate,is_branch);
			break;
		case 0x0002:	// quadrant 2, compressed
			rc = decodeRV64Q2Instruction(instruction,inst_size,inst_type,rs1,rd,immediate,is_branch);
			break;
		case 0x0003:	// not compressed. Assume RV32 for now
			rc = decodeRV64Instruction(instruction,inst_size,inst_type,rs1,rd,immediate,is_branch);
			break;
		}
		break;
	default:
		printf("Error: (): Unknown arch size %d\n",archSize);

		rc = 1;
	}

	return rc;
}

// make all path separators either '/' or '\'; also remove '/./' and /../. Remove weird double path
// showing up on linux (libbfd issue)

void static sanePath(TraceDqr::pathType pt,const char *src,char *dst)
{
	char drive = 0;
	int r = 0;
	int w = 0;
	char sep;

	if (pt == TraceDqr::PATH_RAW) {
		strcpy(dst,src);

		return;
	}

	if (pt == TraceDqr::PATH_TO_WINDOWS) {
		sep = '\\';
	}
	else {
		sep = '/';
	}

	while (src[r] != 0) {
		switch (src[r]) {
		case ':':
			if (w > 0) {
				if (((dst[w-1] >= 'a') && (dst[w-1] <= 'z')) || ((dst[w-1] >= 'A') && (dst[w-1] <= 'Z'))) {
					drive = dst[w-1];
					w = 0;
					dst[w] = drive;
					w += 1;
				}
			}

			dst[w] = src[r];
			w += 1;
			r += 1;
			break;
		case '/':
		case '\\':
			if ((w > 0) && (dst[w-1] == sep)) {
				// skip; remove double /
				r += 1;
			}
			else {
				dst[w] = sep;
				w += 1;
				r += 1;
			}
			break;
		case '.':
			if ((w > 0) && (dst[w-1] == sep)) {
				if ((src[r+1] == '/') || (src[r+1] == '\\')) { // have \.\ or /./
					r += 2;
				}
				else if ((src[r+1] == '.') && ((src[r+2] == '/') || (src[r+2] == '\\'))) { // have \..\ or /../
					// scan back in w until either sep or beginning of line is found
					w -= 1;
					while ((w > 0) && (dst[w-1] != sep)) {
						w -= 1;
					}
					r += 3;
				}
				else { // have a '.' in a name - just pass it on
					dst[w] = '.';
					w += 1;
					r += 1;
				}
			}
			else {	// might be at beginning of string. Could be a ./ or a ../; copy the . or .. if followed by a /
				if ((w == 0 ) && ((src[r+1] == '/') || (src[r+1] == '\\'))) {
					// have a ./ at the beginning - strip it off
					r += 2;
				}
				else {
					dst[w] = '.';
					w += 1;
					r += 1;
				}
			}
			break;
		default:
			dst[w] = src[r];
			w += 1;
			r += 1;
			break;
		}
	}

	dst[w] = 0;
}

int Disassembler::getSrcLines(TraceDqr::ADDRESS addr, const char **filename, const char **functionname, unsigned int *linenumber, const char **lineptr)
{
	const char *file = nullptr;
	const char *function = nullptr;
	unsigned int line = 0;
	unsigned int discrim;

	// need to loop through all sections with code below and try to find one that succeeds

	section *sp;

	*filename = nullptr;
	*functionname = nullptr;
	*linenumber = 0;
	*lineptr = nullptr;

	if (codeSectionLst == nullptr) {
		return 0;
	}

	sp = codeSectionLst->getSectionByAddress(addr);

	if (sp == nullptr) {
		return 0;
	}

	if (bfd_find_nearest_line_discriminator(abfd,sp->asecptr,symbol_table,addr-sp->startAddr,&file,&function,&line,&discrim) == 0) {
		return 0;
	}

	*linenumber = line;

	if (file == nullptr) {
		return 0;
	}

	char fprime[2048];
	char *sane;

	if (strlen(file) > sizeof fprime) {
		sane = new char[strlen(file)+1];
	}
	else {
		sane = fprime;
	}

	sanePath(pType,file,sane);

	// the fl/filelist stuff is to get the source line text

	struct fileReader::fileList *fl;

	fl = fileReader->findFile(sane);
	assert(fl != nullptr);

	*filename = fl->name;

	// save function name and line src in function list struct so it will be saved for reuse, or later use throughout the
	// life of the object. Won't be overwritten.

	if (function != nullptr) {
		// find the function name in fncList

		struct fileReader::funcList *funcLst;

		for (funcLst = fl->funcs; (funcLst != nullptr) && (strcmp(funcLst->func,function) != 0); funcLst = funcLst->next) {
			// empty
		}

		if (funcLst == nullptr) {
			// add function to the list

			int len = strlen(function)+1;
			char *fname = new char[len];
			strcpy(fname,function);

			funcLst = new fileReader::funcList;
			funcLst->func = fname;
			funcLst->next = fl->funcs;
			fl->funcs = funcLst;
		}

		// at this point funcLst should point to this function (if it was just added or not)

		*functionname = funcLst->func;
	}

	// line numbers start at 1

	if ((line >= 1) && (line <= fl->lineCount)) {
		*lineptr = fl->lines[line-1];
	}

	if (sane != fprime) {
		delete sane;
		sane = nullptr;
	}

	return 1;
}

int Disassembler::Disassemble(TraceDqr::ADDRESS addr)
{
	assert(disassemble_func != nullptr);

	bfd_vma vma;
	vma = (bfd_vma)addr;

	if (codeSectionLst == nullptr) {
		return 1;
	}

	section *sp = codeSectionLst->getSectionByAddress(addr);

	if (sp == nullptr) {
		return 1;
	}

	cachedInstInfo *cii;

	cii = sp->getCachedInfo(addr);
	if (cii != nullptr) {
		source.sourceFile = cii->filename;
		source.sourceFunction = cii->functionname;
		source.sourceLineNum = cii->linenumber;
		source.sourceLine = cii->lineptr;

		instruction.address = addr;
		instruction.instruction = cii->instruction;
		instruction.instSize = cii->instsize;
		strcpy(instruction.instructionText,cii->instructionText);

		instruction.addressLabel = cii->addressLabel;
		instruction.addressLabelOffset = cii->addressLabelOffset;
		instruction.haveOperandAddress = cii->haveOperandAddress;
		instruction.operandAddress = cii->operandAddress;
		instruction.operandLabel = cii->operandLabel;
		instruction.operandLabelOffset = cii->operandLabelOffset;

		// instruction.timestamp = 0;
		// instruction.cycles = 0;

		return 0;
	}

	getSrcLines(addr, &source.sourceFile, &source.sourceFunction, &source.sourceLineNum, &source.sourceLine);

	setInstructionAddress(vma);

	int rc;

	instruction.instructionText[0] = 0;
	dis_output = instruction.instructionText;

	instruction.haveOperandAddress = false;

	// before calling disassemble_func, need to update info struct to point to correct section!

   	info->buffer_vma = sp->startAddr;
   	info->buffer_length = sp->size;
   	info->section = sp->asecptr;

   	// potential memory leak below the first time this is done because buffer was initially allocated by bfd

   	info->buffer = (bfd_byte*)sp->code;

	rc = disassemble_func(vma,info);

	// output from disassemble_func is in instruction.instrucitonText

	sp->setCachedInfo(addr,source.sourceFile,source.sourceFunction,source.sourceLineNum,source.sourceLine,instruction.instructionText,instruction.instruction,instruction.instSize,instruction.addressLabel,instruction.addressLabelOffset,instruction.haveOperandAddress,instruction.operandAddress,instruction.operandLabel,instruction.operandLabelOffset);

	return rc;
}

AddrStack::AddrStack(int size)
{
	stackSize = size;
	sp = size;

	if (size == 0) {
		stack = nullptr;
	}
	else {
		stack = new TraceDqr::ADDRESS[size];
	}
}

AddrStack::~AddrStack()
{
	if (stack != nullptr) {
		delete [] stack;
		stack = nullptr;
	}

	stackSize = 0;
	sp = 0;
}

void AddrStack::reset()
{
	sp = stackSize;
}

int AddrStack::push(TraceDqr::ADDRESS addr)
{
	if (sp <= 0) {
		return 1;
	}

	sp -= 1;
	stack[sp] = addr;

	return 0;
}

TraceDqr::ADDRESS AddrStack::pop()
{
	if (sp >= stackSize) {
		return -1;
	}

	TraceDqr::ADDRESS t = stack[sp];
	sp += 1;

	return t;
}

NexusMessageSync::NexusMessageSync()
{
	firstMsgNum = 0;
	lastMsgNum  = 0;
	index = 0;
}

Simulator::Simulator(char *f_name,int arch_size)
{
	TraceDqr::DQErr ec;

	vf_name = nullptr;
	lineBuff = nullptr;
	lines = nullptr;
	numLines = 0;
	nextLine = 0;
	currentCore = 0;
	flushing = false;

	elfReader = nullptr;
	disassembler = nullptr;

	if (f_name == nullptr) {
		status = TraceDqr::DQERR_ERR;
		return;
	}

	ec = readFile(f_name);
	if (ec != TraceDqr::DQERR_OK) {
		status = ec;
		return;
	}

//	ec = parseFile();
//	if (ec != TraceDqr::DQERR_OK) {
//		status = ec;
//		return;
//	}

	// prep the dissasembler

	archSize = arch_size;

	init_disassemble_info(&disasm_info,stdout,(fprintf_ftype)stringify_callback);

	disasm_info.arch = bfd_arch_riscv;

	int mach;

	if (archSize == 64) {
		disasm_info.mach = bfd_mach_riscv64;
		mach = bfd_mach_riscv64;
	}
	else {
		disasm_info.mach = bfd_mach_riscv32;
		mach = bfd_mach_riscv32;
	}

	disasm_func = ::disassembler((bfd_architecture)67/*bfd_arch_riscv*/,false,mach,nullptr);
	if (disasm_func == nullptr) {
		status = TraceDqr::DQERR_ERR;
		return;
	}

	disasm_info.read_memory_func = buffer_read_memory;
	disasm_info.buffer = (bfd_byte*)instructionBuffer;
	disasm_info.buffer_vma = 0;
	disasm_info.buffer_length = sizeof instructionBuffer;

	disassemble_init_for_target(&disasm_info);

	for (int i = 0; (size_t)i < sizeof currentAddress / sizeof currentAddress[0]; i++) {
		currentAddress[i] = 0;
	}

	for (int i = 0; (size_t)i < sizeof currentTime / sizeof currentTime[0]; i++) {
		currentTime[i] = 0;
	}

	for (int i = 0; (size_t)i < sizeof haveCurrentSrec / sizeof haveCurrentSrec[0]; i++) {
		haveCurrentSrec[i] = false;
	}

	for (int i = 0; (size_t)i < sizeof enterISR / sizeof enterISR[0]; i++) {
		enterISR[i] = 0;
	}

	status = TraceDqr::DQERR_OK;
	return;
}

Simulator::Simulator(char *f_name,char *e_name)
{
	TraceDqr::DQErr ec;

	vf_name = nullptr;
	lineBuff = nullptr;
	lines = nullptr;
	numLines = 0;
	nextLine = 0;
	currentCore = 0;
	flushing = false;
	elfReader = nullptr;
	disassembler = nullptr;

	if (f_name == nullptr) {
		status = TraceDqr::DQERR_ERR;
		return;
	}

	ec = readFile(f_name);
	if (ec != TraceDqr::DQERR_OK) {
		status = ec;
		return;
	}

    elfReader = new (std::nothrow) ElfReader(e_name);

    assert(elfReader != nullptr);

    if (elfReader->getStatus() != TraceDqr::DQERR_OK) {
    	delete elfReader;
    	elfReader = nullptr;

    	status = TraceDqr::DQERR_ERR;

    	return;
    }

    bfd *abfd;
    abfd = elfReader->get_bfd();

	disassembler = new (std::nothrow) Disassembler(abfd);

	assert(disassembler != nullptr);

	if (disassembler->getStatus() != TraceDqr::DQERR_OK) {
		if (elfReader != nullptr) {
			delete elfReader;
			elfReader = nullptr;
		}

		delete disassembler;
		disassembler = nullptr;

		status = TraceDqr::DQERR_ERR;

		return;
	}

	archSize = elfReader->getArchSize();

	for (int i = 0; (size_t)i < sizeof currentAddress / sizeof currentAddress[0]; i++) {
		currentAddress[i] = 0;
	}

	for (int i = 0; (size_t)i < sizeof currentTime / sizeof currentTime[0]; i++) {
		currentTime[i] = 0;
	}

	for (int i = 0; (size_t)i < sizeof haveCurrentSrec / sizeof haveCurrentSrec[0]; i++) {
		haveCurrentSrec[i] = false;
	}

	for (int i = 0; (size_t)i < sizeof enterISR / sizeof enterISR[0]; i++) {
		enterISR[i] = 0;
	}

	status = TraceDqr::DQERR_OK;
	return;
}

Simulator::~Simulator()
{
	cleanUp();
}

void Simulator::cleanUp()
{
	if (vf_name != nullptr) {
		delete [] vf_name;
		vf_name = nullptr;
	}

	if (lineBuff != nullptr) {
		delete [] lineBuff;
		lineBuff = nullptr;
	}

	if (elfReader != nullptr) {
		delete elfReader;
		elfReader = nullptr;
	}

	if (disassembler != nullptr) {
		delete disassembler;
		disassembler = nullptr;
	}
}

TraceDqr::DQErr Simulator::readFile(char *file)
{
	if (file == nullptr) {
		status = TraceDqr::DQERR_ERR;

		return status;
	}

//	printf("Simulator::readFile(%s)\n",file);

	int l = strlen(file);
	vf_name = new char [l+1];

	strcpy(vf_name,file);

	std::ifstream  f(file, std::ifstream::binary);

	if (!f) {
		printf("Error: Simulator::readAndParse(): could not open verilator file %s for input\n",vf_name);
		status = TraceDqr::DQERR_ERR;

		return status;
	}

	// get length of file:

	f.seekg (0, f.end);
	int length = f.tellg();
	f.seekg (0, f.beg);

	// allocate memory:

	lineBuff = new char [length];

	// read file into buffer

	f.read(lineBuff,length);

	f.close();

	// count lines

	int lc = 1;

	for (int i = 0; i < length; i++) {
		if (lineBuff[i] == '\n') {
			lc += 1;
		}
	}

	lines = new char *[lc];

	// initialize arry of ptrs

	int s;

	l = 0;
	s = 1;

	lines[0] = &lineBuff[0];

	for (int i = 1; i < length;i++) {
		if (s != 0) {
			lines[l] = &lineBuff[i];
			l += 1;
			s = 0;
		}

		// strip out CRs and LFs

		if (lineBuff[i] == '\r') {
			lineBuff[i] = 0;
		}
		else if (lineBuff[i] == '\n') {
			lineBuff[i] = 0;
			s = 1;
		}
	}

	if (l >= lc) {
		delete [] lines;
		delete [] lineBuff;

		lines = nullptr;
		lineBuff = nullptr;

		printf("Error: Simulator::readAndParse(): Error computing line count for file\n");

		status = TraceDqr::DQERR_ERR;
		return status;
	}

	lines[l] = nullptr;

	numLines = l;


	return TraceDqr::DQERR_OK;
}

void SRec::dump()
{
	printf("SRec: %d",validLine);

	if (validLine) {
		printf(" line:%d",line);
		printf(" core:%d",coreId);
		printf(" cycles: %d",cycles);
		printf(" valid: %d",valid);
		printf(" pc=[%08llx]",pc);
		printf(" W[r%2d=%08x][%d]",wReg,wVal,wvf);
		printf(" R[r%2d=%08x]",r1Reg,r1Val);
		printf(" R[r%2d=%08x]",r2Reg,r2Val);
		printf(" inst=[%08x]",inst);
		printf(" DASM(%08x)\n",dasm);
	}
}

TraceDqr::DQErr Simulator::parseLine(int l, SRec *srec)
{
	if (l >= numLines) {
		return TraceDqr::DQERR_EOF;
	}

	char *lp = lines[l];
	int ci = 0;
	char *ep;

	srec->validLine = false;
	srec->valid = false;
	srec->line = l;
	srec->haveFRF = false;

	// No syntax errors until find first line that starts with 'C'

	// strip ws

	while (lp[ci] == ' ') {
		ci += 1;
	}

	// check for core specifyer

	if (lp[ci] != 'C') {
		return TraceDqr::DQERR_OK;
	}

	ci += 1;

	// Skip whitespace (seen out files with "C:     0")
	while (lp[ci] == ' ') {
		ci += 1;
	}

	if (!isdigit(lp[ci])) {
		// Still a comment line
		return TraceDqr::DQERR_OK;
	}

	srec->coreId = strtoul(&lp[ci],&ep,10);

	if (ep == &lp[ci]) {
		printf("Simulator::parseLine(): syntax error. Expected core number\n");
		printf("Line %d:%d: '%s'\n",l,ci+1,lp);

		return TraceDqr::DQERR_ERR;
	}

	ci = ep - lp;

	while (lp[ci] == ' ') {
		ci += 1;
	}

	if (lp[ci] != ':') {
		printf("Simulator::parseLine(): syntax error. Expected ':' at end of core number\n");
		printf("Line %d:%d: '%s'\n",l,ci+1,lp);

		return TraceDqr::DQERR_ERR;
	}

	ci += 1;

	srec->cycles = strtoul(&lp[ci],&ep,10);

	if (ep == &lp[ci]) {
		printf("Simulator::parseLine(): syntax error. Expected cycle count\n");
		printf("Line %d:%d: '%s'\n",l,ci+1,lp);

		return TraceDqr::DQERR_ERR;
	}

	ci = ep - lp;

	while (lp[ci] == ' ') {
		ci += 1;
	}

	if (strncmp("frf",&lp[ci],3) == 0) {
		ci += 3;

		if (lp[ci] != '[') {
			printf("Simulator::parseLine(): syntax error. Execpted '[' after frf\n");
			printf("Line %d:%d: '%s'\n",l,ci+1,lp);

			return TraceDqr::DQERR_ERR;
		}

		ci += 1;

		srec->wReg = strtoull(&lp[ci],&ep,16);

		if (&lp[ci] == ep) {
			printf("Simulator::parseLine(): syntax error parsing frf register number\n");
			printf("Line %d:%d: '%s'\n",l,ci+1,lp);

			return TraceDqr::DQERR_ERR;
		}

		ci = ep - lp;

		if (lp[ci] != ']') {
			printf("Simulator::parseLine(): syntax error. Expected ']' after frf register number\n");
			printf("Line %d:%d: '%s'\n",l,ci+1,lp);

			return TraceDqr::DQERR_ERR;
		}

		ci += 1;

		while (lp[ci] == ' ') {
			ci += 1;
		}

		if (lp[ci] != '=') {
			printf("Simulator::parseLine(): syntax error. Expected '=' after frf register number\n");
			printf("Line %d:%d: '%s'\n",l,ci+1,lp);

			return TraceDqr::DQERR_ERR;
		}

		ci += 1;

		while (lp[ci] == ' ') {
			ci += 1;
		}

		if (lp[ci] != '[') {
			printf("Simulator::parseLine(): syntax error. Expected '[' after frf '='\n");
			printf("Line %d:%d: '%s'\n",l,ci+1,lp);

			return TraceDqr::DQERR_ERR;
		}

		ci += 1;

		srec->frfAddr = strtoull(&lp[ci],&ep,16);

		if (&lp[ci] == ep) {
			printf("Simulator::parseLine(): syntax error parsing frf address\n");
			printf("Line %d:%d: '%s'\n",l,ci+1,lp);

			return TraceDqr::DQERR_ERR;
		}

		ci = ep - lp;

		if (lp[ci] != ']') {
			printf("Simulator::parseLine(): syntax error. Expected ']' after frf address\n");
			printf("Line %d:%d: '%s'\n",l,ci+1,lp);

			return TraceDqr::DQERR_ERR;
		}

		ci += 1;

		while (lp[ci] == ' ') {
			ci += 1;
		}

		if (lp[ci] != 0) {
			printf("Simulator::parseLine(): extra input on end of line; ignoring\n");
		}

		// don't set srec->valid to true because frf records are different

		srec->haveFRF = true;
		srec->validLine = true;

		return TraceDqr::DQERR_OK;
	}

	if (lp[ci] != '[') {
		printf("Simulator::parseLine(): syntax error. Expected '[' after cycle count\n");
		printf("Line %d:%d: '%s'\n",l,ci+1,lp);

		return TraceDqr::DQERR_ERR;
	}

	ci += 1;

	if ((lp[ci] >= '0') && (lp[ci] <= '1')) {
		srec->valid = lp[ci] - '0';
	}
	else {
		printf("Simulator::parseLine(): syntax error. Expected valid flag of either 0 or 1\n");
		printf("Line %d:%d: '%s'\n",l,ci+1,lp);

		return TraceDqr::DQERR_ERR;
	}

	ci += 1;

	if (lp[ci] != ']') {
		printf("Simulator::parseLine(): syntax error. Expected ']' after valid flag\n");
		printf("Line %d:%d: '%s'\n",l,ci+1,lp);

		return TraceDqr::DQERR_ERR;
	}

	ci += 1;

	while (lp[ci] == ' ') {
		ci += 1;
	}

	if (strncmp("pc=[",&lp[ci],sizeof "pc=[" - 1) != 0) {
		printf("Simulator::parseLine(): syntax error. Expected pc=[\n");
		printf("Line %d:%d: '%s'\n",l,ci+1,lp);

		return TraceDqr::DQERR_ERR;
	}

	ci += sizeof "pc=[" - 1;

	srec->pc = strtoull(&lp[ci],&ep,16);

	if (&lp[ci] == ep) {
		printf("Simulator::parseLine(): syntax error parsing PC value\n");
		printf("Line %d:%d: '%s'\n",l,ci+1,lp);

		return TraceDqr::DQERR_ERR;
	}

	ci = ep - lp;

	if (lp[ci] != ']') {
		printf("Simulator::parseLine(): syntax error. Expected ']' after PC address\n");
		printf("Line %d:%d: '%s'\n",l,ci+1,lp);

		return TraceDqr::DQERR_ERR;
	}

	ci += 1;

	int numReads = 0;

	for (int i = 0; i < 3; i++) { // get dst and two src operands
		while (lp[ci] == ' ') {
			ci += 1;
		}

		bool wf = false;
		bool rf = false;

		if (lp[ci] == 'W') {
			wf = true;
		}
		else if (lp[ci] == 'R') {
			rf = true;
			numReads += 1;
		}

		if ((rf != true) && (wf != true)) {
			printf("Simulator::parseLine(): syntax error. Expected read or write specifier\n");
			printf("Line %d:%d: '%s'\n",l,ci+1,lp);

			return TraceDqr::DQERR_ERR;
		}

		ci += 1;

		if (lp[ci] != '[') {
			printf("Simulator::parseLine(): syntax error. Expected '[' after read or write specifier\n");
			printf("Line %d:%d: '%s'\n",l,ci+1,lp);

			return TraceDqr::DQERR_ERR;
		}

		ci += 1;

		if (lp[ci] != 'r') {
			printf("Simulator::parseLine(): syntax error. Expected register specifier\n");
			printf("Line %d:%d: '%s'\n",l,ci+1,lp);

			return TraceDqr::DQERR_ERR;
		}

		ci += 1;

		uint32_t regNum;
		regNum = strtoul(&lp[ci],&ep,10);

		if (ep == &lp[ci]) {
			printf("Simulator::parseLine(): syntax error. Expected register number\n");
			printf("Line %d:%d: '%s'\n",l,ci+1,lp);

			return TraceDqr::DQERR_ERR;
		}

		ci = ep - lp;

		if (lp[ci] != '=') {
			printf("Simulator::parseLine(): syntax error. Expected '='\n");
			printf("Line %d:%d: '%s'\n",l,ci+1,lp);

			return TraceDqr::DQERR_ERR;
		}

		ci += 1;

		uint32_t regVal;
		regVal = strtoul(&lp[ci],&ep,16);

		if (&lp[ci] == ep) {
			printf("Simulator::parseLine(): syntax error. Expected register value\n");
			printf("Line %d:%d: '%s'\n",l,ci+1,lp);

			return TraceDqr::DQERR_ERR;
		}

		ci = ep - lp;

		if (lp[ci] != ']') {
			printf("Simulator::parseLine(): syntax error. Expected ']' after register value\n");
			printf("Line %d:%d: '%s'\n",l,ci+1,lp);

			return TraceDqr::DQERR_ERR;
		}

		ci += 1;

		if (wf) {
			// for writes

			srec->wReg = regNum;
			srec->wVal = regVal;

			if (lp[ci] != '[') {
				printf("Simulator::parseLine(): syntax error. Expected '[' for write valid flag\n");
				printf("Line %d:%d: '%s'\n",l,ci+1,lp);

				return TraceDqr::DQERR_ERR;
			}

			ci += 1;

			if ((lp[ci] < '0') || (lp[ci] > '1')) {
				printf("Simulator::parseLine(): syntax error. Expected write valid flag of either '0' or '1'\n");
				printf("Line %d:%d: '%s'\n",l,ci+1,lp);

				return TraceDqr::DQERR_ERR;
			}

			srec->wvf = lp[ci] - '0';
			ci += 1;

			if (lp[ci] != ']') {
				printf("Simulator::parseLine(): syntax error. Expected ']' after write valid flag\n");
				printf("Line %d:%d: '%s'\n",l,ci+1,lp);

				return TraceDqr::DQERR_ERR;
			}

			ci += 1;
		}
		else {
			// for reads

			if (numReads == 1) {
				srec->r1Reg = regNum;
				srec->r1Val = regVal;
			}
			else {
				srec->r2Reg = regNum;
				srec->r2Val = regVal;
			}
		}
	}

	ci += 1;

	while (lp[ci] == ' ') {
		ci += 1;
	}

	if (strncmp(&lp[ci],"inst=[",sizeof "inst=[" - 1) != 0) {
		printf("Simulator::parseLine(): syntax error. Expected 'inst='\n");
		printf("Line %d:%d: '%s'\n",l,ci+1,lp);

		return TraceDqr::DQERR_ERR;
	}

	ci += sizeof "inst=[" - 1;

	srec->inst = strtoul(&lp[ci],&ep,16);
	if (ep == &lp[ci]) {
		printf("Simulator::parseLine(): syntax error parsing instruction\n");
		printf("Line %d:%d: '%s'\n",l,ci+1,lp);

		return TraceDqr::DQERR_ERR;
	}

	ci = ep - lp;

	if (lp[ci] != ']') {
		printf("Simulator::parseLine(): syntax error parsing instruction. Expected ']'\n");
		printf("Line %d:%d: '%s'\n",l,ci+1,lp);

		return TraceDqr::DQERR_ERR;
	}

	ci += 1;

	while (lp[ci] == ' ') {
		ci += 1;
	}

	if (strncmp(&lp[ci],"DASM(",sizeof "DASM(" - 1) != 0) {
		printf("Simulator::parseLine(): syntax error. Expected 'DASM('\n");
		printf("Line %d:%d: '%s'\n",l,ci+1,lp);

		return TraceDqr::DQERR_ERR;
	}

	ci += sizeof "DASM(" - 1;

	srec->dasm = strtoul(&lp[ci],&ep,16);
	if (ep == &lp[ci]) {
		printf("Simulator::parseLine(): syntax error parsing DASM\n");
		printf("Line %d:%d: '%s'\n",l,ci+1,lp);

		return TraceDqr::DQERR_ERR;
	}

	ci = ep - lp;

	if (lp[ci] != ')') {
		printf("Simulator::parseLine(): syntax error parsing DASM. Expected ')'\n");
		printf("Line %d:%d: '%s'\n",l,ci+1,lp);

		return TraceDqr::DQERR_ERR;
	}

	ci += 1;

	while (lp[ci] == ' ') {
		ci += 1;
	}

	if (lp[ci] != 0) {
		printf("Simulator::parseLine(): extra input on end of line; ignoring\n");
	}

	srec->validLine = true;

	return TraceDqr::DQERR_OK;
}

TraceDqr::DQErr Simulator::parseFile()
{
	TraceDqr::DQErr s;
	SRec srec;

	for (int i = 0; i < numLines; i++) {
		s = parseLine(i,&srec);
		if (s != TraceDqr::DQERR_OK) {
			status = s;
			printf("Error parsing file!\n");
			return s;
		}
		srec.dump();
	}

	return TraceDqr::DQERR_OK;
}

TraceDqr::DQErr Simulator::computeBranchFlags(TraceDqr::ADDRESS currentAddr,uint32_t currentInst, TraceDqr::ADDRESS &nextAddr,int &crFlag,TraceDqr::BranchFlags &brFlag)
{
	int inst_size;
	TraceDqr::InstType inst_type;
	int32_t immediate;
	bool isBranch;
	int rc;
	TraceDqr::Reg rs1;
	TraceDqr::Reg rd;

	brFlag = TraceDqr::BRFLAG_none;
	crFlag = enterISR[currentCore];
	enterISR[currentCore] = 0;

//	if ((expectedAddress != -1) &&(expectedAddress != currentAddress) {
//		crFlag |= TraceDqr::isInterrupt;
//	}

	// figure out how big the instruction is
	// Note: immediate will already be adjusted - don't need to mult by 2 before adding to address

	rc = Disassembler::decodeInstruction(currentInst,archSize,inst_size,inst_type,rs1,rd,immediate,isBranch);
	if (rc != 0) {
		printf("Error: computeBranchFlags(): Cannot decode instruction %04x\n",currentInst);

		status = TraceDqr::DQERR_ERR;

		return status;
	}

	switch (inst_type) {
	case TraceDqr::INST_UNKNOWN:
		if ((currentAddr + inst_size/8) != nextAddr) {
			enterISR[currentCore] = TraceDqr::isInterrupt;
		}
		break;
	case TraceDqr::INST_JAL:
		// rd = pc+4 (rd can be r0)
		// pc = pc + (sign extended immediate offset)
		// plain unconditional jumps use rd -> r0
		// inferrable unconditional

		if ((rd == TraceDqr::REG_1) || (rd == TraceDqr::REG_5)) { // rd == link
			crFlag |= TraceDqr::isCall;
		}

		if (currentAddr + immediate != nextAddr) {
			enterISR[currentCore] = TraceDqr::isInterrupt;
		}
		break;
	case TraceDqr::INST_JALR:
		// rd = pc+4 (rd can be r0)
		// pc = pc + ((sign extended immediate offset) + rs) & 0xffe
		// plain unconditional jumps use rd -> r0
		// not inferrable unconditional

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

		// without more info, don't know if enterISR should be set or not, so we leave it alone for now
		// could include register info and then we could tell
		break;
	case TraceDqr::INST_BEQ:
	case TraceDqr::INST_BNE:
	case TraceDqr::INST_BLT:
	case TraceDqr::INST_BGE:
	case TraceDqr::INST_BLTU:
	case TraceDqr::INST_BGEU:
	case TraceDqr::INST_C_BEQZ:
	case TraceDqr::INST_C_BNEZ:
		// pc = pc + (sign extend immediate offset) (BLTU and BGEU are not sign extended)
		// inferrable conditional

		if (nextAddr == (currentAddr + inst_size / 8)) {
			brFlag = TraceDqr::BRFLAG_notTaken;
		}
		else if (nextAddr == (currentAddr + immediate)) {
			brFlag = TraceDqr::BRFLAG_taken;
		}
		else {
			enterISR[currentCore] = TraceDqr::isInterrupt;
		}
		break;
	case TraceDqr::INST_C_J:
		// pc = pc + (signed extended immediate offset)
		// inferrable unconditional

		if (currentAddr + immediate != nextAddr) {
			enterISR[currentCore] = TraceDqr::isInterrupt;
		}
		break;
	case TraceDqr::INST_C_JAL:
		// btm, htm same

		// x1 = pc + 2
		// pc = pc + (signed extended immediate offset)
		// inferrable unconditional

		if ((rd == TraceDqr::REG_1) || (rd == TraceDqr::REG_5)) { // rd == link
			crFlag |= TraceDqr::isCall;
		}

		if (currentAddr + immediate != nextAddr) {
			enterISR[currentCore] = TraceDqr::isInterrupt;
		}
		break;
	case TraceDqr::INST_C_JR:
		// pc = pc + rs1
		// not inferrable unconditional

		if ((rs1 == TraceDqr::REG_1) || (rs1 == TraceDqr::REG_5)) {
			crFlag |= TraceDqr::isReturn;
		}

		// without more info, don't know if enterISR should be set or not, so we leave it alone for now
		// could include register info and then we could tell
		break;
	case TraceDqr::INST_C_JALR:
		// x1 = pc + 2
		// pc = pc + rs1
		// not inferrble unconditional

		if (rs1 == TraceDqr::REG_5) {
			crFlag |= TraceDqr::isSwap;
		}
		else {
			crFlag |= TraceDqr::isCall;
		}

		// without more info, don't know if enterISR should be set or not, so we leave it alone for now
		// could include register info and then we could tell
		break;
	case TraceDqr::INST_EBREAK:
	case TraceDqr::INST_ECALL:
		crFlag |= TraceDqr::isException;

		// without more info, don't know if enterISR should be set or not, so we leave it alone for now
		// could include register info and then we could tell
		break;
	case TraceDqr::INST_MRET:
	case TraceDqr::INST_SRET:
	case TraceDqr::INST_URET:
		crFlag |= TraceDqr::isExceptionReturn;

		// without more info, don't know if enterISR should be set or not, so we leave it alone for now
		// could include register info and then we could tell
		break;
	default:
		if (currentAddr + immediate != nextAddr) {
			enterISR[currentCore] = TraceDqr::isInterrupt;
		}
		break;
	}

	return TraceDqr::DQERR_OK;
}

TraceDqr::DQErr Simulator::getTraceFileOffset(int &size,int &offset)
{
	size = numLines;
	offset = nextLine;

	return TraceDqr::DQERR_OK;
}

TraceDqr::DQErr Simulator::getNextSrec(int nextLine,SRec &srec)
{
	TraceDqr::DQErr rc;

	do {
		rc = parseLine(nextLine,&srec);
		nextLine += 1;

		if (rc != TraceDqr::DQERR_OK) {
			return rc;
		}
	} while ((srec.validLine == false) || (srec.valid == false));

	// when we get here, we have read the next valid SRec in the input. Could be for any core

	return TraceDqr::DQERR_OK;
}

TraceDqr::DQErr Simulator::flushNextInstruction(Instruction *instInfo, NexusMessage *msgInfo, Source *srcInfo)
{
	TraceDqr::DQErr rc;

	for (int i = 0; i < DQR_MAXCORES; i++) {
		if (haveCurrentSrec[i]){
			haveCurrentSrec[i] = false;

			// don't need to compute branch flags for call/return because this is the last instrucition
			// and we can't tell if a branch is taken or not. But e could determine call/return info.
			// We should be doing that. Dang!

			rc = buildInstructionFromSrec(&currentSrec[i],TraceDqr::BRFLAG_none,TraceDqr::isNone);

			return rc;
		}
	}

	return deferredStatus;
}

TraceDqr::DQErr Simulator::buildInstructionFromSrec(SRec *srec,TraceDqr::BranchFlags brFlags,int crFlag)
{
	// at this point we have two srecs for same core

	int rc;

	rc = Disassemble(srec);
	if (rc != 0) {
		return TraceDqr::DQERR_ERR;
	}

	instructionInfo.brFlags = brFlags;
	instructionInfo.CRFlag = crFlag;

	instructionInfo.timestamp = srec->cycles;

	instructionInfo.r0Val = srec->r1Val;
	instructionInfo.r1Val = srec->r2Val;
	instructionInfo.wVal = srec->wVal;

	if (currentTime[srec->coreId] == 0) {
		instructionInfo.cycles = 0;
	}
	else {
		instructionInfo.cycles = srec->cycles - currentTime[srec->coreId];
	}

	currentTime[srec->coreId] = srec->cycles;

	return TraceDqr::DQERR_OK;
}

int Simulator::Disassemble(SRec *srec)
{
	TraceDqr::DQErr ec;

	if (disassembler != nullptr) {
		disassembler->Disassemble(srec->pc);

		ec = disassembler->getStatus();

		if (ec != TraceDqr::DQERR_OK ) {
			status = ec;
			return 0;
		}

		// the two lines below copy each structure completely. This is probably
		// pretty inefficient, and just returning pointers and using pointers
		// would likely be better

		instructionInfo = disassembler->getInstructionInfo();
		sourceInfo = disassembler->getSourceInfo();
	}
	else {
		// hafta do it all ourselves!

		disasm_info.buffer_vma = srec->pc;
		instructionBuffer[0] = srec->inst;

		instructionInfo.instructionText[0] = 0;
		dis_output = instructionInfo.instructionText;

		// don't need to use global dis_output to point to where to print. Instead, override stream to point to char
		// buffer of where to print data to. This will give multi-instance safe code for verilator and trace objects

		// not easy to cache disassembly because we don't know the address range for the program (can't read
		// the elf file if we don't have one! So can't allocate a block of memory for the code region unless
		// we read through the verilator file and collect info on pc addresses first

		size_t instSize = disasm_func(srec->pc,&disasm_info)*8;

		if (instSize == 0) {
			status = TraceDqr::DQERR_ERR;
			return 1;
		}

		instructionInfo.haveOperandAddress = false;
		instructionInfo.operandAddress = 0;
		instructionInfo.operandLabel = nullptr;
		instructionInfo.operandLabelOffset = 0;
		instructionInfo.addressLabel = nullptr;
		instructionInfo.addressLabelOffset = 0;

		// now turn srec into instrec

		instructionInfo.address = srec->pc;
		instructionInfo.instruction = srec->inst;
		instructionInfo.instSize = instSize;
	}

	instructionInfo.coreId = srec->coreId;

	return 0;
}

TraceDqr::DQErr Simulator::NextInstruction(Instruction *instInfo, NexusMessage *msgInfo, Source *srcInfo, int *flags)
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

TraceDqr::DQErr Simulator::NextInstruction(Instruction **instInfo, NexusMessage **msgInfo, Source **srcInfo)
{
	TraceDqr::DQErr rc;
	int crFlag = 0;
	TraceDqr::BranchFlags brFlags = TraceDqr::BRFLAG_none;
	SRec nextSrec;

	if (instInfo != nullptr) {
		*instInfo = nullptr;
	}

	if (msgInfo != nullptr) {
		*msgInfo = nullptr;
	}

	if (srcInfo != nullptr) {
		*srcInfo = nullptr;
	}

	if (nextLine >= numLines) {
		return TraceDqr::DQERR_EOF;
	}

	if (!flushing) {
		bool done = false;

		do {
			rc = getNextSrec(nextLine,nextSrec);

			if (rc != TraceDqr::DQERR_OK) {
				deferredStatus = rc;
				flushing = true;
				done = true;
			}
			else {
				nextLine = nextSrec.line+1;	// as long as rc is not an error, nextSrec.line is valid

				if (nextSrec.validLine && nextSrec.valid) {
					if (haveCurrentSrec[nextSrec.coreId] == false) {
						currentSrec[nextSrec.coreId] = nextSrec;
						haveCurrentSrec[nextSrec.coreId] = true;

						nextSrec.valid = false;

						// don't set done to true, because we still don't have current and new srecs!
					}
					else {
						// have two consecutive srecs for the same core. We are done looping

						done = true;
					}
				}
				else if (nextSrec.validLine && nextSrec.haveFRF) {
					// for now, just ignore frf records. Don't update time because cycle
					// count time for frf records does not seem to be associated with an
					// instruction

					// currentTime[nextSrec.coreId] = nextSrec.cycles;
				}
			}
		} while (((nextSrec.validLine == false) || (nextSrec.valid == false)) && !done);
	}

	if (flushing) {
		rc = flushNextInstruction(&instructionInfo,&messageInfo,&sourceInfo);

		if (instInfo != nullptr) {
			*instInfo = &instructionInfo;
		}

		if (srcInfo != nullptr) {
			if (disassembler != nullptr) {
//				shouldn't need to call getsrclines. FlushNextInstruciotn should be callingbuildinstructionfromsrec which fills it all in
//
//				disassembler->getSrcLines(instructionInfo.address, &sourceInfo.sourceFile, &sourceInfo.sourceFunction, &sourceInfo.sourceLineNum, &sourceInfo.sourceLine);
//
				*srcInfo = &sourceInfo;
			}
		}

		return rc;
	}

	rc = computeBranchFlags(currentSrec[currentCore].pc,currentSrec[currentCore].inst,nextSrec.pc,crFlag,brFlags);
	if (rc != TraceDqr::DQERR_OK) {
		printf("Error: Simulator::NextInstruction(): could not compute branch flags\n");

		status = rc;
		return status;
	}

	currentCore = nextSrec.coreId;

	rc = buildInstructionFromSrec(&currentSrec[currentCore],brFlags,crFlag);

	currentSrec[currentCore] = nextSrec;

	if (instInfo != nullptr) {
		*instInfo = &instructionInfo;
	}

	if (disassembler != nullptr) {
		if (srcInfo != nullptr) {
			*srcInfo = &sourceInfo;
		}
	}

	// possible improvements:
	// caching of instruction/address
	// improve print callback disassembly routines to use stream as a pointer to a struct with a buffer and
	// and length to improve performance and allow multithreading (or multi object)
	// improve disassembly to not print 32 bit literals as 64 bits
	// bfd.h seems outof sync with libbfe. bfd_arch_riskv is wrong! Update bfd.h in lib\xx\bfd.h

	return TraceDqr::DQERR_OK;
}
