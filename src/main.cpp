/*
 * Copyright 2019 Sifive, Inc.
 *
 * main.cpp
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

using namespace std;

static void usage(char *name)
{
	printf("Usage: dqr -t tracefile -e elffile [-ca cafile -catype (none | instruction | vector)] [-btm | -htm] -basename name\n");
	printf("           [-pf propfile] [-srcbits=nn] [-src] [-nosrc] [-file] [-nofile] [-func] [-nofunc] [-dasm] [-nodasm]\n");
	printf("           [-trace] [-notrace] [-pathunix] [-pathwindows] [-pathraw] [--strip=path] [-itcprint | -itcprint=n] [-noitcprint]\n");
	printf("           [-addrsize=n] [-addrsize=n+] [-32] [-64] [-32+] [-archsize=nn] [-addrsep] [-noaddrsep] [-analytics | -analyitcs=n]\n");
	printf("           [-noanalytics] [-freq nn] [-tssize=n] [-callreturn] [-nocallreturn] [-branches] [-nobranches] [-msglevel=n]\n");
	printf("           [-cutpath=<base path>] [-s file] [-r addr] [-labels] [-nolables] [-debug] [-nodebug] [-v] [-h]\n");
	printf("\n");
	printf("-t tracefile: Specify the name of the Nexus trace message file. Must contain the file extension (such as .rtd).\n");
	printf("-e elffile:   Specify the name of the executable elf file. Must contain the file extension (such as .elf).\n");
	printf("-s simfile:   Specify the name of the simulator output file. When using a simulator output file, cannot use\n");
	printf("              a tracefile (-t option). Can provide an elf file (-e option), but is not required.\n");
	printf("-pf propfile: Specify a properties file containing information on trace. Properties may be overridden using command\n");
	printf("              flags.\n");
	printf("-ca cafile:   Specify the name of the cycle accurate trace file. Must also specify the -t and -e switches.\n");
	printf("-catype nn:   Specify the type of the CA trace file. Valid options are none, instruction, and vector\n");
	printf("-btm:         Specify the type of the trace file as btm (branch trace messages). On by default.\n");
	printf("-htm:         Specify the type of the trace file as htm (history trace messages).\n");
	printf("-n basename:  Specify the base name of the Nexus trace message file and the executable elf file. No extension\n");
	printf("              should be given. The extensions .rtd and .elf will be added to basename.\n");
	printf("-cutpath=<cutPath>[,<newRoot>]: When searching for source files, <cutPath> is removed from the beginning of thepath name\n");
	printf("              found in the elf file for the source file name. If <newRoot> is given, it is prepended to the begging of the\n");
	printf("              after removing <cutPath>. If <cutPath> is not found, <newRoot> is not prepended. This allows having a local copy\n");
	printf("              of the source file sub-tree. If <cutPath> is not part of the file location, the original source path is used.\n");
	printf("-src:         Enable display of source lines in output if available (on by default).\n");
	printf("-nosrc:       Disable display of source lines in output.\n");
	printf("-file:        Display source file information in output (on by default).\n");
	printf("-nofile:      Do not display source file information.\n");
	printf("-dasm:        Display disassembled code in output (on by default).\n");
	printf("-nodasm:      Do not display disassembled code in output.\n");
	printf("-func:        Display function name with source information (off by default).\n");
	printf("-nofunc:      Do not display function information with source information.\n");
	printf("-trace:       Display trace information in output (off by default).\n");
	printf("-notrace:     Do not display trace information in output.\n");
	printf("--strip=path: Strip of the specified path when displaying source file name/path. Strips off all that matches.\n");
	printf("              Path may be enclosed in quotes if it contains spaces.\n");
	printf("-itcprint:    Display ITC 0 data as a null terminated string. Data from consecutive ITC 0's will be concatenated\n");
	printf("              and displayed as a string until a terminating \\0 is found. Also enables processing and display of\n");
	printf("              no-load-strings.\n");
	printf("-itcprint=n:  Display ITC channel n data as a null terminated string. Data for consecutive ITC channel n's will be\n");
	printf("              concatenated and display as a string until a terminating \\n or \\0 is found. Also enabled processing\n");
	printf("              and display of no-load-strings\n");
	printf("-noitcprint:  Display ITC 0 data as a normal ITC message; address, data pair\n");
	printf("-nls:         Enables processing of no-load-strings\n");
	printf("-nonls:       Disable processing of no-load-strings.\n");
	printf("-addrsize=n:  Display address as n bits (32 <= n <= 64). Values larger than n bits will print, but take more space and\n");
	printf("              cause the address field to be jagged. Overrides value address size read from elf file.\n");
	printf("-addrsize=n+: Display address as n bits (32 <= n <= 64) unless a larger address size is seen, in which case the address\n");
	printf("              size is increased to accommodate the larger value. When the address size is increased, it stays increased\n");
	printf("              (sticky) and will be again increased if a new larger value is encountered. Overrides the address size\n");
	printf("              read from the elf file.\n");
	printf("-32:          Display addresses as 32 bits. Values lager than 32 bits will print, but take more space and cause\n");
	printf("              the address field to be jagged. Selected by default if elf file indicates 32 bit address size.\n");
	printf("              Specifying -32 overrides address size read from elf file\n");
	printf("-32+          Display addresses as 32 bits until larger addresses are displayed and then adjust up to a larger\n");
	printf("              enough size to display the entire address. When addresses are adjusted up, they do not later adjust\n");
	printf("              back down, but stay at the new size unless they need to adjust up again. This is the default setting\n");
	printf("              if the elf file specifies > 32 bit address size (such as 64). Specifying -32+ overrides the value\n");
	printf("              read from the elf file\n");
	printf("-64:          Display addresses as 64 bits. Overrides value read from elf file\n");
	printf("-archsize=nn: Set the architecture size to 32 or 64 bits instead of getting it from the elf file\n");
	printf("-addrsep:     For addresses greater than 32 bits, display the upper bits separated from the lower 32 bits by a '-'\n");
	printf("-noaddrsep:   Do not add a separator for addresses greater than 32 bit between the upper bits and the lower 32 bits\n");
	printf("              (default).\n");
	printf("-srcbits=n:   The size in bits of the src field in the trace messages. n must 0 to 16. Setting srcbits to 0 disables\n");
	printf("              multi-core. n > 0 enables multi-core. If the -srcbits=n switch is not used, srcbits is 0 by default.\n");
	printf("-labels:      Treat labels as functions for source information and disassembly. On by default.\n");
	printf("-nolables:    Do not use local labels as function names when returning source information or instruction location information.\n");
	printf("-analytics:   Compute and display detail level 1 trace analytics.\n");
	printf("-analytics=n: Specify the detail level for trace analytics display. N sets the level to either 0 (no analytics display)\n");
	printf("              1 (sort system totals), or 2 (display analytics by core).\n");
	printf("-noanaylitics: Do not compute and display trace analytics (default). Same as -analytics=0.\n");
	printf("-freq nn:     Specify the frequency in Hz for the timestamp tics clock. If specified, time instead\n");
	printf("              of tics will be displayed.\n");
	printf("-tssize=n:    Specify size in bits of timestamp counter; used for timestamp wrap\n");
	printf("-callreturn:  Annotate calls, returns, and exceptions\n");
	printf("-nocallreturn Do not annotate calls, returns, exceptions (default)\n");
	printf("-branches:    Annotate conditional branches with taken or not taken information\n");
	printf("-nobrnaches:  Do not annotate conditional branches with taken or not taken information (default)\n");
	printf("-pathunix:    Show all file paths using unix-type '/' path separators (default)\n");
	printf("              Also cleans up path, removing // -> /, /./ -> /, and uplevels for each /../\n");
	printf("-pathwindows: Show all file paths using windows-type '\\' path separators\n");
	printf("              Also cleans up path, removing // -> /, /./ -> /, and uplevels for each /../\n");
	printf("-pathraw:     Show all file path in the format stored in the elf file\n");
	printf("-msglevel=n:  Set the Nexus trace message detail level. n must be >= 0, <= 3\n");
	printf("-r addr:      Display the label information for the address specified for the elf file specified\n");
	printf("-debug:       Display some debug information for the trace to aid in debugging the trace decoder\n");
	printf("-nodebug:     Do not display any debug information for the trace decoder\n");
	printf("-v:           Display the version number of the DQer and exit.\n");
	printf("-h:           Display this usage information.\n");
}

static const char *stripPath(const char *prefix,const char *srcpath)
{
	if (prefix == nullptr) {
		return srcpath;
	}

	if (srcpath == nullptr) {
		return nullptr;
	}

	const char *s = srcpath;

	for (;;) {
		if (*prefix == 0) {
			return s;
		}

		if (tolower(*prefix) == tolower(*s)) {
			prefix += 1;
			s += 1;
		}
		else if (*prefix == '/') {
			if (*s != '\\') {
				return srcpath;
			}
			prefix += 1;
			s += 1;
		}
		else if (*prefix == '\\') {
			if (*s != '/') {
				return srcpath;
			}
			prefix += 1;
			s += 1;
		}
		else {
			return srcpath;
		}
	}

	return nullptr;
}

int main(int argc, char *argv[])
{
	char *tf_name = nullptr;
	char *base_name = nullptr;
	char *ef_name = nullptr;
	char *sf_name = nullptr;
	char *ca_name = nullptr;
	char *pf_name = nullptr;
	char buff[128];
	int buff_index = 0;
	bool usage_flag = false;
	bool version_flag = false;
	bool src_flag = true;
	bool file_flag = true;
	bool dasm_flag = true;
	bool trace_flag = false;
	bool func_flag = false;
	int tssize = 40;
	uint32_t freq = 0;
	char *strip_flag = nullptr;
	int  numAddrBits = 0;
	uint32_t addrDispFlags = 0;
	int srcbits = 0;
	int analytics_detail = 0;
	int itcPrintOpts = TraceDqr::ITC_OPT_NLS;
	int itcPrintChannel = 0;
	bool showCallsReturns = false;
	bool showBranches = false;
	TraceDqr::pathType pt = TraceDqr::PATH_TO_UNIX;
	int archSize = 32;
	int msgLevel = 2;
	bool labelFlag = true;
	TraceDqr::CATraceType caType = TraceDqr::CATRACE_NONE;
	TraceDqr::TraceType traceType = TraceDqr::TRACETYPE_BTM;
	char *cutPath = nullptr;
	char *newRoot = nullptr;
	bool ctf_flag = false;

	for (int i = 1; i < argc; i++) {
		if (strcmp("-t",argv[i]) == 0) {
			i += 1;
			if (i >= argc) {
				printf("Error: option -t requires a file name\n");
				usage(argv[0]);
				return 1;
			}

			base_name = nullptr;
			sf_name = nullptr;

			tf_name = argv[i];
		}
		else if (strcmp("-n",argv[i]) == 0) {
			i += 1;
			if (i >= argc) {
				printf("Error: option -n requires a file name\n");
				usage(argv[0]);
				return 1;
			}

			ef_name = nullptr;
			tf_name = nullptr;
			sf_name = nullptr;

			base_name = argv[i];
		}
		else if (strcmp("-e",argv[i]) == 0) {
			i += 1;
			if (i >= argc) {
				printf("Error: option -e requires a file name\n");
				usage(argv[0]);
				return 1;
			}

			base_name = nullptr;

			ef_name = argv[i];
		}
		else if (strcmp("-pf",argv[i]) == 0) {
			i += 1;
			if (i >= argc) {
				printf("Error: option -pf requires a file name\n");
				usage(argv[0]);
				return 1;
			}

			pf_name = argv[i];
		}
		else if (strcmp("-ca",argv[i]) == 0) {
			i += 1;
			if (i >= argc) {
				printf("Error: option -ca requires a file name\n");
				usage(argv[0]);
				return 1;
			}

			ca_name = argv[i];
		}
		else if (strcmp("-ctf",argv[i]) == 0) {
			ctf_flag = true;
		}
		else if (strcmp("-noctf",argv[i]) == 0) {
			ctf_flag = false;
		}
		else if (strcmp("-btm",argv[i]) == 0) {
			traceType = TraceDqr::TRACETYPE_BTM;
		}
		else if (strcmp("-htm",argv[i]) == 0) {
			traceType = TraceDqr::TRACETYPE_HTM;
		}
		else if (strcmp("-src",argv[i]) == 0) {
			src_flag = true;
		}
		else if (strcmp("-nosrc",argv[i]) == 0) {
			src_flag = false;
		}
		else if (strcmp("-file",argv[i]) == 0) {
			file_flag = true;
		}
		else if (strcmp("-nofile",argv[i]) == 0) {
			file_flag = false;
		}
		else if (strcmp("-dasm",argv[i]) == 0) {
			dasm_flag = true;
		}
		else if (strcmp("-nodasm",argv[i]) == 0) {
			dasm_flag = false;
		}
		else if (strcmp("-trace",argv[i]) == 0) {
			trace_flag = true;
		}
		else if (strcmp("-notrace",argv[i]) == 0) {
			trace_flag = false;
		}
		else if (strcmp("-func",argv[i]) == 0) {
			func_flag = true;
		}
		else if (strcmp("-nofunc",argv[i]) == 0) {
			func_flag = false;
		}
		else if (strncmp("--strip=",argv[i],strlen("--strip=")) == 0) {
			strip_flag = argv[i]+strlen("--strip=");

			if (strip_flag[0] == '"') {
				strip_flag += 1;

				if (strip_flag[strlen(strip_flag)] == '"') {
					strip_flag[strlen(strip_flag)] = 0;
				}
			}
		}
		else if (strncmp("-cutpath=",argv[i],strlen("-cutpath=")) == 0) {
			cutPath = argv[i]+strlen("-cutpath=");

			// see if newRoot is provided

			int i;
			for (i = 0; (cutPath[i] != 0) && (cutPath[i] != ','); i++) { /* empty */ }

			if (cutPath[i] == ',') {
				cutPath[i] = 0;

				newRoot = &cutPath[i+1];
			}
		}
		else if (strcmp("-v",argv[i]) == 0) {
			version_flag = true;
		}
		else if (strcmp("-h",argv[i]) == 0) {
			usage_flag = true;
		}
		else if (strcmp("-itcprint",argv[i]) == 0) {
			itcPrintOpts |= TraceDqr::ITC_OPT_PRINT | TraceDqr::ITC_OPT_NLS;
			itcPrintChannel = 0;
		}
		else if (strncmp("-itcprint=",argv[i],strlen("-itcprint=")) == 0) {
			int l;
			char *endptr;

			l = strtol(&argv[i][strlen("-itcprint=")], &endptr, 0);

			if (endptr[0] == 0 ) {
				itcPrintOpts |= TraceDqr::ITC_OPT_PRINT | TraceDqr::ITC_OPT_NLS;
				itcPrintChannel = l;
			}
			else {
				itcPrintOpts = false;
				printf("Error: option -itcprint= requires a valid number 0 - 31\n");
				usage(argv[0]);
				return 1;
			}
		}
		else if (strcmp("-noitcprint",argv[i]) == 0) {
			itcPrintOpts &= ~TraceDqr::ITC_OPT_PRINT;
		}
		else if (strcmp("-nls",argv[i]) == 0) {
			itcPrintOpts |= TraceDqr::ITC_OPT_NLS;
		}
		else if (strcmp("-nonls",argv[i]) == 0) {
			itcPrintOpts &= ~TraceDqr::ITC_OPT_NLS;
		}
		else if (strcmp("-32",argv[i]) == 0) {
			numAddrBits = 32;
			addrDispFlags = addrDispFlags & ~TraceDqr::ADDRDISP_WIDTHAUTO;
		}
		else if (strcmp("-64",argv[i]) == 0) {
			numAddrBits = 64;
			addrDispFlags = addrDispFlags & ~TraceDqr::ADDRDISP_WIDTHAUTO;
		}
		else if (strcmp("-32+",argv[i]) == 0) {
			numAddrBits = 32;
			addrDispFlags = addrDispFlags | TraceDqr::ADDRDISP_WIDTHAUTO;
		}
		else if (strncmp("-addrsize=",argv[i],strlen("-addrsize=")) == 0) {
			int l;
			char *endptr;

			l = strtol(&argv[i][strlen("-addrsize=")], &endptr, 10);

			if (endptr[0] == 0 ) {
				numAddrBits = l;
				addrDispFlags = addrDispFlags  & ~TraceDqr::ADDRDISP_WIDTHAUTO;
			}
			else if (endptr[0] == '+') {
				numAddrBits = l;
				addrDispFlags = addrDispFlags | TraceDqr::ADDRDISP_WIDTHAUTO;
			}
			else {
				printf("Error: option -addressize= requires a valid number <= 32, >= 64\n");
				usage(argv[0]);
				return 1;
			}

			if ((l < 32) || (l > 64)) {
				printf("Error: option -addressize= requires a valid number <= 32, >= 64\n");
				usage(argv[0]);
				return 1;
			}
		}
		else if (strcmp("-addrsep", argv[i]) == 0) {
			addrDispFlags = addrDispFlags | TraceDqr::ADDRDISP_SEP;
		}
		else if (strcmp("-noaddrsep", argv[i]) == 0) {
			addrDispFlags = addrDispFlags & ~TraceDqr::ADDRDISP_SEP;
		}
		else if (strncmp("-srcbits=",argv[i],strlen("-srcbits=")) == 0) {
			srcbits = atoi(argv[i]+strlen("-srcbits="));

			if ((srcbits < 0) || (srcbits > 8)) {
				printf("Error: option -srcbits=n, n must be a valid number of trace message src bits >= 0, <= 8\n");
				usage(argv[0]);
				return 1;
			}
		}
		else if (strcmp("-analytics",argv[i]) == 0) {
			analytics_detail = 1;
		}
		else if (strncmp("-analytics=",argv[i],strlen("-analytics=")) == 0) {
			analytics_detail = atoi(argv[i]+strlen("-analytics="));

			if (analytics_detail < 0) {
				printf("Error: option -analytics=n, n must be a valid number >= 0\n");
				usage(argv[0]);
				return 1;
			}
		}
		else if (strcmp("-noanalytics",argv[i]) == 0) {
			analytics_detail = 0;
		}
		else if (strcmp("-freq",argv[i]) == 0) {
			i += 1;
			if (i >= argc) {
				printf("Error: option -freq requires a clock frequency to be specified\n");
				usage(argv[0]);
				return 1;
			}

			freq = atoi(argv[i]);
			if (freq < 0) {
				printf("Error: clock frequency must be >= 0\n");
				return 1;
			}
		}
		else if (strncmp("-tssize=",argv[i],sizeof "-tssize=") == 0) {
			tssize = atoi(argv[i]+strlen("-tssize="));

			if ((tssize <= 0) || (tssize > 64)) {
				printf("Error: tssize must be > 0, <= 64");
				return 1;
			}
		}
		else if (strcmp("-callreturn",argv[i]) == 0 ) {
			showCallsReturns = true;
		}
		else if (strcmp("-nocallreturn",argv[i]) == 0 ) {
			showCallsReturns = false;
		}
		else if (strcmp("-branches",argv[i]) == 0 ) {
			showBranches = true;
		}
		else if (strcmp("-nobranches",argv[i]) == 0 ) {
			showBranches = false;
		}
		else if (strcmp("-pathunix",argv[i]) == 0) {
			pt = TraceDqr::PATH_TO_UNIX;
		}
		else if (strcmp("-pathwindows",argv[i]) == 0) {
			pt = TraceDqr::PATH_TO_WINDOWS;
		}
		else if (strcmp("-pathraw",argv[i]) == 0) {
			pt = TraceDqr::PATH_RAW;
		}
		else if (strcmp("-s",argv[i]) == 0) {
			i += 1;
			if (i >= argc) {
				printf("Error: option -s requires a file name\n");
				usage(argv[0]);
				return 1;
			}

			base_name = nullptr;
			tf_name = nullptr;
			ef_name = nullptr;

			sf_name = argv[i];
		}
		else if (strncmp("-archsize=",argv[i],sizeof "-archsize=") == 0) {
			archSize = atoi(argv[i]+strlen("-archsize="));

			if ((archSize != 32) && (archSize != 64)) {
				printf("Error: archSize must be 32 or 64\n");
				return 1;
			}
		}
		else if (strncmp("-msglevel=",argv[i],sizeof "-msglevel") == 0) {
			msgLevel = atoi(argv[i]+strlen("-msglevel="));

			if ((msgLevel < 0) || (msgLevel > 3)) {
				printf("Error: msgLevel must be >=0, <= 3\n");
				return 1;
			}
		}
		else if (strcmp("-r",argv[i]) == 0) {
			i += 1;
			if (i >= argc) {
				printf("Error: option -r requires an address\n");
				return 1;
			}

			if (ef_name == nullptr) {
				printf("option -r requires first specifying the elf file name (with the -e flag)\n");
				return 1;
			}

			ObjFile *of;
			TraceDqr::DQErr rc;

			of = new ObjFile(ef_name);
			rc = of->getStatus();
			if (rc != TraceDqr::DQERR_OK) {
				printf("Error: cannot create ObjFile object\n");
				return 1;
			}

			of->setLabelMode(labelFlag);

			while (i < argc) {
				uint32_t addr;
				char *endptr;

				addr = strtoul(argv[i],&endptr,0);
				if (endptr[0] != 0) {
					printf("Error: option -r requires a valid address\n");
					return 1;
				}

				Instruction instInfo;
				Source srcInfo;

				rc = of->sourceInfo(addr,instInfo,srcInfo);
				if (rc != TraceDqr::DQERR_OK) {
					printf("Error: cannot get sourceInfo for address 0x%08x\n");
				}

				printf("For address 0x%08x\n",addr);
				printf("File: %s:%d\n",srcInfo.sourceFile,srcInfo.sourceLineNum);
				printf("Function: %s\n",srcInfo.sourceFunction);
				printf("Src: %s\n",srcInfo.sourceLine);

				printf("Label: %s+0x%08x\n",instInfo.addressLabel,instInfo.addressLabelOffset);

				i += 1;
			}

			return 0;
		}
		else if (strcmp("-labels",argv[i]) == 0) {
			labelFlag = true;
		}
		else if (strcmp("-nolabels",argv[i]) == 0) {
			labelFlag = false;
		}
		else if (strcmp("-debug",argv[i]) == 0) {
			globalDebugFlag = 1;
		}
		else if (strcmp("-nodebug",argv[i]) == 0) {
			globalDebugFlag = 0;
		}
		else if (strcmp("-catype",argv[i]) == 0) {
			i += 1;
			if (i >= argc) {
				printf("Error: -catype flag requires a CA Trace type (none, instruction, or vector)\n");
				return 1;
			}
			if (strcmp("none",argv[i]) == 0) {
				caType = TraceDqr::CATRACE_NONE;
				ca_name = nullptr;
			}
			else if (strcmp("instruction",argv[i]) == 0) {
				caType = TraceDqr::CATRACE_INSTRUCTION;
			}
			else if (strcmp("vector",argv[i]) == 0) {
				caType = TraceDqr::CATRACE_VECTOR;
			}
			else {
				printf("Error: CA Trace type must be either none, instruction, or vector\n");
				return 1;
			}
		}
		else {
			printf("Unkown option '%s'\n",argv[i]);
			usage_flag = true;
		}
	}

	if (usage_flag) {
		usage(argv[0]);
		return 0;
	}

	if (version_flag) {
		printf("%s: version %s\n",argv[0],Trace::version());
		return 0;
	}

	if (base_name != nullptr) {
		tf_name = &buff[buff_index];
		strcpy(tf_name,base_name);
		strcat(tf_name,".rtd");
		buff_index += strlen(tf_name) + 1;

		ef_name = &buff[buff_index];
		strcpy(ef_name,base_name);
		strcat(ef_name,".elf");
		buff_index += strlen(ef_name) + 1;
	}

	Trace *trace = nullptr;
	Simulator *sim = nullptr;

	if (sf_name != nullptr) {
		if ( ef_name != nullptr) {
			sim = new (std::nothrow) Simulator(sf_name,ef_name);
		}
		else {
			sim = new (std::nothrow) Simulator(sf_name,archSize);
		}

		assert(sim != nullptr);

		if (sim->getStatus() != TraceDqr::DQERR_OK) {
			delete sim;
			sim = nullptr;
			printf("Error: new Simulator(%s,%d) failed\n",sf_name,archSize);

			return 1;
		}

		if (cutPath != nullptr) {
			TraceDqr::DQErr rc;

			rc = sim->subSrcPath(cutPath,newRoot);
			if (rc != TraceDqr::DQERR_OK) {
				printf("Error: Could not set cutPath or newRoot\n");
				return 1;
			}
		}

		sim->setLabelMode(labelFlag);
	}
	else if ((pf_name != nullptr) || (tf_name != nullptr)) {
		TraceDqr::DQErr rc;

		if (pf_name != nullptr) {
			// generate error message if anything was set to not-default!

			if (tf_name != nullptr) {
				printf("Error: cannot specify -t flag when -pf is also specified\n");
				return 1;
			}

			if (ef_name != nullptr) {
				printf("Error: cannot specify -e flag when -pf is also specified\n");
				return 1;
			}

			trace = new (std::nothrow) Trace(pf_name);

			assert(trace != nullptr);

			if (trace->getStatus() != TraceDqr::DQERR_OK) {
				delete trace;
				trace = nullptr;

				printf("Error: new Trace() failed\n",pf_name);

				return 1;
			}
		}
		else {
			trace = new (std::nothrow) Trace(tf_name,ef_name,numAddrBits,addrDispFlags,srcbits,freq);

			assert(trace != nullptr);

			if (trace->getStatus() != TraceDqr::DQERR_OK) {
				delete trace;
				trace = nullptr;

				printf("Error: new Trace(%s,%s) failed\n",tf_name,ef_name);

				return 1;
			}

			trace->setTraceType(traceType);

			if (ca_name != nullptr) {
				rc = trace->setCATraceFile(ca_name,caType);
				if (rc != TraceDqr::DQERR_OK) {
					printf("Error: Could not set cycle accurate trace file\n");
					return 1;
				}
			}

			trace->setTSSize(tssize);
			trace->setPathType(pt);

			if (cutPath != nullptr) {
				rc = trace->subSrcPath(cutPath,newRoot);
				if (rc != TraceDqr::DQERR_OK) {
					printf("Error: Could not set cutPath or newRoot\n");
					return 1;
				}
			}

			// NLS is on by default when the trace object is created. Only
			// set the print options if something has changed

			if (itcPrintOpts != TraceDqr::ITC_OPT_NLS) {
				trace->setITCPrintOptions(itcPrintOpts,4096,itcPrintChannel);
			}

			trace->setLabelMode(labelFlag);

			if (ctf_flag != false) {
				rc = trace->enableCTFConverter(-1,nullptr);
				if (rc != TraceDqr::DQERR_OK) {
					printf("Error: Could not set CTF file\n");
					return 1;
				}
			}
		}
	}
	else {
		printf("Error: must specify either simulator file, trace file, SWT trace server, properties file, or base name\n");
		usage(argv[0]);
		return 1;
	}

	TraceDqr::DQErr ec;

	// main loop

//	this shouldn't be next instruction. it should be next because we may not be generating instruction'
//	we may just be dumping raw traces, or we may be dumping traces with addresses or we may be doing
//	dissasembled instruction traces or we may be adding source code
//
//	flags:
//
//		raw trace messages
//		decoded trace message
//		include trace addresses
//		include disassembly
//		include source

	// we want to be able to select the level of output, from minimal (raw trace only) to full (everything)

//	do we select when we create the trace object?
//	does it always generate as much as possible and we jsut print what we want?
//
//	should not print, but should return a string or way to make a string!
//
//	should look at source code display!

	Instruction *instInfo;
	NexusMessage *msgInfo;
	Source *srcInfo;
	char dst[10000];
	int instlevel = 1;
	const char *lastSrcFile = nullptr;
	const char *lastSrcLine = nullptr;
	unsigned int lastSrcLineNum = 0;
	TraceDqr::ADDRESS lastAddress = 0;
	int lastInstSize = 0;
	bool firstPrint = true;
	uint32_t core_mask = 0;
	TraceDqr::TIMESTAMP startTime, endTime;

	do {
		if (sim != nullptr) {
			ec = sim->NextInstruction(&instInfo,&msgInfo,&srcInfo);
		}
		else {
			ec = trace->NextInstruction(&instInfo,&msgInfo,&srcInfo);
		}

		if (ec == TraceDqr::DQERR_OK) {
			if (srcInfo != nullptr) {
				if ((lastSrcFile != srcInfo->sourceFile) || (lastSrcLine != srcInfo->sourceLine) || (lastSrcLineNum != srcInfo->sourceLineNum)) {
					lastSrcFile = srcInfo->sourceFile;
					lastSrcLine = srcInfo->sourceLine;
					lastSrcLineNum = srcInfo->sourceLineNum;

					if (file_flag) {
						if (srcInfo->sourceFile != nullptr) {
							if (firstPrint == false) {
								printf("\n");
							}

							const char *sfp;

							sfp = stripPath(strip_flag,srcInfo->sourceFile);

							if (srcbits > 0) {
								printf("[%d] ",srcInfo->coreId);
							}

							int sfpl = 0;
							int sfl = 0;
							int stripped = 0;

							if (sfp != srcInfo->sourceFile) {
								sfpl = strlen(sfp);
								sfl = strlen(srcInfo->sourceFile);
								stripped = sfl - sfpl;
							}

							if (stripped < srcInfo->cutPathIndex) {
								printf("File: [");

								if (sfp != srcInfo->sourceFile) {
									printf("..");
								}

								for (int i = stripped; i < srcInfo->cutPathIndex; i++) {
									printf("%c",srcInfo->sourceFile[i]);
								}

								printf("]%s:%d\n",&srcInfo->sourceFile[srcInfo->cutPathIndex],srcInfo->sourceLineNum);
							}
							else {
								if (sfp != srcInfo->sourceFile) {
									printf("File: ..%s:%d\n",sfp,srcInfo->sourceLineNum);
								}
								else {
									printf("File: %s:%d\n",sfp,srcInfo->sourceLineNum);
								}
							}

							firstPrint = false;
						}
					}

					if (src_flag) {
						if (srcInfo->sourceLine != nullptr) {
							if (srcbits > 0) {
								printf("[%d] Source: %s\n",srcInfo->coreId,srcInfo->sourceLine);
							}
							else {
								printf("Source: %s\n",srcInfo->sourceLine);
							}

							firstPrint = false;
						}
					}
				}
			}

			if (dasm_flag && (instInfo != nullptr)) {
				instInfo->addressToText(dst,sizeof dst,0);

				if (func_flag) {
					if (srcbits > 0) {
						printf("[%d] ",instInfo->coreId);
					}
					if (instInfo->address != (lastAddress + lastInstSize / 8)) {
						if (instInfo->addressLabel != nullptr) {
							printf("<%s",instInfo->addressLabel);
							if (instInfo->addressLabelOffset != 0) {
								printf("+%x",instInfo->addressLabelOffset);
							}
							printf(">\n");
						}
					}

					lastAddress = instInfo->address;
					lastInstSize = instInfo->instSize;
				}

				if (srcbits > 0) {
					printf("[%d] ", instInfo->coreId);
				}

				int n;

				if (((sim != nullptr) || (ca_name != nullptr)) && (instInfo->timestamp != 0)) {
					n = printf("t:%d ",instInfo->timestamp);

					if (instInfo->caFlags & (TraceDqr::CAFLAG_PIPE0 | TraceDqr::CAFLAG_PIPE1)) {
						if (instInfo->caFlags & TraceDqr::CAFLAG_PIPE0) {
							n += printf("[0:%d",instInfo->pipeCycles);
						}
						else if (instInfo->caFlags & TraceDqr::CAFLAG_PIPE1) {
							n += printf("[1:%d",instInfo->pipeCycles);
						}

						if (instInfo->caFlags & TraceDqr::CAFLAG_VSTART) {
							n += printf("(%d)-%d(%dA,%dL,%dS)",instInfo->qDepth,instInfo->VIStartCycles,instInfo->arithInProcess,instInfo->loadInProcess,instInfo->storeInProcess);
																					}

						if (instInfo->caFlags & TraceDqr::CAFLAG_VARITH) {
							n += printf("-%dA",instInfo->VIFinishCycles);
						}

						if (instInfo->caFlags & TraceDqr::CAFLAG_VLOAD) {
							n += printf("-%dL",instInfo->VIFinishCycles);
						}

						if (instInfo->caFlags & TraceDqr::CAFLAG_VSTORE) {
							n += printf("-%dS",instInfo->VIFinishCycles);
						}

						n += printf("] ");
					}

					for (int i = n; i < 14; i++) {
						printf(" ");
					}
				}

				n = printf("    %s:",dst);

				for (int i = n; i < 20; i++) {
					printf(" ");
				}

				instInfo->instructionToText(dst,sizeof dst,instlevel);
				printf("  %s",dst);

				if (showBranches == true) {
					switch (instInfo->brFlags) {
					case TraceDqr::BRFLAG_none:
						break;
					case TraceDqr::BRFLAG_unknown:
						printf(" [u]");
						break;
					case TraceDqr::BRFLAG_taken:
						printf(" [t]");
						break;
					case TraceDqr::BRFLAG_notTaken:
						printf(" [nt]");
						break;
					}
				}

				if (showCallsReturns == true) {
					if (instInfo->CRFlag != TraceDqr::isNone) {
						const char *format = "%s";

						printf(" [");

						if (instInfo->CRFlag & TraceDqr::isCall) {
							printf(format,"Call");
							format = ",%s";
						}

						if (instInfo->CRFlag & TraceDqr::isReturn) {
							printf(format,"Return");
							format = ",%s";
						}

						if (instInfo->CRFlag & TraceDqr::isSwap) {
							printf(format,"Swap");
							format = ",%s";
						}

						if (instInfo->CRFlag & TraceDqr::isInterrupt) {
							printf(format,"Interrupt");
							format = ",%s";
						}

						if (instInfo->CRFlag & TraceDqr::isException) {
							printf(format,"Exception");
							format = ",%s";
						}

						if (instInfo->CRFlag & TraceDqr::isExceptionReturn) {
							printf(format,"Exception Return");
							format = ",%s";
						}

						printf("]");
					}
				}

				printf("\n");

				firstPrint = false;
			}

			if ((trace != nullptr) && trace_flag && (msgInfo != nullptr)) {
				// got the goods! Get to it!

				if (globalDebugFlag) {
					msgInfo->dumpRawMessage();
				}

				msgInfo->messageToText(dst,sizeof dst,msgLevel);

				if (firstPrint == false) {
					printf("\n");
				}

				if (srcbits > 0) {
					printf("[%d] ",msgInfo->coreId);
				}

				printf("Trace: %s",dst);

				printf("\n");

				firstPrint = false;
			}

			if ((trace != nullptr) && (itcPrintOpts != TraceDqr::ITC_OPT_NONE)) {
				std::string s;
				bool haveStr;

				core_mask = trace->getITCPrintMask();

				for (int core = 0; core_mask != 0; core++) {
					if (core_mask & 1) {
						s = trace->getITCPrintStr(core,haveStr,startTime,endTime);
						while (haveStr != false) {
							if (firstPrint == false) {
								printf("\n");
							}

							if (srcbits > 0) {
								printf("[%d] ",msgInfo->coreId);
							}

							std::cout << "ITC Print: ";

							if ((startTime != 0) || (endTime != 0)) {
								std::cout << "Msg Tics: <" << startTime << "-" << endTime << "> ";
							}

							std::cout << s;

							firstPrint = false;

							s = trace->getITCPrintStr(core,haveStr,startTime,endTime);
						}
					}

					core_mask >>= 1;
				}
			}
		}
	} while (ec == TraceDqr::DQERR_OK);

	if (ec == TraceDqr::DQERR_EOF) {
		if (firstPrint == false) {
			printf("\n");
		}
		printf("End of Trace File\n");
	}
	else {
		printf("Error (%d) terminated trace decode\n",ec);
		return 1;
	}

	if ((trace != nullptr) && (itcPrintOpts != TraceDqr::ITC_OPT_NONE)) {
		std::string s = "";
		bool haveStr;

		core_mask = trace->getITCFlushMask();

		for (int core = 0; core_mask != 0; core++) {
			if (core_mask & 1) {
				s = trace->flushITCPrintStr(core,haveStr,startTime,endTime);
				while (haveStr != false) {
					if (firstPrint == false) {
						printf("\n");
					}

					if (srcbits > 0) {
						printf("[%d] ",core);
					}

					std::cout << "ITC Print: ";

					if ((startTime != 0) || (endTime != 0)) {
						std::cout << "Msg Tics: <" << startTime << "-" << endTime << "> ";
					}

					std::cout << s;

					firstPrint = false;

					s = trace->flushITCPrintStr(core,haveStr,startTime,endTime);
				}
			}

			core_mask >>= 1;
		}
	}

	if (analytics_detail > 0) {
		if (trace != nullptr) {
			trace->analyticsToText(dst,sizeof dst,analytics_detail);
			if (firstPrint == false) {
				printf("\n");
			}
			firstPrint = false;
			printf("%s",dst);
		}
		if (sim != nullptr) {
			sim->analyticsToText(dst,sizeof dst,analytics_detail);
			if (firstPrint == false) {
				printf("\n");
			}
			firstPrint = false;
			printf("%s",dst);
		}
	}

	if (trace != nullptr) {
		trace->cleanUp();

		delete trace;
		trace = nullptr;
	}

	if (sim != nullptr) {
		delete sim;
		sim = nullptr;
	}

	return 0;
}
